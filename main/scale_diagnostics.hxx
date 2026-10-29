#pragma once
#include "scale.hxx"
#include <cstring>

namespace smart_scale {
inline void failed_names(unsigned mask, char (&out)[64]) {
    out[0] = '\0';
    const char* names[] = {"warming_up", "noise", "drift", "residual"};
    for (unsigned bit = 0; bit < 4; ++bit) if (mask & (1U << bit)) {
        if (out[0]) std::strcat(out, ",");
        std::strcat(out, names[bit]);
    }
    if (!out[0]) std::strcpy(out, "none");
}
struct DiagnosticSample {
    std::uint64_t seq{}, sample_ms{}, now_ms{}, dt_ms{};
    std::int32_t raw{};
    double raw_g{NAN}, median_g{NAN}, filtered_g{NAN}, net_g{NAN}, tare_g{};
    double factor{}, zero{}, sd{}, drift{}, p2p{}, residual{}, operational_sd{}, half_shift{}, anchor_delta{};
    unsigned failed{1};
    bool raw_valid{}, ready{}, stable{}, quiet{}, drift_guard{};
    const char* reason{"warming_up"};
};
inline DiagnosticSample diagnostic_sample(const Scale& scale, std::uint64_t now_ms) {
    DiagnosticSample s;
    s.seq = scale.received(); s.sample_ms = scale.sample_ms(); s.now_ms = now_ms;
    s.raw_valid = scale.last_raw().has_value();
    s.raw = scale.last_raw().value_or(0); s.tare_g = scale.tare_g();
    s.ready = scale.ready(); s.stable = scale.stable(); s.quiet = scale.quiet();
    s.reason = scale.stability_reason(); s.drift_guard = scale.drift_guard_active();
    const auto c = scale.calibration();
    const auto stats = scale.stability_stats(); s.failed = stats.failed;
    const double units = c ? std::abs(c->counts_per_gram) : 1;
    s.factor = c ? c->counts_per_gram : 0; s.zero = c ? c->zero : 0;
    if (c && scale.last_raw()) s.raw_g = (static_cast<double>(s.raw) - c->zero) / c->counts_per_gram;
    if (const auto median = scale.recent_gross()) s.median_g = *median;
    if (s.ready) { s.filtered_g = scale.gross(); s.net_g = scale.net(); }
    s.sd = stats.std_counts / units; s.p2p = stats.peak_to_peak_counts / units;
    s.drift = stats.drift_counts_s / (c ? c->counts_per_gram : 1);
    s.residual = stats.residual_counts / units;
    s.operational_sd = scale.operational_noise_counts() / units;
    s.half_shift = scale.half_shift_counts() / units; s.anchor_delta = scale.anchor_delta_counts() / units;
    return s;
}
// Controller-thread only; timestamps determine duration at both 10 and 80 SPS.
// Bounded even if capture is faster than expected. A frozen trace is retained
// until consumed; logging congestion cannot silently overwrite its evidence.
class AnomalyTrace {
public:
    static constexpr unsigned capacity = 1024;
    static constexpr std::uint64_t pre_ms = 5000, post_ms = 5000, cooldown_ms = 30000;
    static constexpr double jump_g = 1.0;
    const char* push(DiagnosticSample sample) {
        if (last_sample_ms_ && sample.sample_ms > last_sample_ms_) sample.dt_ms = sample.sample_ms - last_sample_ms_;
        last_sample_ms_ = sample.sample_ms;
        if (frozen_) { ++missed_samples_; return nullptr; }
        const bool gap = sample.dt_ms > config::sensor_timeout_ms;
        const bool jump = sample.ready && previous_ready_ && std::isfinite(sample.median_g) &&
                          std::abs(sample.median_g - previous_median_) >= jump_g;
        previous_ready_ = sample.ready; previous_median_ = sample.median_g;
        const unsigned failures = sample.ready && !(sample.failed & 1U) ? sample.failed & 14U : 0;
        const bool rising = (failures & ~previous_failures_) != 0;
        const bool stable_exit = previous_stable_ && !sample.stable;
        previous_failures_ = failures; previous_stable_ = sample.stable;
        if (!active_) {
            while (count_ && sample.sample_ms - rows_[head_].sample_ms > pre_ms) drop_front();
        }
        if (count_ == capacity) { drop_front(); ++truncated_; }
        rows_[(head_ + count_) % capacity] = sample; ++count_;
        const char* trigger = nullptr;
        if (gap || jump || rising || stable_exit) {
            const auto reason = gap ? "sample_gap" : jump ? "median_jump" : stable_exit ? "stable_exit" : "diagnostic_failure";
            if (begin(sample.sample_ms, reason)) trigger = reason;
        }
        poll(sample.now_ms);
        return trigger;
    }
    bool begin(std::uint64_t now_ms, const char* reason) {
        if (active_ || frozen_ || (has_triggered_ && now_ms < next_trigger_ms_)) return false;
        while (count_ && now_ms > rows_[head_].sample_ms && now_ms - rows_[head_].sample_ms > pre_ms) drop_front();
        trigger_ms_ = now_ms; trigger_reason_ = reason;
        active_ = has_triggered_ = true; ++id_;
        next_trigger_ms_ = now_ms + cooldown_ms;
        return true;
    }
    void poll(std::uint64_t now_ms) {
        if (active_ && !frozen_ && now_ms >= trigger_ms_ + post_ms) frozen_ = true;
    }
    bool frozen() const { return frozen_; }
    const DiagnosticSample* front() const { return frozen_ && count_ ? &rows_[head_] : nullptr; }
    void pop() { if (front()) drop_front(); }
    bool finish() {
        if (!frozen_ || count_) return false;
        active_ = frozen_ = false;
        previous_ready_ = previous_stable_ = false; previous_failures_ = 0;
        return true;
    }
    unsigned id() const { return id_; }
    unsigned truncated() const { return truncated_; }
    unsigned missed_samples() const { return missed_samples_; }
    std::uint64_t trigger_ms() const { return trigger_ms_; }
    const char* trigger_reason() const { return trigger_reason_; }
private:
    void drop_front() { head_ = (head_ + 1) % capacity; --count_; }
    std::array<DiagnosticSample, capacity> rows_{};
    unsigned head_{}, count_{}, id_{}, truncated_{}, missed_samples_{}, previous_failures_{};
    std::uint64_t last_sample_ms_{}, trigger_ms_{}, next_trigger_ms_{};
    double previous_median_{};
    bool active_{}, frozen_{}, has_triggered_{}, previous_ready_{}, previous_stable_{};
    const char* trigger_reason_{"none"};
};
}
