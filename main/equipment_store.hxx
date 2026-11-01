#pragma once
#include "session.hxx"
#include "esp_err.h"

namespace smart_scale::equipment_storage {
// NVS initialization is shared with calibration; missing data is an empty list.
esp_err_t load(std::vector<Equipment>& items);
esp_err_t save(const std::vector<Equipment>& items);
}
