#pragma once
#include <cstddef>
#include <cstdint>
#include "board349.hxx"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

namespace smart_scale::config {
inline constexpr int screen_width = board349::width;
inline constexpr int screen_height = board349::height;
inline constexpr std::uint32_t ui_period_ms = 100;
inline constexpr std::size_t max_samples = 601;
inline constexpr std::size_t max_equipment = 8;
inline constexpr std::size_t max_records = 8;
inline constexpr int dose_min_tenths = 1;
inline constexpr int dose_max_tenths = 600;
inline constexpr float object_tolerance_g = 40.0F; // Identity only, not a dose or tare tolerance.
inline constexpr float filter_tau_s = 0.3F;
// Flow keeps a fixed filter; adaptive weight mode changes must not create flow.
inline constexpr float filter_fast_tau_s = 0.15F, filter_quiet_tau_s = 1.2F;
inline constexpr float filter_transition_tau_s = 0.3F, motion_detector_tau_s = 0.12F;
inline constexpr float motion_threshold_g = 0.12F;
inline constexpr std::uint64_t motion_confirm_ms = 200, filter_quiet_after_ms = 1000;
inline constexpr std::uint64_t display_confirm_ms = 300;
inline constexpr std::uint64_t display_quiet_confirm_ms = 1000;
inline constexpr float display_average_tau_s = 0.25F;
inline constexpr float display_noise_decay_s = 2.0F, display_noise_multiplier = 3.0F;
// Registration uses the same noise-tolerant stability state as other screens.
inline constexpr std::uint64_t registration_snapshot_ms = 2000, registration_exit_ms = 300;
inline constexpr float registration_movement_g = 0.50F, registration_mean_shift_g = 0.50F;
// Removal is a physical loss of load, not a sub-gram beverage fluctuation.
inline constexpr float brew_removal_margin_g = 1.0F;
inline constexpr std::uint64_t brew_removal_hold_ms = 300;
inline constexpr double brew_error_return_s = 180.0;
inline constexpr std::uint64_t sensor_timeout_ms = 1000;
inline constexpr std::uint64_t stability_window_ms = 1200;
inline constexpr std::uint64_t stability_min_ms = 1000;
inline constexpr std::uint64_t drift_window_ms = 3200, drift_min_ms = 3000;
inline constexpr float stability_std_g = 0.10F, stability_drift_g_s = 0.05F;
// Operational stability: short averaged window, separate enter/exit limits.
inline constexpr std::uint64_t stable_window_ms = 600, stable_min_ms = 400;
inline constexpr unsigned stable_min_samples = 5;
inline constexpr std::uint64_t stable_enter_ms = 200, stable_exit_ms = 300;
inline constexpr float stable_noise_enter_g = 0.25F, stable_noise_exit_g = 0.40F;
inline constexpr float stable_half_shift_g = 0.20F, stable_anchor_shift_g = 0.50F;
inline constexpr float stable_hard_shift_g = 0.75F;
inline constexpr std::uint64_t auto_flow_bucket_ms = 200, auto_flow_confirm_ms = 1000;
inline constexpr float auto_flow_gain_g = 0.60F, auto_flow_step_g = 0.025F;
inline constexpr float auto_flow_backstep_g = 0.20F, auto_flow_jump_g = 3.0F;
// Slow/intermittent flow needs a longer window; do not lower the fast gate.
inline constexpr std::uint64_t auto_flow_slow_min_ms = 3000, auto_flow_history_ms = 6000;
inline constexpr float auto_flow_slow_gain_g = 0.40F, auto_flow_slow_rate_g_s = 0.08F;
inline constexpr float auto_flow_slow_step_g = 0.008F;
// Large positive steps need subsequent sustained rises, not immediate disarming.
inline constexpr float auto_flow_max_bucket_gain_g = 12.0F;
inline constexpr float display_hysteresis_g = 0.15F;
inline constexpr float display_zero_deadband_g = 0.15F;
// Header GPIO44/43 replace the old GPIO17/18 pair, now used by touch I2C.
#ifdef ESP_PLATFORM
inline constexpr int hx711_dout = CONFIG_SMART_SCALE_HX711_DOUT;
inline constexpr int hx711_sck = CONFIG_SMART_SCALE_HX711_SCK;
#else
inline constexpr int hx711_dout = board349::hx711_dout, hx711_sck = board349::hx711_sck;
#endif
constexpr bool hx711_header_pin(int pin) {
    return pin == 38 || pin == 39 || pin == 40 || pin == 41 || pin == 43 || pin == 44;
}
static_assert(hx711_header_pin(hx711_dout) && hx711_header_pin(hx711_sck), "Use an exposed HX711 header GPIO");
static_assert(hx711_dout != hx711_sck, "HX711 DOUT and SCK must differ");
#if defined(ESP_PLATFORM) && defined(CONFIG_ESP_CONSOLE_UART_DEFAULT)
static_assert(hx711_dout != 43 && hx711_dout != 44 && hx711_sck != 43 && hx711_sck != 44,
              "HX711 GPIO43/44 conflicts with UART0; select USB Serial/JTAG console");
#endif
inline constexpr int reset_button = 0; // BOOT, not the hardware EN/reset switch.
}
