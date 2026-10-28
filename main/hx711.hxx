#pragma once
#include "scale.hxx"
#include "driver/gpio.h"
#include <optional>

namespace smart_scale::device::scale {
// Adapted from the user's smart_scale.bak/main/hx711.hxx. Channel A, gain 128.
class HX711 {
public:
    esp_err_t init();
    std::optional<std::int32_t> read(); // Nonblocking when DOUT is not ready.
private:
    gpio_num_t dout_{GPIO_NUM_17}, clock_{GPIO_NUM_18};
};
esp_err_t init_storage();
std::optional<Calibration> load_calibration();
esp_err_t save_calibration(Calibration calibration);
}
