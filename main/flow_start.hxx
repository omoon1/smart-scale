#pragma once
#include "config.hxx"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace smart_scale {
struct FlowStart { float baseline_g{}; std::uint64_t started_ms{}; };
// Fixed-capacity, sample-time driven detector. A single step is never flow.
class FlowStartDetector {
public:
    void reset() { *this = {}; }
    std::optional<FlowStart> update(float recent, float mean, std::uint64_t now, bool stable) {
        if (!std::isfinite(recent) || !std::isfinite(mean)) { reset(); return {}; }
        if (last_ms_ && now == *last_ms_) return {};
        if (last_ms_ && now < *last_ms_) { reset(); return {}; }
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
        const float delta = average - points_[size_ - 1].grams;
        if (delta < -config::auto_flow_jump_g || delta > config::auto_flow_max_bucket_gain_g) {
            reset(); return {}; // Removal or out-of-envelope movement needs a stable rearm.
        }
        if (size_ == points_.size()) {
            for (unsigned i = 1; i < size_; ++i) points_[i - 1] = points_[i];
            --size_;
        }
        points_[size_++] = {average, now};
        // Evaluate suffixes: pauses/old disturbances must not poison later pours.
        for (unsigned first = size_ - 1; first > 0;) {
            --first;
            const auto duration = now - points_[first].ms;
            if (duration < config::auto_flow_confirm_ms) continue;
            if (duration > config::auto_flow_history_ms) break;
            const bool fast = qualifies(first, false);
            if (fast || qualifies(first, true)) {
                const float step = fast ? config::auto_flow_step_g : config::auto_flow_slow_step_g;
                unsigned onset = first + 1;
                while (onset + 1 < size_ && points_[onset].grams - points_[onset - 1].grams < step) ++onset;
                return FlowStart{baseline_, points_[onset].ms};
            }
        }
        // Only rebase a quiet full history; a slow pour must retain its baseline.
        if (stable && size_ == points_.size()) {
            float low = average, high = average;
            for (unsigned i = 0; i < size_; ++i) {
                low = std::min(low, points_[i].grams); high = std::max(high, points_[i].grams);
            }
            if (high - low < .05F) baseline_ = mean;
        }
        return {};
    }
private:
    bool qualifies(unsigned first, bool slow) const {
        const auto last = size_ - 1;
        const auto duration = points_[last].ms - points_[first].ms;
        const float gain = points_[last].grams - points_[first].grams;
        const float step = slow ? config::auto_flow_slow_step_g : config::auto_flow_step_g;
        if (slow) {
            if (duration < config::auto_flow_slow_min_ms || gain < config::auto_flow_slow_gain_g ||
                gain * 1000 / duration < config::auto_flow_slow_rate_g_s) return false;
        } else if (gain < config::auto_flow_gain_g) return false;
        unsigned rises = 0;
        float backwards = 0;
        std::optional<std::uint64_t> first_rise, last_rise;
        for (unsigned i = first + 1; i <= last; ++i) {
            const float delta = points_[i].grams - points_[i - 1].grams;
            if (delta < -config::auto_flow_backstep_g) return false;
            if (delta < 0) backwards -= delta;
            if (delta >= step) {
                if (!slow && last_rise && points_[i].ms - *last_rise >= 400) return false;
                ++rises;
                if (!first_rise) first_rise = points_[i].ms;
                last_rise = points_[i].ms;
            }
        }
        // Distributed rises reject a single placement, ringing and oscillation.
        if (rises < 4 || !first_rise || *last_rise - *first_rise < config::auto_flow_confirm_ms ||
            backwards > std::max(.08F, gain * .25F)) return false;
        if (slow) {
            return *first_rise - points_[first].ms <= duration / 3 &&
                   points_[last].ms - *last_rise <= config::auto_flow_bucket_ms &&
                   *last_rise - *first_rise >= duration * 2 / 3;
        }
        return points_[first + 1].grams - points_[first].grams >= step &&
               points_[last].grams - points_[last - 1].grams >= step;
    }
    struct Point { float grams{}; std::uint64_t ms{}; };
    std::array<Point, config::auto_flow_history_ms / config::auto_flow_bucket_ms + 1> points_{};
    unsigned size_{}, samples_{};
    double sum_{};
    float baseline_{};
    bool armed_{};
    std::uint64_t bucket_ms_{};
    std::optional<std::uint64_t> last_ms_;
};
}
