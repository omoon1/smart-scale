#pragma once
#include "config.hxx"
#include <algorithm>
#include <cmath>
#include <optional>

namespace smart_scale {
// Presentation only: never feed held/rounded values back into measurements.
class WeightDisplay {
public:
    explicit WeightDisplay(float resolution = 0.1F, float hysteresis = config::display_hysteresis_g,
                           float zero_band = config::display_zero_deadband_g)
        : resolution_(resolution), hysteresis_(hysteresis), zero_band_(zero_band) {}
    void reset() { held_.reset(); candidate_direction_ = 0; last_ms_.reset(); }
    // Immediate legacy path (e.g. an immutable result). Timed live readouts use
    // the overload below; no view may feed these values into calibration/tare.
    std::optional<float> update(std::optional<float> grams, bool zero_deadband = true) {
        if (!grams || !std::isfinite(*grams)) { reset(); return std::nullopt; }
        float value = *grams;
        if (zero_deadband && std::abs(value) <= zero_band_) value = 0;
        if (!held_ || (zero_deadband && value == 0) ||
            std::abs(value - *held_) >= hysteresis_) {
            held_ = rounded(value);
            if (*held_ == 0) held_ = 0.0F; // Avoid a displayed negative zero.
        }
        return held_;
    }
    std::optional<float> update(std::optional<float> value, std::uint64_t sample_ms,
                                bool quiet, bool zero_deadband = true) {
        if (!value || !std::isfinite(*value)) { reset(); return std::nullopt; }
        if (last_ms_ && sample_ms < *last_ms_) reset();
        if (last_ms_ && sample_ms == *last_ms_) return held_;
        const auto elapsed = last_ms_ ? sample_ms - *last_ms_ : 0;
        if (elapsed > config::sensor_timeout_ms) reset();
        last_ms_ = sample_ms;
        if (!held_) {
            held_ = zero_deadband && std::abs(*value) <= zero_band_ ? 0 : rounded(*value);
            reference_ = average_ = *value;
            was_quiet_ = quiet;
            return held_;
        }
        average_ += static_cast<float>(-std::expm1(-static_cast<double>(elapsed) / 1000 /
                     config::display_average_tau_s)) * (*value - average_);
        if (quiet != was_quiet_) candidate_direction_ = 0;
        was_quiet_ = quiet;
        const float delta = std::abs(*value - *held_);
        if ((!quiet && delta >= hysteresis_) || delta >= std::max(10 * resolution_, 7 * hysteresis_)) {
            held_ = rounded(*value);
            reference_ = *value;
            candidate_direction_ = 0;
            return held_;
        }
        // Quiet changes need both amplitude and time evidence from an averaged
        // measurement. Compare against the last accepted UNROUNDED reference:
        // 100.04 <-> 100.06 is not a physical 0.1 g change.
        const float evidence = quiet ? average_ : *value;
        const float proposed = rounded(evidence);
        const float change = evidence - reference_;
        const float amplitude = resolution_ * (quiet ? 0.8F : 0.6F);
        // Separate up/down boundaries around the currently displayed digit.
        const float boundary = resolution_ * 0.7F;
        const bool beyond_boundary = std::abs(evidence - *held_) >= boundary ||
                                     std::abs(change) >= resolution_ * 0.95F;
        const int direction = change >= amplitude ? 1 : change <= -amplitude ? -1 : 0;
        if (!direction || !beyond_boundary || proposed == *held_) {
            candidate_direction_ = 0;
        } else if (candidate_direction_ != direction) {
            candidate_direction_ = direction;
            candidate_since_ = sample_ms;
        } else if (sample_ms - candidate_since_ >=
                   (quiet ? config::display_quiet_confirm_ms : config::display_confirm_ms)) {
            held_ = proposed;
            reference_ = evidence;
            candidate_direction_ = 0;
        }
        return held_;
    }
private:
    float rounded(float value) const {
        const float result = std::round(value / resolution_) * resolution_;
        return result == 0 ? 0.0F : result;
    }
    float resolution_, hysteresis_, zero_band_;
    std::optional<float> held_;
    float reference_{}, average_{};
    int candidate_direction_{};
    bool was_quiet_{};
    std::uint64_t candidate_since_{};
    std::optional<std::uint64_t> last_ms_;
};
}
