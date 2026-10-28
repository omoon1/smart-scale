#include "config.hxx"
#include "device.hxx"
#include "session.hxx"
#include "ui.hxx"
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
        new (storage) smart_scale::Controller, release);
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
        if (lvgl_port_lock(0)) {
            if (display.reset_pressed()) view.reset();
            controller->tick(elapsed);
            view.update();
            lvgl_port_unlock();
        }
    }
}
