#include "scale.hxx"
#include <algorithm>

namespace smart_scale {
void Scale::clear_window() {
    count_ = cursor_ = median_count_ = median_cursor_ = 0;
    filtered_.reset();
    adaptive_.reset();
    flow_filtered_.reset();
    stable_since_.reset();
    unstable_since_.reset();
    stable_latched_ = false;
    stability_motion_ms_ = 0;
    flow_g_s_ = 0;
    ++display_epoch_;
    stability_reason_ = "warming_up";
    operational_noise_ = half_shift_ = anchor_delta_ = 0;
    drift_guard_ = false;
}
void Scale::invalidate() {
    ++faults_;
    connected_ = false;
    captured_zero_.reset();
    clear_window();
    notify("sensor_invalidated");
}
float Scale::mean() const {
    double sum = 0;
    unsigned n = 0;
    for (unsigned i = 0; i < count_; ++i) {
        if (last_ms_ - samples_[i].ms <= config::stable_window_ms) { sum += samples_[i].counts; ++n; }
    }
    return n ? static_cast<float>(sum / n) : 0;
}
void Scale::ingest(std::int32_t raw, std::uint64_t now) {
    // Ignore duplicate/out-of-order timestamps. Gaps cannot reuse an old baseline.
    if (connected_ && now <= last_ms_) { notify("timestamp_rejected"); return; }
    if (connected_ && now - last_ms_ > config::sensor_timeout_ms) invalidate();
    const auto previous_ms = last_ms_;
    last_ms_ = now;
    last_raw_ = raw;
    ++received_;
    connected_ = true;
    overload_ = raw <= -8388608 || raw >= 8388607;
    if (overload_) { ++faults_; clear_window(); captured_zero_.reset(); notify("overload"); notify("sample"); return; }
    median_[median_cursor_] = raw;
    median_cursor_ = (median_cursor_ + 1) % median_.size();
    median_count_ = std::min<unsigned>(median_count_ + 1, median_.size());
    if (median_count_ < median_.size()) { notify("sample"); return; }
    auto sorted = median_;
    std::sort(sorted.begin(), sorted.end());
    const double value = sorted[1];
    // Stability uses unsmoothed, spike-rejected samples, not the low-pass output.
    samples_[cursor_] = {value, now};
    cursor_ = (cursor_ + 1) % samples_.size();
    count_ = std::min<unsigned>(count_ + 1, samples_.size());
    const double dt = static_cast<double>(now - previous_ms) / 1000;
    filtered_ = adaptive_.update(value, dt, now, calibration_ ? std::abs(calibration_->counts_per_gram) : 0,
                                stability_stats().std_counts);
    if (!flow_filtered_) flow_filtered_ = value;
    else {
        const double previous = *flow_filtered_;
        const double alpha = -std::expm1(-dt / config::filter_tau_s);
        *flow_filtered_ += alpha * (value - *flow_filtered_);
        flow_g_s_ = calibration_ ? static_cast<float>((*flow_filtered_ - previous) / calibration_->counts_per_gram / dt) : 0;
    }
    if (calibration_ && std::abs(gross()) > 10000) {
        overload_ = true;
        clear_window();
        captured_zero_.reset();
        ++faults_;
        notify("overload");
    }
    const bool was_stable = stable();
    update_stability(value, now);
    if (was_stable != stable()) notify("stability_changed");
    notify("sample");
}
void Scale::check_timeout(std::uint64_t now) {
    if (connected_ && now > last_ms_ && now - last_ms_ > config::sensor_timeout_ms) invalidate();
}
bool Scale::stable() const {
    return connected_ && filtered_ && !overload_ && stable_latched_;
}
void Scale::update_stability(double recent, std::uint64_t now) {
    if (!connected_ || overload_ || !filtered_) return;
    const double factor = calibration_ ? std::abs(calibration_->counts_per_gram) : 0;
    const auto units = [factor](double grams, double counts) {
        return factor > 0 ? std::max(2.0, factor * grams) : counts;
    };
    double sum = 0, squares = 0, older = 0, newer = 0;
    unsigned n = 0, old_n = 0, new_n = 0;
    auto oldest = now;
    for (unsigned i = 0; i < count_; ++i) {
        const auto& sample = samples_[i];
        const auto age = now - sample.ms;
        if (age > config::stable_window_ms) continue;
        oldest = std::min(oldest, sample.ms);
        sum += sample.counts; squares += sample.counts * sample.counts; ++n;
        if (age > config::stable_window_ms / 2) { older += sample.counts; ++old_n; }
        else { newer += sample.counts; ++new_n; }
    }
    if (n < config::stable_min_samples || now - oldest < config::stable_min_ms || !old_n || !new_n) {
        stability_reason_ = "warming_up";
        stable_since_.reset(); stable_latched_ = false; unstable_since_.reset(); return;
    }
    const double average = sum / n;
    const double noise = std::sqrt(std::max(0.0, squares / n - average * average));
    const double shift = std::abs(newer / new_n - older / old_n);
    const auto stats = stability_stats();
    // Long-window drift remains a guard once available, never a startup wait.
    const bool drift = now - stability_motion_ms_ >= config::drift_window_ms &&
                       stats.drift_span_ms >= config::drift_min_ms &&
                       std::abs(stats.drift_counts_s) > stats.drift_limit;
    operational_noise_ = noise; half_shift_ = shift;
    anchor_delta_ = std::abs(recent - stable_anchor_); drift_guard_ = drift;
    if (stable_latched_) {
        // A median-confirmed large load change exits immediately. Ordinary
        // jitter needs a persistent, wider threshold to release the latch.
        const bool hard = std::abs(recent - stable_anchor_) > units(config::stable_hard_shift_g, 2250);
        const bool bad = noise > units(config::stable_noise_exit_g, 1200) ||
                         std::abs(average - stable_anchor_) > units(config::stable_anchor_shift_g, 1500);
        if (bad && !unstable_since_) unstable_since_ = now;
        if (!bad) unstable_since_.reset();
        if (hard || (unstable_since_ && now - *unstable_since_ >= config::stable_exit_ms)) {
            stability_reason_ = hard ? "hard_shift" : noise > units(config::stable_noise_exit_g, 1200) ? "noise_exit" : "anchor_shift";
            if (hard) stability_motion_ms_ = now;
            stable_latched_ = false; stable_since_.reset(); unstable_since_.reset();
        }
    } else {
        const bool qualifies = noise <= units(config::stable_noise_enter_g, 750) &&
                               shift <= units(config::stable_half_shift_g, 600) && !drift;
        if (!qualifies) {
            stability_reason_ = noise > units(config::stable_noise_enter_g, 750) ? "noise_enter" :
                                shift > units(config::stable_half_shift_g, 600) ? "half_shift" : "drift_guard";
            stable_since_.reset();
        }
        else {
            stability_reason_ = "enter_hold";
            if (!stable_since_) stable_since_ = now;
            if (now - *stable_since_ >= config::stable_enter_ms) {
                stable_latched_ = true; stable_anchor_ = average; stability_reason_ = "qualified";
            }
        }
    }
}
StabilityStats Scale::stability_stats() const {
    StabilityStats stats;
    if (!connected_ || overload_ || !filtered_) return stats;
    double sy = 0;
    unsigned n = 0;
    std::uint64_t oldest = last_ms_;
    for (unsigned i = 0; i < count_; ++i) {
        const auto& s = samples_[i];
        if (last_ms_ - s.ms > config::stability_window_ms) continue;
        oldest = std::min(oldest, s.ms);
        sy += s.counts;
        ++n;
    }
    stats.samples = n;
    stats.span_ms = last_ms_ - oldest;
    if (n < 8 || stats.span_ms < config::stability_min_ms) return stats;
    const double my = sy / n;
    double yy = 0, low = my, high = my;
    for (unsigned i = 0; i < count_; ++i) {
        const auto& s = samples_[i];
        if (last_ms_ - s.ms > config::stability_window_ms) continue;
        const double y = s.counts - my;
        low = std::min(low, s.counts); high = std::max(high, s.counts);
        yy += y * y;
    }
    // Noise/settling remain responsive over 1.2 s. Estimate sustained drift over
    // a separate 3.2 s window so small oscillations do not keep resetting the hold.
    double dx = 0, dy = 0;
    auto drift_oldest = last_ms_;
    for (unsigned i = 0; i < count_; ++i) {
        const auto& s = samples_[i];
        if (last_ms_ - s.ms > config::drift_window_ms) continue;
        drift_oldest = std::min(drift_oldest, s.ms);
        dx += static_cast<double>(last_ms_ - s.ms) / 1000;
        dy += s.counts;
        ++stats.drift_samples;
    }
    stats.drift_span_ms = last_ms_ - drift_oldest;
    double xx = 0, xy = 0;
    if (stats.drift_samples) {
        const double mx = dx / stats.drift_samples, mean_y = dy / stats.drift_samples;
        for (unsigned i = 0; i < count_; ++i) {
            const auto& s = samples_[i];
            if (last_ms_ - s.ms > config::drift_window_ms) continue;
            const double x = static_cast<double>(last_ms_ - s.ms) / 1000 - mx;
            xx += x * x; xy += x * (s.counts - mean_y);
        }
    }
    const double factor = calibration_ ? std::abs(calibration_->counts_per_gram) : 0;
    const double std_limit = calibration_ ? std::max(2.0, factor * config::stability_std_g) : 150.0;
    const double drift_limit = calibration_ ? std::max(1.0, factor * config::stability_drift_g_s) : 50.0;
    stats.std_counts = std::sqrt(yy / n);
    stats.drift_counts_s = xx > 0 ? -xy / xx : 0; // x is sample age; reverse sign for time.
    stats.residual_counts = std::abs(*filtered_ - my);
    stats.peak_to_peak_counts = high - low;
    stats.std_limit = std_limit;
    stats.drift_limit = drift_limit;
    stats.failed = (xx <= 0 || stats.drift_span_ms < config::drift_min_ms ? 1U : 0U) | (stats.std_counts > std_limit ? 2U : 0U) |
        (std::abs(stats.drift_counts_s) > drift_limit ? 4U : 0U) |
        (stats.residual_counts > std_limit ? 8U : 0U);
    return stats;
}
bool Scale::restore(Calibration c) {
    if (!valid_calibration(c)) { notify("calibration_rejected"); return false; }
    calibration_ = c;
    // Restart motion thresholds in the new calibration units, retaining the
    // count history used by the measurement averages and diagnostics.
    adaptive_.reset();
    tare_g_ = 0;
    flow_g_s_ = 0;
    ++display_epoch_;
    notify("calibration_restored");
    return true;
}
bool Scale::capture_zero() {
    if (!stable()) { notify("zero_rejected"); return false; }
    captured_zero_ = mean();
    notify("zero_captured");
    return true;
}
bool Scale::calibrate(float grams) {
    if (!captured_zero_ || !stable() || !std::isfinite(grams) || grams <= 0 || grams > 10000) { notify("calibration_rejected"); return false; }
    const auto delta = mean() - *captured_zero_;
    if (std::abs(delta) < 1000) { notify("calibration_rejected"); return false; }
    Calibration c{*captured_zero_, delta / grams};
    if (!restore(c)) return false;
    pending_save_ = c;
    captured_zero_.reset();
    return true;
}
bool Scale::tare() {
    if (!ready() || !stable()) { notify("tare_rejected"); return false; }
    // Fast Stable may precede low-pass convergence after placing a vessel.
    // Tare the qualified median mean and align filter state to prevent the
    // unchanged vessel from drifting upward as the old filter catches up.
    filtered_ = mean();
    adaptive_.reset();
    flow_filtered_ = filtered_;
    tare_g_ = gross();
    flow_g_s_ = 0;
    ++display_epoch_;
    notify("tare_applied");
    return true;
}
std::optional<Calibration> Scale::take_pending_save() {
    const auto c = pending_save_;
    pending_save_.reset();
    return c;
}
const char* Scale::status() const {
    if (!connected_) return "HX711 not ready - check wiring";
    if (overload_) return "Overload / signal out of range";
    if (captured_zero_) return "Zero captured. Place known mass.";
    if (!calibration_) return "Calibration required";
    if (save_failed_) return "Calibration not saved - retry";
    return stable() ? "Stable" : "Settling...";
}
}
