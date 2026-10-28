#include "config.hxx"
#include "device.hxx"
#include "session.hxx"
#include "ui.hxx"
#include "acquisition.hxx"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <memory>
#include <cstdio>
#include <algorithm>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
    auto& display = smart_scale::device::display::device();
    display.init();
    // Session history is the largest application object; keep it out of DMA RAM.
    void* storage = heap_caps_malloc(sizeof(smart_scale::Controller), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(storage ? ESP_OK : ESP_ERR_NO_MEM);
    auto release = [](smart_scale::Controller* value) { value->~Controller(); heap_caps_free(value); };
    std::unique_ptr<smart_scale::Controller, decltype(release)> controller(
        new (storage) smart_scale::Controller(true), release);
    static smart_scale::device::scale::Acquisition acquisition;
    ESP_ERROR_CHECK(acquisition.start());
    const auto storage_status = smart_scale::device::scale::init_storage();
    if (storage_status == ESP_OK) {
        if (auto calibration = smart_scale::device::scale::load_calibration())
            controller->scale().restore(*calibration);
    } else ESP_LOGE("scale", "NVS unavailable: %s", esp_err_to_name(storage_status));
    static smart_scale::ui::View view(*controller);
    if (lvgl_port_lock(0)) {
        view.init();
        lvgl_port_unlock();
    }
    auto previous = esp_timer_get_time();
    std::uint64_t last_diagnostic_ms = previous / 1000, previous_received = 0, max_age_ms = 0;
    unsigned max_batch = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(smart_scale::config::ui_period_ms));
        const auto now = esp_timer_get_time();
        const auto elapsed = static_cast<float>(now - previous) / 1000000.0F;
        previous = now;
        std::optional<smart_scale::Calibration> save;
        char diagnostic[512]{};
        if (lvgl_port_lock(0)) {
            const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
            smart_scale::device::scale::Reading reading;
            unsigned batch = 0;
            for (unsigned i = 0; i < acquisition.capacity && acquisition.receive(reading); ++i) {
                ++batch;
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
            if (display.reset_pressed()) view.reset();
            controller->tick(elapsed);
            view.update();
            save = controller->scale().take_pending_save();
            if (now_ms - last_diagnostic_ms >= 5000) {
                const auto& scale = controller->scale();
                const auto stats = scale.stability_stats();
                const auto calibration = scale.calibration();
                const double factor = calibration ? calibration->counts_per_gram : 0;
                const double units = calibration ? std::abs(factor) : 1;
                const double hz = (scale.received() - previous_received) * 1000.0 / (now_ms - last_diagnostic_ms);
                std::snprintf(diagnostic, sizeof(diagnostic),
                    "ready=%d stable=%d fail=%u hold_ms=%llu n=%u span_ms=%llu drift_n=%u drift_span_ms=%llu sd=%.4f/%.4f drift=%.4f/%.4f residual=%.4f p2p=%.4f units=%s hz=%.1f age_ms=%llu max_age_ms=%llu batch=%u overflow=%u faults=%llu factor=%.4f zero=%.1f",
                    scale.ready(), scale.stable(), stats.failed,
                    static_cast<unsigned long long>(scale.stable_for_ms()), stats.samples,
                    static_cast<unsigned long long>(stats.span_ms), stats.drift_samples,
                    static_cast<unsigned long long>(stats.drift_span_ms), stats.std_counts / units, stats.std_limit / units,
                    stats.drift_counts_s / (calibration ? factor : 1), stats.drift_limit / units, stats.residual_counts / units,
                    stats.peak_to_peak_counts / units, calibration ? "g,g/s" : "counts,counts/s", hz,
                    static_cast<unsigned long long>(scale.last_raw() && now_ms > scale.sample_ms() ? now_ms - scale.sample_ms() : 0),
                    static_cast<unsigned long long>(max_age_ms), max_batch, acquisition.overflows(),
                    static_cast<unsigned long long>(scale.faults()), factor, calibration ? calibration->zero : 0);
                last_diagnostic_ms = now_ms; previous_received = scale.received(); max_age_ms = 0; max_batch = 0;
            }
            lvgl_port_unlock();
        }
        if (diagnostic[0]) ESP_LOGI("hx711_diag", "%s", diagnostic); // Outside LVGL lock.
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
