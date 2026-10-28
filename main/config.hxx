#pragma once
#include <cstddef>
#include <cstdint>

namespace smart_scale::config {
inline constexpr int screen_width = 480;
inline constexpr int screen_height = 320;
inline constexpr std::uint32_t ui_period_ms = 100;
inline constexpr std::size_t max_samples = 601;
inline constexpr std::size_t max_equipment = 8;
inline constexpr std::size_t max_records = 8;
inline constexpr int dose_min_tenths = 1;
inline constexpr int dose_max_tenths = 600;
inline constexpr float object_tolerance_g = 0.5F; // Demo assumption, not calibrated.
inline constexpr float combination_tolerance_g = 3.0F;
inline constexpr int lcd_mosi = 1, lcd_clock = 5, lcd_dc = 3, lcd_backlight = 6;
inline constexpr int i2c_sda = 8, i2c_scl = 7;
inline constexpr int reset_button = 0; // BOOT, not the hardware EN/reset switch.
}
