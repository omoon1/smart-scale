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
    void activity(std::uint64_t now) { activity_ms_ = now; state_ = PowerState::active; sleep_reported_ = false; retry_ms_ = 0; }
    void weight(std::optional<float> grams, std::uint64_t now) {
        if (!grams || !std::isfinite(*grams)) { anchor_.reset(); candidate_.reset(); return; }
        if (!anchor_) { anchor_ = grams; return; }
        if (std::abs(*grams - *anchor_) < 0.5F) { candidate_.reset(); return; }
        if (!candidate_) candidate_ = now;
        if (now - *candidate_ >= 300) { anchor_ = grams; candidate_.reset(); activity(now); }
    }
    PowerState update(std::uint64_t now, bool inhibit, bool external_power = false) {
        // Keep dialogs and brewing bright; release starts a fresh idle period.
        // Plug/unplug starts a fresh interval; plugged-in idle may only dim.
        if (inhibit || inhibited_ || external_power != external_power_) activity(now);
        inhibited_ = inhibit;
        external_power_ = external_power;
        const auto idle = now - activity_ms_;
        state_ = idle >= 60000 && !external_power_ ? PowerState::screen_off : idle >= 30000 ? PowerState::dimmed : PowerState::active;
        return state_;
    }
    bool sleep_due(std::uint64_t now) const { return !inhibited_ && !external_power_ && now >= retry_ms_ && now - activity_ms_ >= 60000; }
    // Keep the display dark and bound retries when a shared IRQ is noisy/stuck.
    void retry_sleep(std::uint64_t now) { retry_ms_ = now + 30000; sleep_reported_ = false; }
    bool report_sleep_once() { if (sleep_reported_) return false; sleep_reported_ = true; return true; }
    PowerState state() const { return state_; }
private:
    std::uint64_t activity_ms_{}, retry_ms_{};
    std::optional<float> anchor_;
    std::optional<std::uint64_t> candidate_;
    PowerState state_{PowerState::active};
    bool inhibited_{}, sleep_reported_{}, external_power_{};
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
