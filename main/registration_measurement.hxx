#pragma once
#include "scale.hxx"

namespace smart_scale {
struct RegistrationSnapshot {
    float grams{}; // Unrounded mean of a qualified short measurement window.
    std::uint64_t ms{};
};

// Both the button and its handler use the same qualified snapshot.
class RegistrationMeasurement {
public:
    void reset() { *this = {}; }
    std::optional<RegistrationSnapshot> update(const Scale& scale) {
        const auto now = scale.sample_ms();
        if ((epoch_ && *epoch_ != scale.display_epoch()) ||
            (faults_ && *faults_ != scale.faults()) || (last_ms_ && now < *last_ms_)) reset();
        epoch_ = scale.display_epoch(); faults_ = scale.faults(); last_ms_ = now;
        if (!scale.ready()) { snapshot_.reset(); bad_since_.reset(); return std::nullopt; }
        const auto recent = scale.recent_gross();
        const float mean = scale.reference_gross();
        if (snapshot_) {
            // Median rejects isolated spikes; the mean guard also catches slow
            // additions. No temporary noise grace is allowed after a load change.
            if (!recent || *recent <= 0 ||
                std::abs(*recent - snapshot_->grams) > config::registration_movement_g ||
                std::abs(mean - snapshot_->grams) > config::registration_mean_shift_g) {
                invalidate(now);
                return std::nullopt;
            }
            // Use the shared hysteretic state, not the old strict diagnostic
            // flags: ordinary +/-0.2 g noise must not disable Continue.
            const bool severe = !scale.stable();
            if (severe) {
                if (!bad_since_) bad_since_ = now;
                if (now - *bad_since_ >= config::registration_exit_ms) {
                    invalidate(now);
                    return std::nullopt;
                }
            } else bad_since_.reset();
            if (now - snapshot_->ms > config::registration_snapshot_ms) snapshot_.reset();
        }
        if (now >= blocked_until_ && scale.stable() && mean > 0) {
            snapshot_ = RegistrationSnapshot{mean, now};
            bad_since_.reset();
        }
        return snapshot_;
    }
private:
    void invalidate(std::uint64_t now) {
        snapshot_.reset(); bad_since_.reset();
        // Require a fresh measurement window before accepting another load.
        blocked_until_ = now + config::stable_window_ms;
    }
    std::optional<RegistrationSnapshot> snapshot_;
    std::optional<std::uint32_t> epoch_;
    std::optional<std::uint64_t> faults_, last_ms_, bad_since_;
    std::uint64_t blocked_until_{};
};
}
