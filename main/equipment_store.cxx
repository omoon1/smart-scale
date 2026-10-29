#include "equipment_store.hxx"
#include "equipment_codec.hxx"
#include "nvs.h"

namespace smart_scale::equipment_storage {
esp_err_t load(std::vector<Equipment>& items) {
    nvs_handle_t handle;
    auto error = nvs_open("equipment", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) { items.clear(); return ESP_OK; }
    if (error != ESP_OK) return error;
    std::size_t size = 0;
    error = nvs_get_blob(handle, "profiles", nullptr, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); items.clear(); return ESP_OK; }
    if (error != ESP_OK || size > config::max_equipment * record_bytes || size % record_bytes) {
        nvs_close(handle);
        return error == ESP_OK ? ESP_ERR_INVALID_SIZE : error;
    }
    std::vector<std::uint8_t> bytes(size);
    if (size) error = nvs_get_blob(handle, "profiles", bytes.data(), &size);
    nvs_close(handle);
    if (error != ESP_OK) return error;
    const auto decoded = decode(bytes);
    if (!decoded) return ESP_ERR_INVALID_ARG;
    items = *decoded;
    return ESP_OK;
}
esp_err_t save(const std::vector<Equipment>& items) {
    const auto bytes = encode(items);
    if (!bytes) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    auto error = nvs_open("equipment", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    if (bytes->empty()) {
        error = nvs_erase_key(handle, "profiles");
        if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    } else error = nvs_set_blob(handle, "profiles", bytes->data(), bytes->size());
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}
}
