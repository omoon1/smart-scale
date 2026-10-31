#include "theme.hxx"
#ifdef ESP_PLATFORM
#include "nvs.h"
#endif
namespace smart_scale {
Theme load_theme() {
#ifdef ESP_PLATFORM
    nvs_handle_t handle;
    if (nvs_open("appearance", NVS_READONLY, &handle) != ESP_OK) return Theme::sage_green;
    std::uint8_t value = 0;
    const auto result = nvs_get_u8(handle, "theme", &value);
    nvs_close(handle);
    if (result == ESP_OK && valid_theme(static_cast<Theme>(value))) return static_cast<Theme>(value);
#endif
    return Theme::sage_green;
}
bool save_theme(Theme theme) {
    if (!valid_theme(theme)) return false;
#ifdef ESP_PLATFORM
    nvs_handle_t handle;
    if (nvs_open("appearance", NVS_READWRITE, &handle) != ESP_OK) return false;
    auto result = nvs_set_u8(handle, "theme", static_cast<std::uint8_t>(theme));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
#else
    return true; // Desktop preview is intentionally volatile.
#endif
}
}
