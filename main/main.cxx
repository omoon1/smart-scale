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

namespace {
struct Diagnostics {
    smart_scale::AnomalyTrace trace;
    static void observe(void* context, const smart_scale::Scale& scale, const char* event) {
        auto& self = *static_cast<Diagnostics*>(context);
        auto& logger = smart_scale::device::DiagnosticLogger::instance();
        auto sample = smart_scale::diagnostic_sample(scale, esp_timer_get_time() / 1000);
        if (std::strcmp(event, "sample") == 0) {
            if (const auto trigger = self.trace.push(sample)) logger.sample(trigger, sample, self.trace.id());
        } else {
            if (std::strcmp(event, "sensor_invalidated") == 0 || std::strcmp(event, "overload") == 0)
                self.trace.begin(sample.now_ms, event);
            logger.sample(event, sample);
        }
    }
};
}

extern "C" void app_main() {
    // Volatile history belongs in PSRAM and starts empty on every boot.
    void* history_storage = heap_caps_malloc(sizeof(smart_scale::BatteryHistory), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(history_storage ? ESP_OK : ESP_ERR_NO_MEM);
    auto& history = *new (history_storage) smart_scale::BatteryHistory;
    auto& logger = smart_scale::device::DiagnosticLogger::instance();
    logger.start();
    // Large capture history belongs in PSRAM, never on app_main's stack.
    void* diagnostic_storage = heap_caps_malloc(sizeof(Diagnostics), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(diagnostic_storage ? ESP_OK : ESP_ERR_NO_MEM);
    auto& diagnostics = *new (diagnostic_storage) Diagnostics;
    auto& display = smart_scale::device::display::device();
    display.init();
    // Session history is the largest application object; keep it out of DMA RAM.
    void* storage = heap_caps_malloc(sizeof(smart_scale::Controller), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(storage ? ESP_OK : ESP_ERR_NO_MEM);
    auto release = [](smart_scale::Controller* value) { value->~Controller(); heap_caps_free(value); };
    std::unique_ptr<smart_scale::Controller, decltype(release)> controller(
        new (storage) smart_scale::Controller(true), release);
    controller->scale().observe(Diagnostics::observe, &diagnostics);
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
        "fw=%s idf=%s source=hx711 dout=%d sck=%d channel=A gain=128 rate=observed_in_health median_n=3 fast_tau_s=%.2f quiet_tau_s=%.2f stable_window_ms=%llu enter_sd_g=%.2f exit_sd_g=%.2f half_shift_g=%.2f anchor_shift_g=%.2f hard_shift_g=%.2f drift_limit_gps=%.2f trace_pre_ms=5000 trace_post_ms=5000 trace_jump_g=1 trace_capacity=1024 cooldown_ms=30000",
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
    std::uint64_t last_diagnostic_ms = previous / 1000, previous_received = 0, max_age_ms = 0;
    unsigned max_batch = 0;
    std::uint64_t acquisition_seq = 0;
    unsigned started_trace = 0;
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
        char diagnostic[1024]{};
        if (lvgl_port_lock(0)) {
            const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
            smart_scale::device::scale::Reading reading;
            unsigned batch = 0;
            for (unsigned i = 0; i < acquisition.capacity && acquisition.receive(reading); ++i) {
                ++batch;
                acquisition_seq = reading.seq;
                max_age_ms = std::max(max_age_ms, now_ms > reading.ms ? now_ms - reading.ms : 0);
                if (reading.gap) controller->sensor_fault();
                if (now_ms > reading.ms && now_ms - reading.ms > smart_scale::config::sensor_timeout_ms) {
                    controller->sensor_fault();
                    continue;
                }
                controller->sensor_sample(reading.raw, reading.ms);
            }
            controller->sensor_sample(std::nullopt, static_cast<std::uint64_t>(esp_timer_get_time() / 1000));
            max_batch = std::max(max_batch, batch);
            if (display.reset_pressed()) {
                if (power.state() != smart_scale::PowerState::screen_off) view.reset();
                power.activity(now_ms);
            }
            if (display.take_touch_activity()) power.activity(now_ms);
            power.weight(controller->scale().ready() ? std::optional<float>{controller->scale().gross()} : std::nullopt, now_ms);
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
            const auto desired = power.update(now_ms, view.power_inhibited());
            if (desired != applied_power) {
                const auto error = display.power_state(desired);
                if (error == ESP_OK) {
                    ESP_LOGI("power", "state %d -> %d", static_cast<int>(applied_power), static_cast<int>(desired));
                    applied_power = desired;
                } else ESP_LOGW("power", "display transition failed: %s", esp_err_to_name(error));
            }
            save = controller->scale().take_pending_save();
            if (now_ms - last_diagnostic_ms >= 5000) {
                const auto& scale = controller->scale();
                const auto stats = scale.stability_stats();
                const auto calibration = scale.calibration();
                const double factor = calibration ? calibration->counts_per_gram : 0;
                const double units = calibration ? std::abs(factor) : 1;
                const double hz = (scale.received() - previous_received) * 1000.0 / (now_ms - last_diagnostic_ms);
                char failed[64]; smart_scale::failed_names(stats.failed, failed);
                std::snprintf(diagnostic, sizeof(diagnostic),
                    "now_ms=%llu sample_ms=%llu seq=%llu acquisition_seq=%llu failed=%s ready=%d stable=%d quiet=%d fail=%u hold_ms=%llu n=%u span_ms=%llu drift_n=%u drift_span_ms=%llu sd=%.4f/%.4f drift=%.4f/%.4f residual=%.4f p2p=%.4f units=%s hz=%.1f age_ms=%llu max_age_ms=%llu batch=%u overflow=%u sample_dropped_estimate=%u acquisition_hwm=%u log_dropped=%u log_hwm=%u trace_truncated=%u trace_missed_samples=%u faults=%llu factor=%.4f zero=%.1f profiles=%u object=%s",
                    static_cast<unsigned long long>(now_ms), static_cast<unsigned long long>(scale.sample_ms()),
                    static_cast<unsigned long long>(scale.received()), static_cast<unsigned long long>(acquisition_seq), failed,
                    scale.ready(), scale.stable(), scale.quiet(), stats.failed,
                    static_cast<unsigned long long>(scale.stable_for_ms()), stats.samples,
                    static_cast<unsigned long long>(stats.span_ms), stats.drift_samples,
                    static_cast<unsigned long long>(stats.drift_span_ms), stats.std_counts / units, stats.std_limit / units,
                    stats.drift_counts_s / (calibration ? factor : 1), stats.drift_limit / units, stats.residual_counts / units,
                    stats.peak_to_peak_counts / units, calibration ? "g,g/s" : "counts,counts/s", hz,
                    static_cast<unsigned long long>(scale.last_raw() && now_ms > scale.sample_ms() ? now_ms - scale.sample_ms() : 0),
                    static_cast<unsigned long long>(max_age_ms), max_batch, acquisition.overflows(), acquisition.dropped(), acquisition.high_water(),
                    logger.dropped(), logger.high_water(), diagnostics.trace.truncated(), diagnostics.trace.missed_samples(),
                    static_cast<unsigned long long>(scale.faults()), factor, calibration ? calibration->zero : 0,
                    static_cast<unsigned>(controller->equipment_count()), controller->recognition_status());
                last_diagnostic_ms = now_ms; previous_received = scale.received(); max_age_ms = 0; max_batch = 0;
            }
            // Save requests and dialog/brew operations must finish before sleep.
            if (power.sleep_due(now_ms) && !save && applied_power == smart_scale::PowerState::screen_off &&
                !display.touch_down() && power.report_sleep_once()) {
                if (!display.sleep_supported()) {
                    ESP_LOGW("power", "sleep blocked: touch wake wiring not verified; remaining screen-off");
                } else {
                    const bool stopped = acquisition.suspend();
                    uint64_t sleep_start{}, sleep_end{};
                    const auto error = stopped ? display.light_sleep(sleep_start, sleep_end) : ESP_ERR_TIMEOUT;
                    const auto resumed_s = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
                    if (error == ESP_OK) {
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
                    acquisition.resume(); // also cancel a timed-out suspend request
                    controller->resume_measurement();
                    const auto resumed = esp_timer_get_time();
                    power.activity(resumed / 1000);
                    previous = resumed; // exclude sleep time from session clocks
                    last_diagnostic_ms = resumed / 1000;
                    previous_received = controller->scale().received();
                    battery_sampled = false;
                    ESP_LOGI("power", "sleep result=%s wake_cause=%d", esp_err_to_name(error), static_cast<int>(esp_sleep_get_wakeup_cause()));
                    view.update();
                    const auto shown = display.power_state(smart_scale::PowerState::active);
                    if (shown == ESP_OK) applied_power = smart_scale::PowerState::active;
                }
            }
            lvgl_port_unlock();
        }
        if (diagnostic[0]) logger.text("hx711_diag", diagnostic);
        diagnostics.trace.poll(esp_timer_get_time() / 1000);
        if (diagnostics.trace.frozen()) {
            if (started_trace != diagnostics.trace.id()) {
                char header[256];
                std::snprintf(header, sizeof(header), "event=trace_begin id=%u trigger_ms=%llu reason=%s truncated_total=%u",
                    diagnostics.trace.id(), static_cast<unsigned long long>(diagnostics.trace.trigger_ms()),
                    diagnostics.trace.trigger_reason(), diagnostics.trace.truncated());
                if (logger.text("scale_trace", header)) started_trace = diagnostics.trace.id();
            }
            if (started_trace == diagnostics.trace.id()) {
                for (unsigned i = 0; i < 16 && diagnostics.trace.front(); ++i) {
                    if (!logger.sample("trace_sample", *diagnostics.trace.front(), diagnostics.trace.id(), true)) break;
                    diagnostics.trace.pop();
                }
                if (!diagnostics.trace.front()) {
                    char footer[128];
                    std::snprintf(footer, sizeof(footer), "event=trace_end id=%u log_dropped=%u", diagnostics.trace.id(), logger.dropped());
                    if (logger.text("scale_trace", footer)) diagnostics.trace.finish();
                }
            }
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
