#pragma once
#include "battery_history.hxx"
#include "nvs.h"
#include "esp_log.h"
namespace smart_scale::device {
class BatteryHistoryStore {
public:
    ~BatteryHistoryStore() { if (open_) nvs_close(handle_); }
    void init(BatteryHistory& history) {
        auto err = nvs_open("battery_usage", NVS_READWRITE, &handle_);
        open_ = err == ESP_OK;
        if (!open_) { ESP_LOGW("battery", "history storage unavailable: %s", esp_err_to_name(err)); return; }
        BatteryHistoryData data{};
        size_t length = sizeof(data);
        err = nvs_get_blob(handle_, "history", &data, &length);
        if (err == ESP_ERR_NVS_NOT_FOUND) return;
        if (err != ESP_OK || length != sizeof(data) || !history.restore(data))
            ESP_LOGW("battery", "history checkpoint invalid; starting fresh");
    }
    bool checkpoint(BatteryHistory& history, uint64_t now_s, bool event = false, bool before_sleep = false) {
        if (!open_ || !history.dirty()) return false;
        // Coalesce power-source chatter; retry failures no faster than once a minute.
        pending_event_ |= event;
        if (attempted_ && (now_s - attempted_s_ < (before_sleep ? 1 : (pending_event_ || failed_) ? 60 : 900))) return false;
        attempted_ = true; attempted_s_ = now_s;
        const auto& data = history.data();
        auto err = nvs_set_blob(handle_, "history", &data, sizeof(data));
        if (err == ESP_OK) err = nvs_commit(handle_);
        failed_ = err != ESP_OK;
        if (err == ESP_OK) { history.saved(); pending_event_ = false; }
        else ESP_LOGW("battery", "history save failed: %s", esp_err_to_name(err));
        return err == ESP_OK;
    }
private:
    nvs_handle_t handle_{};
    bool open_{}, attempted_{}, pending_event_{}, failed_{};
    uint64_t attempted_s_{};
};
}
