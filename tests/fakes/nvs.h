#pragma once
#include <cstdint>
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, NVS_READONLY = 0, NVS_READWRITE = 1;
int nvs_open(const char*, int, nvs_handle_t*);
int nvs_get_u8(nvs_handle_t, const char*, std::uint8_t*);
int nvs_set_u8(nvs_handle_t, const char*, std::uint8_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
