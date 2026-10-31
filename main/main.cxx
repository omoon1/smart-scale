#include "config.hxx"
#include "device.hxx"
#include "session.hxx"
#include "ui.hxx"
#include "battery_history.hxx"
#include "acquisition.hxx"
#include "diagnostic_logger.hxx"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_heap_caps.h"
#include <memory>
#include <cstdio>
#include <algorithm>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

namespace {
// Keep ordinary scale events; anomaly capture and trace dumps are disabled.
void observe_scale(void*, const smart_scale::Scale& scale, const char* event) {
    if (std::strcmp(event, "sample") != 0)
        smart_scale::device::DiagnosticLogger::instance().sample(event,
            smart_scale::diagnostic_sample(scale, esp_timer_get_time() / 1000));
}
}

extern "C" void app_main() {
    // Volatile history belongs in PSRAM and starts empty on every boot.
    void* history_storage = heap_caps_malloc(sizeof(smart_scale::BatteryHistory), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(history_storage ? ESP_OK : ESP_ERR_NO_MEM);
    auto& history = *new (history_storage) smart_scale::BatteryHistory;
    auto& logger = smart_scale::device::DiagnosticLogger::instance();
    logger.start();
    auto& display = smart_scale::device::display::device();
    display.init();
    // Session history is the largest application object; keep it out of DMA RAM.
    void* storage = heap_caps_malloc(sizeof(smart_scale::Controller), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(storage ? ESP_OK : ESP_ERR_NO_MEM);
    auto release = [](smart_scale::Controller* value) { value->~Controller(); heap_caps_free(value); };
    std::unique_ptr<smart_scale::Controller, decltype(release)> controller(
        new (storage) smart_scale::Controller(true), release);
    controller->scale().observe(observe_scale, nullptr);
    static smart_scale::device::scale::Acquisition acquisition;
    ESP_ERROR_CHECK(acquisition.start());
    const auto storage_status = smart_scale::device::scale::init_storage();
    if (storage_status == ESP_OK) {
        if (auto calibration = smart_scale::device::scale::load_calibration())
            controller->scale().restore(*calibration);
    } else ESP_LOGE("scale", "NVS unavailable: %s", esp_err_to_name(storage_status));
    char configuration[768];
    const auto* app = esp_app_get_description();
    std::snprintf(configuration, sizeof(configuration),
        "fw=%s idf=%s source=hx711 dout=%d sck=%d channel=A gain=128 rate=hardware_selected median_n=3 fast_tau_s=%.2f quiet_tau_s=%.2f stable_window_ms=%llu enter_sd_g=%.2f exit_sd_g=%.2f half_shift_g=%.2f anchor_shift_g=%.2f hard_shift_g=%.2f drift_limit_gps=%.2f scale_trace=disabled hx711_diag=disabled",
        app->version, app->idf_ver, smart_scale::config::hx711_dout, smart_scale::config::hx711_sck,
        static_cast<double>(smart_scale::config::filter_fast_tau_s), static_cast<double>(smart_scale::config::filter_quiet_tau_s),
        static_cast<unsigned long long>(smart_scale::config::stable_window_ms),
        static_cast<double>(smart_scale::config::stable_noise_enter_g), static_cast<double>(smart_scale::config::stable_noise_exit_g),
        static_cast<double>(smart_scale::config::stable_half_shift_g), static_cast<double>(smart_scale::config::stable_anchor_shift_g),
        static_cast<double>(smart_scale::config::stable_hard_shift_g), static_cast<double>(smart_scale::config::stability_drift_g_s));
    logger.text("scale_config", configuration);
    logger.sample("startup", smart_scale::diagnostic_sample(controller->scale(), esp_timer_get_time() / 1000));
    static smart_scale::ui::View view(*controller);
    if (lvgl_port_lock(0)) {
        view.init();
        ESP_ERROR_CHECK(display.show());
        lvgl_port_unlock();
    }
    auto previous = esp_timer_get_time();
    std::uint64_t health_ms = 0;
    unsigned sleep_cycle = 0;
    bool previous_inhibit = false;
    bool awaiting_stable = false;
    std::uint64_t resume_ms = 0;
    logger.event("power", "event=config fw=%s idf=%s reset_reason=%d sleep_enabled=%d wake_gpio=8 touch_mask=0x01 dim_ms=30000 sleep_ms=60000 retry_ms=30000 source=unknown",
        app->version, app->idf_ver, static_cast<int>(esp_reset_reason()), display.sleep_supported());
    smart_scale::PowerPolicy power(previous / 1000);
    auto applied_power = smart_scale::PowerState::active;
    std::uint64_t battery_sample_ms = 0;
    bool battery_sampled = false;
    bool battery_valid = false;
    smart_scale::BatterySnapshot current_battery;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(smart_scale::config::ui_period_ms));
        const auto now = esp_timer_get_time();
        const auto elapsed = static_cast<float>(now - previous) / 1000000.0F;
        previous = now;
        std::optional<smart_scale::Calibration> save;

        if (lvgl_port_lock(0)) {
            const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
            smart_scale::device::scale::Reading reading;

            for (unsigned i = 0; i < acquisition.capacity && acquisition.receive(reading); ++i) {
                if (reading.gap) { controller->sensor_fault(); power.reset_weight(); }
                if (now_ms > reading.ms && now_ms - reading.ms > smart_scale::config::sensor_timeout_ms) {
                    controller->sensor_fault();
                    power.reset_weight();
                    continue;
                }
                controller->sensor_sample(reading.raw, reading.ms);
            }
            controller->sensor_sample(std::nullopt, static_cast<std::uint64_t>(esp_timer_get_time() / 1000));

            if (display.reset_pressed()) {
                logger.event("power", "event=activity reason=button state=%d", static_cast<int>(applied_power));
                if (power.state() != smart_scale::PowerState::screen_off) view.reset();
                power.activity(now_ms);
            }
            // Wake-only button never invokes reset/tare; held input prevents idle.
            if (display.wake_button_pressed()) power.activity(now_ms);
            if (display.take_touch_activity()) {
                if (applied_power != smart_scale::PowerState::active)
                    logger.event("power", "event=activity reason=touch state=%d", static_cast<int>(applied_power));
                power.activity(now_ms);
            }
            const auto idle_before_weight = power.idle_ms(now_ms);
            power.weight(controller->scale().ready() ? std::optional<float>{controller->scale().gross()} : std::nullopt, controller->scale().sample_ms());
            if (power.idle_ms(now_ms) < idle_before_weight && applied_power != smart_scale::PowerState::active)
                logger.event("power", "event=activity reason=weight state=%d", static_cast<int>(applied_power));
            if (!battery_sampled || now_ms - battery_sample_ms >= 5000) {
                const auto battery = display.battery();
                current_battery = battery;
                view.battery(battery);
                if (!battery_sampled || battery.valid != battery_valid)
                    ESP_LOGI("power", "battery telemetry %s", battery.valid ? "available" : "unavailable");
                battery_valid = battery.valid;
                battery_sample_ms = now_ms; battery_sampled = true;
            }
            history.update(now_ms / 1000, smart_scale::BatteryHistory::state(current_battery), current_battery);
            view.history(history);
            controller->tick(elapsed);
            view.update();
            const bool inhibit = view.power_inhibited();
            if (inhibit != previous_inhibit) {
                logger.event("power", "event=inhibit active=%d phase=%d", inhibit, static_cast<int>(controller->session().phase));
                previous_inhibit = inhibit;
            }
            const auto desired = power.update(now_ms, inhibit);
            if (awaiting_stable && controller->scale().ready() && controller->scale().stable()) {
                logger.event("power", "event=measurement_stable cycle=%u latency_ms=%llu", sleep_cycle,
                    static_cast<unsigned long long>(now_ms - resume_ms));
                awaiting_stable = false;
            }
            if (now_ms - health_ms >= 30000) {
                logger.event("power", "event=health state=%d idle_ms=%llu inhibited=%d sleep_due=%d touch_down=%d ready=%d stable=%d awaiting_stable=%d log_dropped=%u queue_hwm=%u",
                    static_cast<int>(applied_power), static_cast<unsigned long long>(power.idle_ms(now_ms)), inhibit,
                    power.sleep_due(now_ms), display.touch_down(), controller->scale().ready(), controller->scale().stable(),
                    awaiting_stable, logger.dropped(), logger.high_water());
                display.log_touch_health(); health_ms = now_ms;
            }
            if (desired != applied_power) {
                const auto error = display.power_state(desired);
                if (error == ESP_OK) {
                    logger.event("power", "event=state from=%d to=%d idle_ms=%llu", static_cast<int>(applied_power), static_cast<int>(desired), static_cast<unsigned long long>(power.idle_ms(now_ms)));
                    applied_power = desired;
                } else ESP_LOGW("power", "display transition failed: %s", esp_err_to_name(error));
            }
            save = controller->scale().take_pending_save();
            // Save requests and dialog/brew operations must finish before sleep.
            if (power.sleep_due(now_ms) && !save && applied_power == smart_scale::PowerState::screen_off &&
                !display.touch_down() && power.report_sleep_once()) {
                if (!display.sleep_supported()) {
                    ESP_LOGW("power", "sleep blocked: enable verified 3.49 V2 expander touch wake; remaining screen-off");
                } else {
                    ++sleep_cycle;
                    bool precheck_pressed{};
                    const auto precheck = display.touch_pressed(precheck_pressed);
                    logger.event("power", "event=sleep_precheck cycle=%u result=%s pressed=%d", sleep_cycle,
                        esp_err_to_name(precheck), precheck_pressed);
                    if (precheck != ESP_OK || precheck_pressed) {
                        const auto checked_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
                        if (precheck == ESP_OK && precheck_pressed) {
                            display.suppress_wake_touch();
                            power.activity(checked_ms);
                        }
                        else power.retry_sleep(checked_ms);
                        logger.event("power", "event=sleep_deferred cycle=%u result=%s user_wake=%d acquisition_untouched=1 retry_ms=%d",
                            sleep_cycle, esp_err_to_name(precheck), precheck == ESP_OK && precheck_pressed,
                            precheck == ESP_OK && precheck_pressed ? 0 : 30000);
                    } else {
                        logger.event("power", "event=suspend_begin cycle=%u", sleep_cycle);
                        const auto suspend_at = esp_timer_get_time();
                        const bool stopped = acquisition.suspend();
                        logger.event("power", "event=suspend_end cycle=%u ok=%d elapsed_us=%llu", sleep_cycle, stopped,
                            static_cast<unsigned long long>(esp_timer_get_time() - suspend_at));
                        uint64_t sleep_start{}, sleep_end{};
                        bool user_wake = false;
                        const auto error = stopped ? display.light_sleep(sleep_start, sleep_end, user_wake, sleep_cycle) : ESP_ERR_TIMEOUT;
                        const auto resumed_s = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
                        if (sleep_end > sleep_start) {
                            // Only the successful sleep call belongs to Sleep. Setup and failed attempts stay Active.
                            const auto start_s = sleep_start / 1000000, end_s = sleep_end / 1000000;
                            const auto sleep_state = smart_scale::BatteryHistory::state(current_battery, true);
                            history.update(start_s, sleep_state, current_battery);
                            current_battery = display.battery();
                            history.update(end_s, smart_scale::BatteryHistory::state(current_battery), current_battery);
                        } else current_battery = display.battery();
                        history.update(resumed_s, smart_scale::BatteryHistory::state(current_battery), current_battery);
                        view.battery(current_battery);
                        view.history(history);
                        logger.event("power", "event=measurement_resume cycle=%u", sleep_cycle);
                        acquisition.resume(); // also cancel a timed-out suspend request
                        controller->resume_measurement();
                        power.reset_weight(); // discard pre-sleep baseline, including aborted attempts
                        const auto resumed = esp_timer_get_time();
                        if (user_wake) power.activity(resumed / 1000);
                        else power.retry_sleep(resumed / 1000);
                        previous = resumed; // exclude sleep time from session clocks
                        resume_ms = resumed / 1000; awaiting_stable = true;
                        battery_sampled = false;
                        logger.event("power", "event=sleep_result cycle=%u result=%s slept=%d sleep_us=%llu wake_cause=%d user_wake=%d retry_ms=%d",
                            sleep_cycle, esp_err_to_name(error), sleep_end > sleep_start,
                            static_cast<unsigned long long>(sleep_end > sleep_start ? sleep_end - sleep_start : 0),
                            sleep_end > sleep_start ? static_cast<int>(esp_sleep_get_wakeup_cause()) : -1, user_wake, user_wake ? 0 : 30000);
                        view.update();
                        if (user_wake) {
                            const auto shown = display.power_state(smart_scale::PowerState::active);
                            logger.event("power", "event=display_restore cycle=%u result=%s", sleep_cycle, esp_err_to_name(shown));
                            if (shown == ESP_OK) applied_power = smart_scale::PowerState::active;
                        }
                    } // precheck passed; acquisition was interrupted
                }
            }
            lvgl_port_unlock();
        }
        if (save) {
            const auto error = smart_scale::device::scale::save_calibration(*save);
            if (error != ESP_OK) ESP_LOGE("scale", "Calibration save failed: %s", esp_err_to_name(error));
            if (lvgl_port_lock(0)) {
                controller->scale().saved(error == ESP_OK);
                lvgl_port_unlock();
            }
        }
    }
}
