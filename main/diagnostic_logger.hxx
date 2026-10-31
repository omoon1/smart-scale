#pragma once
#include "scale_diagnostics.hxx"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <atomic>
#include <cstdio>
#include <cstdarg>

namespace smart_scale::device {
// Controller/UI producers, one low-priority consumer. No serial work under
// the LVGL lock and no formatting, allocation or waits in sensor acquisition.
class DiagnosticLogger {
public:
    static DiagnosticLogger& instance() { static DiagnosticLogger value; return value; }
    void start() {
        queue_ = xQueueCreate(32, sizeof(Record));
        ESP_ERROR_CHECK(queue_ ? ESP_OK : ESP_ERR_NO_MEM);
        ESP_ERROR_CHECK(xTaskCreate(run, "scale_log", 4096, this, 1, &task_) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    }
    bool text(const char* tag, const char* text) {
        Record r{}; r.tag = tag; std::snprintf(r.text, sizeof(r.text), "%s", text);
        return send(r);
    }
    bool event(const char* tag, const char* format, ...) __attribute__((format(printf, 3, 4))) {
        Record r{}; r.tag = tag;
        const int prefix = std::snprintf(r.text, sizeof(r.text), "t_us=%llu ",
            static_cast<unsigned long long>(esp_timer_get_time()));
        va_list args;
        va_start(args, format);
        std::vsnprintf(r.text + prefix, sizeof(r.text) - prefix, format, args);
        va_end(args);
        return send(r);
    }
    bool sample(const char* event, const DiagnosticSample& s, unsigned trace_id = 0, bool retry = false) {
        Record r{}; r.event = event; r.sample = s; r.trace_id = trace_id;
        return send(r, retry);
    }
    unsigned dropped() const { return dropped_.load(std::memory_order_relaxed); }
    unsigned high_water() const { return high_water_.load(std::memory_order_relaxed); }
private:
    struct Record { const char* tag{"scale_event"}; const char* event{}; DiagnosticSample sample{}; unsigned trace_id{}; char text[1024]{}; };
    bool send(const Record& record, bool retry = false) {
        if (xQueueSend(queue_, &record, 0) != pdTRUE) {
            if (!retry) dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const auto depth = static_cast<unsigned>(uxQueueMessagesWaiting(queue_));
        auto previous = high_water_.load(std::memory_order_relaxed);
        while (depth > previous && !high_water_.compare_exchange_weak(previous, depth, std::memory_order_relaxed)) {}
        return true;
    }
    static void run(void* context) {
        auto& self = *static_cast<DiagnosticLogger*>(context);
        Record r;
        while (true) if (xQueueReceive(self.queue_, &r, portMAX_DELAY) == pdTRUE) {
            if (!r.event) { ESP_LOGI(r.tag, "%s", r.text); continue; }
            const auto& s = r.sample;
            char failed[64]; failed_names(s.failed, failed);
            char raw[24] = "NA";
            if (s.raw_valid) std::snprintf(raw, sizeof(raw), "%ld", static_cast<long>(s.raw));
            ESP_LOGI("scale_event", "event=%s trace=%u seq=%llu now_ms=%llu sample_ms=%llu age_ms=%llu dt_ms=%llu raw=%s raw_g=%.4f median_g=%.4f filtered_g=%.4f net_g=%.4f tare_g=%.4f ready=%d stable=%d quiet=%d reason=%s failed=%s op_sd=%.4f half_shift=%.4f anchor_delta=%.4f drift_guard=%d sd=%.4f drift=%.4f p2p=%.4f residual=%.4f factor=%.4f zero=%.1f",
                r.event, r.trace_id, static_cast<unsigned long long>(s.seq), static_cast<unsigned long long>(s.now_ms),
                static_cast<unsigned long long>(s.sample_ms), static_cast<unsigned long long>(s.now_ms > s.sample_ms ? s.now_ms - s.sample_ms : 0),
                static_cast<unsigned long long>(s.dt_ms), raw, s.raw_g, s.median_g, s.filtered_g, s.net_g, s.tare_g,
                s.ready, s.stable, s.quiet, s.reason, failed, s.operational_sd, s.half_shift, s.anchor_delta, s.drift_guard,
                s.sd, s.drift, s.p2p, s.residual, s.factor, s.zero);
        }
    }
    QueueHandle_t queue_{};
    TaskHandle_t task_{};
    std::atomic<unsigned> dropped_{};
    std::atomic<unsigned> high_water_{};
};
}
