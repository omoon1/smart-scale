#pragma once
#include "config.hxx"
#include <array>
#include <cmath>
#include <optional>

namespace smart_scale {
struct FlowStart { float baseline_g{}; std::uint64_t started_ms{}; };
// Detect a sustained mass trend, never infer liquid from a single positive spike.
class FlowStartDetector {
public:
    void reset() { *this = {}; }
    std::optional<FlowStart> update(float recent, float mean, std::uint64_t now, bool stable) {
        if (!std::isfinite(recent) || !std::isfinite(mean)) { reset(); return {}; }
        if (last_ms_ && now <= *last_ms_) return {};
        if (last_ms_ && now - *last_ms_ > config::sensor_timeout_ms) reset();
        last_ms_ = now;
        if (!armed_) {
            if (!stable || recent <= 2) return {};
            armed_ = true; baseline_ = mean; bucket_ms_ = now;
            points_[0] = {mean, now}; size_ = 1;
            return {};
        }
        sum_ += recent; ++samples_;
        if (now - bucket_ms_ < config::auto_flow_bucket_ms) return {};
        const float average = static_cast<float>(sum_ / samples_);
        sum_ = 0; samples_ = 0; bucket_ms_ = now;
        if (std::abs(average - points_[size_ - 1].grams) > config::auto_flow_jump_g) {
            reset(); return {}; // Placement/removal: require a fresh stable load.
        }
        const float delta = average - points_[size_ - 1].grams;
        if (delta >= config::auto_flow_step_g) {
            if (!rising_since_) rising_since_ = now;
            last_rise_ms_ = now;
        } else if (delta < -config::auto_flow_backstep_g || now - last_rise_ms_ >= 400)
            rising_since_.reset();
        if (size_ == points_.size()) {
            for (unsigned i = 1; i < size_; ++i) points_[i - 1] = points_[i];
            --size_;
        }
        points_[size_++] = {average, now};
        if (size_ < points_.size()) return {};
        unsigned rising = 0;
        bool backwards = false;
        for (unsigned i = 1; i < size_; ++i) {
            const auto delta = points_[i].grams - points_[i - 1].grams;
            rising += delta >= config::auto_flow_step_g;
            backwards |= delta < -config::auto_flow_backstep_g;
        }
        const bool edges_rise = points_[1].grams - points_[0].grams >= config::auto_flow_step_g &&
                                points_[5].grams - points_[4].grams >= config::auto_flow_step_g;
        if (rising_since_ && now - *rising_since_ >= config::auto_flow_confirm_ms &&
            points_[5].ms - points_[0].ms >= config::auto_flow_confirm_ms &&
            points_[5].grams - points_[0].grams >= config::auto_flow_gain_g &&
            rising >= 4 && edges_rise && !backwards)
            return FlowStart{baseline_, *rising_since_};
        // Rebase only a stationary window, never continuously tare a pour.
        if (stable && rising == 0 && std::abs(points_[5].grams - points_[0].grams) < .05F)
            baseline_ = mean;
        return {};
    }
private:
    struct Point { float grams{}; std::uint64_t ms{}; };
    std::array<Point, 6> points_{};
    unsigned size_{}, samples_{};
    double sum_{};
    float baseline_{};
    bool armed_{};
    std::uint64_t bucket_ms_{};
    std::uint64_t last_rise_ms_{};
    std::optional<std::uint64_t> last_ms_;
    std::optional<std::uint64_t> rising_since_;
};
}
