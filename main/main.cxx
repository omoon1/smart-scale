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
                if (reading.gap) controller->sensor_fault();
                if (now_ms > reading.ms && now_ms - reading.ms > smart_scale::config::sensor_timeout_ms) {
                    controller->sensor_fault();
                    continue;
                }
                controller->sensor_sample(reading.raw, reading.ms);
            }
            controller->sensor_sample(std::nullopt, static_cast<std::uint64_t>(esp_timer_get_time() / 1000));
            if (display.reset_pressed()) view.reset();
            controller->tick(elapsed);
            view.update();
            save = controller->scale().take_pending_save();
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
