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
inline constexpr float object_tolerance_g = 40.0F; // Identity only, not a dose or tare tolerance.
inline constexpr float filter_tau_s = 0.3F;
inline constexpr std::uint64_t sensor_timeout_ms = 1000;
inline constexpr std::uint64_t stability_window_ms = 1200;
inline constexpr std::uint64_t stability_min_ms = 1000;
inline constexpr float stability_std_g = 0.10F, stability_drift_g_s = 0.05F;
inline constexpr float display_hysteresis_g = 0.15F;
inline constexpr float display_zero_deadband_g = 0.15F;
inline constexpr int lcd_mosi = 1, lcd_clock = 5, lcd_dc = 3, lcd_backlight = 6;
inline constexpr int i2c_sda = 8, i2c_scl = 7;
// Camera must remain disconnected: GPIO17/18 share its VSYNC/HREF.
inline constexpr int hx711_dout = 17, hx711_sck = 18;
inline constexpr int reset_button = 0; // BOOT, not the hardware EN/reset switch.
}
