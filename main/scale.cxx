#include "scale.hxx"
#include <algorithm>

namespace smart_scale {
void Scale::clear_window() {
    count_ = cursor_ = median_count_ = median_cursor_ = 0;
    filtered_.reset();
    adaptive_.reset();
    flow_filtered_.reset();
    stable_since_.reset();
    flow_g_s_ = 0;
    ++display_epoch_;
}
void Scale::invalidate() {
    ++faults_;
    connected_ = false;
    captured_zero_.reset();
    clear_window();
}
float Scale::mean() const {
    double sum = 0;
    unsigned n = 0;
    for (unsigned i = 0; i < count_; ++i) {
        if (last_ms_ - samples_[i].ms <= config::stability_window_ms) { sum += samples_[i].counts; ++n; }
    }
    return n ? static_cast<float>(sum / n) : 0;
}
void Scale::ingest(std::int32_t raw, std::uint64_t now) {
    // Ignore duplicate/out-of-order timestamps. Gaps cannot reuse an old baseline.
    if (connected_ && now <= last_ms_) return;
    if (connected_ && now - last_ms_ > config::sensor_timeout_ms) invalidate();
    const auto previous_ms = last_ms_;
    last_ms_ = now;
    last_raw_ = raw;
    ++received_;
    connected_ = true;
    overload_ = raw <= -8388608 || raw >= 8388607;
    if (overload_) { ++faults_; clear_window(); captured_zero_.reset(); return; }
    median_[median_cursor_] = raw;
    median_cursor_ = (median_cursor_ + 1) % median_.size();
    median_count_ = std::min<unsigned>(median_count_ + 1, median_.size());
    if (median_count_ < median_.size()) return;
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
    }
    if (stability_stats().failed == 0) {
        if (!stable_since_) stable_since_ = now;
    } else stable_since_.reset();
}
void Scale::check_timeout(std::uint64_t now) {
    if (connected_ && now > last_ms_ && now - last_ms_ > config::sensor_timeout_ms) invalidate();
}
bool Scale::stable() const {
    return stable_since_ && stable_for_ms() >= config::stability_hold_ms && stability_stats().failed == 0;
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
    if (!valid_calibration(c)) return false;
    calibration_ = c;
    // Restart motion thresholds in the new calibration units, retaining the
    // count history used by the strict calibration/stability checks.
    adaptive_.reset();
    tare_g_ = 0;
    flow_g_s_ = 0;
    ++display_epoch_;
    return true;
}
bool Scale::capture_zero() {
    if (!stable()) return false;
    captured_zero_ = mean();
    return true;
}
bool Scale::calibrate(float grams) {
    if (!captured_zero_ || !stable() || !std::isfinite(grams) || grams <= 0 || grams > 10000) return false;
    const auto delta = mean() - *captured_zero_;
    if (std::abs(delta) < 1000) return false;
    Calibration c{*captured_zero_, delta / grams};
    if (!restore(c)) return false;
    pending_save_ = c;
    captured_zero_.reset();
    return true;
}
bool Scale::tare() {
    if (!ready() || !stable()) return false;
    tare_g_ = gross();
    flow_g_s_ = 0;
    ++display_epoch_;
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
