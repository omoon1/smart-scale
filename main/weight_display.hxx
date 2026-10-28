#pragma once
#include "config.hxx"
#include <cmath>
#include <optional>

namespace smart_scale {
// Presentation only: never feed held/rounded values back into measurements.
class WeightDisplay {
public:
    void reset() { held_.reset(); }
    std::optional<float> update(std::optional<float> grams, bool zero_deadband = true) {
        if (!grams || !std::isfinite(*grams)) { reset(); return std::nullopt; }
        float value = *grams;
        if (zero_deadband && std::abs(value) <= config::display_zero_deadband_g) value = 0;
        if (!held_ || (zero_deadband && value == 0) ||
            std::abs(value - *held_) >= config::display_hysteresis_g) {
            held_ = std::round(value * 10.0F) / 10.0F;
            if (*held_ == 0) held_ = 0.0F; // Avoid a displayed negative zero.
        }
        return held_;
    }
private:
    std::optional<float> held_;
};
}
