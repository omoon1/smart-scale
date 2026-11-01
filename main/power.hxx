#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace smart_scale {
enum class PowerState { active, dimmed, screen_off };
// Idle detection only: never changes measurement, tare or brew filtering.
struct WeightActivitySettings {
    float change_g = 8.0F; // above the full peak-to-peak span of +/-3 g noise
    float settling_span_g = 1.0F;
    std::uint64_t hold_ms = 500;
    std::uint64_t baseline_ms = 500; // time constant: slow drift follows this baseline
    std::uint64_t warmup_ms = 1000;
    std::uint64_t max_gap_ms = 500;
};
class WeightActivityDetector {
public:
    explicit WeightActivityDetector(WeightActivitySettings settings = {}) : settings_(settings) {
        if (!std::isfinite(settings.change_g) || settings.change_g <= 0 ||
            !std::isfinite(settings.settling_span_g) || settings.settling_span_g < 0 ||
            settings.settling_span_g >= settings.change_g || !settings.hold_ms ||
            !settings.baseline_ms || !settings.warmup_ms || !settings.max_gap_ms)
            settings_ = {};
    }
    void reset() { initialized_ = false; candidate_ = false; warmed_ = false; }
    // Call with fresh sensor timestamps, not repeated UI-loop timestamps.
    bool sample(std::optional<float> grams, std::uint64_t now) {
        if (!grams || !std::isfinite(*grams)) { reset(); return false; }
        if (!initialized_ || now < last_ms_ || now - last_ms_ > settings_.max_gap_ms) {
            baseline_ = low_ = high_ = *grams;
            last_ms_ = since_ms_ = now;
            initialized_ = true; warmed_ = candidate_ = false;
            return false;
        }
        if (now == last_ms_) return false;
        const auto dt = now - last_ms_;
        last_ms_ = now;
        if (!warmed_) {
            // Require a quiet initial plateau; recovery offsets are never activity.
            extend(*grams);
            if (high_ - low_ > settings_.settling_span_g) {
                low_ = high_ = *grams; since_ms_ = now;
            }
            baseline_ = *grams;
            warmed_ = now - since_ms_ >= settings_.warmup_ms;
            return false;
        }
        const float delta = *grams - baseline_;
        if (std::abs(delta) >= settings_.change_g) {
            const bool positive = delta > 0;
            if (!candidate_ || positive != positive_) {
                candidate_ = true; positive_ = positive;
                low_ = high_ = *grams; since_ms_ = now;
            }
            extend(*grams);
            if (high_ - low_ > settings_.settling_span_g) {
                low_ = high_ = *grams; since_ms_ = now;
            }
            if (now - since_ms_ >= settings_.hold_ms) {
                baseline_ = *grams; candidate_ = false;
                return true;
            }
            return false;
        }
        candidate_ = false;
        // Time-based baseline tracking prevents accumulated slow drift triggering
        // activity. Freeze it only while confirming a large, same-direction step.
        const float alpha = 1.0F - std::exp(-static_cast<float>(dt) / settings_.baseline_ms);
        baseline_ += alpha * delta;
        return false;
    }
private:
    void extend(float grams) { low_ = std::fmin(low_, grams); high_ = std::fmax(high_, grams); }
    WeightActivitySettings settings_;
    float baseline_{}, low_{}, high_{};
    std::uint64_t last_ms_{}, since_ms_{};
    bool initialized_{}, warmed_{}, candidate_{}, positive_{};
};
// Pure policy. All calls are serialized under the LVGL lock.
class PowerPolicy {
public:
    explicit PowerPolicy(std::uint64_t now = 0, WeightActivitySettings settings = {})
        : activity_ms_(now), weight_activity_(settings) {}
    void activity(std::uint64_t now) { activity_ms_ = now; state_ = PowerState::active; sleep_reported_ = false; retry_ms_ = 0; }
    void reset_weight() { weight_activity_.reset(); }
    void weight(std::optional<float> grams, std::uint64_t sample_ms) {
        if (weight_activity_.sample(grams, sample_ms) && sample_ms > activity_ms_) activity(sample_ms);
    }
    PowerState update(std::uint64_t now, bool inhibit) {
        // Keep dialogs and brewing bright; release starts a fresh idle period.
        // Power-source telemetry is deliberately not an input to idle policy.
        if (inhibit || inhibited_) activity(now);
        inhibited_ = inhibit;
        const auto idle = now - activity_ms_;
        state_ = idle >= 60000 ? PowerState::screen_off : idle >= 30000 ? PowerState::dimmed : PowerState::active;
        return state_;
    }
    bool sleep_due(std::uint64_t now) const { return !inhibited_ && now >= retry_ms_ && now - activity_ms_ >= 60000; }
    // Keep the display dark and bound retries when a shared IRQ is noisy/stuck.
    void retry_sleep(std::uint64_t now) { retry_ms_ = now + 30000; sleep_reported_ = false; }
    bool report_sleep_once() { if (sleep_reported_) return false; sleep_reported_ = true; return true; }
    std::uint64_t idle_ms(std::uint64_t now) const { return now - activity_ms_; }
    PowerState state() const { return state_; }
private:
    std::uint64_t activity_ms_{}, retry_ms_{};
    WeightActivityDetector weight_activity_;
    PowerState state_{PowerState::active};
    bool inhibited_{}, sleep_reported_{};
};

// First press on a dark display is consumed through release, even after wake.
class WakeTouchGate {
public:
    bool suppressed() const { return suppress_; }
    bool filter(bool pressed, bool dark) {
        if (pressed && dark) suppress_ = true;
        const bool deliver = pressed && !suppress_;
        if (!pressed) suppress_ = false;
        return deliver;
    }
private:
    bool suppress_{};
};
}
