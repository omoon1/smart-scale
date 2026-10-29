#include "acquisition.hxx"
#include "esp_timer.h"

namespace smart_scale::device::scale {
esp_err_t Acquisition::start() {
    if (task_) return ESP_ERR_INVALID_STATE;
    auto error = sensor_.init();
    if (error != ESP_OK) return error;
    queue_ = xQueueCreate(capacity, sizeof(Reading));
    if (!queue_) return ESP_ERR_NO_MEM;
    // Priority above the UI/main loop; no ISR or task waits while SCK is high.
    if (xTaskCreate(run, "hx711", 3072, this, 5, &task_) != pdPASS) {
        vQueueDelete(queue_); queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void Acquisition::run(void* context) {
    auto& self = *static_cast<Acquisition*>(context);
    std::uint64_t last = 0, seq = 0;
    while (true) {
        if (const auto raw = self.sensor_.read()) {
            const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
            Reading reading{*raw, now, last != 0 && now - last > config::sensor_timeout_ms, ++seq};
            last = now;
            if (xQueueSend(self.queue_, &reading, 0) != pdTRUE) {
                // A stalled consumer must not silently lose history or replay stale data.
                self.overflows_.fetch_add(1, std::memory_order_relaxed);
                self.dropped_.fetch_add(uxQueueMessagesWaiting(self.queue_), std::memory_order_relaxed);
                xQueueReset(self.queue_);
                reading.gap = true;
                if (xQueueSend(self.queue_, &reading, 0) != pdTRUE)
                    self.dropped_.fetch_add(1, std::memory_order_relaxed);
            }
            const auto depth = static_cast<unsigned>(uxQueueMessagesWaiting(self.queue_));
            if (depth > self.high_water_.load(std::memory_order_relaxed)) self.high_water_.store(depth, std::memory_order_relaxed);
        }
        // Readiness is checked every RTOS tick (1 ms in this project). No busy wait;
        // the controller's timestamp timeout handles unplugged or stuck sensors.
        vTaskDelay(1);
    }
}
}
