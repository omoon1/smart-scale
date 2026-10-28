#pragma once
#include "hx711.hxx"
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace smart_scale::device::scale {
struct Reading { std::int32_t raw{}; std::uint64_t ms{}; bool gap{}; };
// Firmware-lifetime single producer. Never accesses Controller or LVGL.
class Acquisition {
public:
    esp_err_t start();
    bool receive(Reading& reading) { return xQueueReceive(queue_, &reading, 0) == pdTRUE; }
    unsigned overflows() const { return overflows_.load(std::memory_order_relaxed); }
    static constexpr unsigned capacity = 128;
private:
    static void run(void* context);
    std::atomic<unsigned> overflows_{};
    HX711 sensor_;
    QueueHandle_t queue_{};
    TaskHandle_t task_{};
};
}
