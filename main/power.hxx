#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace smart_scale {
enum class PowerState { active, dimmed, screen_off };
// Pure policy. All calls are serialized under the LVGL lock.
class PowerPolicy {
public:
    explicit PowerPolicy(std::uint64_t now = 0) : activity_ms_(now) {}
    void activity(std::uint64_t now) { activity_ms_ = now; state_ = PowerState::active; sleep_reported_ = false; }
    void weight(std::optional<float> grams, std::uint64_t now) {
        if (!grams || !std::isfinite(*grams)) { anchor_.reset(); candidate_.reset(); return; }
        if (!anchor_) { anchor_ = grams; return; }
        if (std::abs(*grams - *anchor_) < 0.5F) { candidate_.reset(); return; }
        if (!candidate_) candidate_ = now;
        if (now - *candidate_ >= 300) { anchor_ = grams; candidate_.reset(); activity(now); }
    }
    PowerState update(std::uint64_t now, bool inhibit) {
        // Keep dialogs and brewing bright; release starts a fresh idle period.
        if (inhibit || inhibited_) activity(now);
        inhibited_ = inhibit;
        const auto idle = now - activity_ms_;
        state_ = idle >= 60000 ? PowerState::screen_off : idle >= 30000 ? PowerState::dimmed : PowerState::active;
        return state_;
    }
    bool sleep_due(std::uint64_t now) const { return !inhibited_ && now - activity_ms_ >= 360000; }
    bool report_sleep_once() { if (sleep_reported_) return false; sleep_reported_ = true; return true; }
    PowerState state() const { return state_; }
private:
    std::uint64_t activity_ms_{};
    std::optional<float> anchor_;
    std::optional<std::uint64_t> candidate_;
    PowerState state_{PowerState::active};
    bool inhibited_{}, sleep_reported_{};
};

// First press on a dark display is consumed through release, even after wake.
class WakeTouchGate {
public:
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
