#include "hx711.hxx"
#include "config.hxx"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace smart_scale::device::scale {
namespace { portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; }
esp_err_t HX711::init() {
    dout_ = static_cast<gpio_num_t>(config::hx711_dout);
    clock_ = static_cast<gpio_num_t>(config::hx711_sck);
    // Pull-up makes a missing sensor report "not ready", not a fake zero.
    gpio_config_t input{};
    input.pin_bit_mask = 1ULL << dout_;
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_ENABLE;
    auto err = gpio_config(&input);
    if (err != ESP_OK) return err;
    gpio_set_level(clock_, 0);
    gpio_config_t output{};
    output.pin_bit_mask = 1ULL << clock_;
    output.mode = GPIO_MODE_OUTPUT;
    err = gpio_config(&output);
    if (err != ESP_OK) return err;
    // Reset any previous channel/gain selection; startup settling is handled by polling.
    gpio_set_level(clock_, 1);
    esp_rom_delay_us(80);
    gpio_set_level(clock_, 0);
    return ESP_OK;
}
void HX711::power_down() {
    gpio_set_level(clock_, 1);
    esp_rom_delay_us(80);
}
void HX711::power_up() { gpio_set_level(clock_, 0); }
std::optional<std::int32_t> HX711::read() {
    if (gpio_get_level(dout_)) return std::nullopt;
    std::uint32_t bits = 0;
    // Never yield with PD_SCK high: >60 us would power down the ADC.
    // Only the ~100 us shift is critical, never the conversion-ready wait.
    portENTER_CRITICAL(&mux);
    for (unsigned i = 0; i < 24; ++i) {
        gpio_set_level(clock_, 1);
        esp_rom_delay_us(1);
        bits = (bits << 1) | gpio_get_level(dout_);
        gpio_set_level(clock_, 0);
        esp_rom_delay_us(1);
    }
    gpio_set_level(clock_, 1); // 25th pulse selects A / 128 for next conversion.
    esp_rom_delay_us(1);
    gpio_set_level(clock_, 0);
    esp_rom_delay_us(1);
    const bool released = gpio_get_level(dout_) != 0;
    portEXIT_CRITICAL(&mux);
    if (!released) return std::nullopt; // DOUT shorted low / invalid transaction.
    return signed_hx711(bits);
}
esp_err_t init_storage() {
    // Do not erase an entire NVS partition to recover calibration storage.
    return nvs_flash_init();
}
std::optional<Calibration> load_calibration() {
    nvs_handle_t handle;
    if (nvs_open("hx711", NVS_READONLY, &handle) != ESP_OK) return std::nullopt;
    struct Data { std::uint32_t version; Calibration calibration; } data{};
    std::size_t size = sizeof(data);
    const auto err = nvs_get_blob(handle, "calibration", &data, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(data) || data.version != 1 || !valid_calibration(data.calibration)) return std::nullopt;
    return data.calibration;
}
esp_err_t save_calibration(Calibration calibration) {
    if (!valid_calibration(calibration)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    auto err = nvs_open("hx711", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    struct Data { std::uint32_t version; Calibration calibration; } data{1, calibration};
    err = nvs_set_blob(handle, "calibration", &data, sizeof(data));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
}
