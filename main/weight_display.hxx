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
    void reset() { held_.reset(); candidate_.reset(); last_ms_.reset(); }
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
        last_ms_ = sample_ms;
        const float proposed = rounded(*value);
        if (!held_) {
            held_ = zero_deadband && std::abs(*value) <= zero_band_ ? 0 : proposed;
            return held_;
        }
        if (proposed == *held_) { candidate_.reset(); return held_; }
        // Large changes / moving measurements stay responsive. At rest, demand
        // a persistent rounded candidate instead of following every excursion.
        const float delta = std::abs(*value - *held_);
        if ((!quiet && delta >= hysteresis_) || delta >= std::max(10 * resolution_, 7 * hysteresis_)) {
            held_ = proposed;
            candidate_.reset();
        } else {
            if (!candidate_ || *candidate_ != proposed) {
                candidate_ = proposed;
                candidate_since_ = sample_ms;
            } else if (sample_ms - candidate_since_ >= config::display_confirm_ms) {
                // A sustained 0.1 g addition can escape even the zero deadband.
                held_ = proposed;
                candidate_.reset();
            }
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
    std::optional<float> candidate_;
    std::uint64_t candidate_since_{};
    std::optional<std::uint64_t> last_ms_;
};
}
