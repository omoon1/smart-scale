#pragma once
#include "config.hxx"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace smart_scale {
// Counts in/out. Motion is detected before the quiet filter, against a fixed
// reference so gradual additions cannot disappear into a moving baseline.
class AdaptiveFilter {
public:
    void reset() { *this = {}; }
    bool quiet() const { return quiet_; }
    double update(double sample, double dt, std::uint64_t now, double counts_per_gram = 0,
                  double noise_counts = 0) {
        if (!value_) {
            value_ = detector_ = reference_ = sample;
            last_motion_ = now;
            return sample;
        }
        // Use the unsmoothed window's noise, bounded so a real step cannot
        // indefinitely inflate its own detection threshold.
        const double base = counts_per_gram > 0 ? std::max(2.0, counts_per_gram * config::motion_threshold_g) : 300.0;
        const double ceiling = counts_per_gram > 0 ? std::max(base, counts_per_gram * 0.6) : 1500.0;
        const double threshold = std::max(base, std::min(3 * noise_counts, ceiling));
        const double settled = counts_per_gram > 0 ? std::max(2.0, counts_per_gram * 0.05) : 150.0;
        detector_ += -std::expm1(-dt / config::motion_detector_tau_s) * (sample - detector_);
        const double delta = detector_ - reference_;
        const int direction = delta > threshold ? 1 : delta < -threshold ? -1 : 0;
        if (!direction) candidate_since_.reset();
        else {
            if (!candidate_since_ || direction != candidate_direction_) {
                candidate_since_ = now;
                candidate_direction_ = direction;
            }
            if (now - *candidate_since_ >= config::motion_confirm_ms) {
                last_motion_ = now;
                reference_ = detector_;
                candidate_since_.reset();
                quiet_ = false;
            }
        }
        // Do not enter quiet mode while the fast output is still catching up.
        if (!candidate_since_ && now - last_motion_ >= config::filter_quiet_after_ms &&
            std::abs(detector_ - *value_) <= settled) quiet_ = true;
        const double target_tau = quiet_ ? config::filter_quiet_tau_s : config::filter_fast_tau_s;
        if (!quiet_) tau_ = target_tau;
        else tau_ += -std::expm1(-dt / config::filter_transition_tau_s) * (target_tau - tau_);
        *value_ += -std::expm1(-dt / tau_) * (sample - *value_);
        return *value_;
    }
private:
    std::optional<double> value_;
    double detector_{}, reference_{}, tau_{config::filter_fast_tau_s};
    std::uint64_t last_motion_{};
    std::optional<std::uint64_t> candidate_since_;
    int candidate_direction_{};
    bool quiet_{};
};
}
