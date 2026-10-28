#pragma once
#include "config.hxx"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace smart_scale {
// Calibration is independent of a temporary container tare. Counts may decrease
// under load; a negative factor is valid. Never inherit another cell's factor.
struct Calibration { float zero{}, counts_per_gram{}; };
inline bool valid_calibration(Calibration c) {
    return std::isfinite(c.zero) && std::abs(c.zero) < 8388607 &&
        std::isfinite(c.counts_per_gram) && std::abs(c.counts_per_gram) >= 0.01F &&
        std::abs(c.counts_per_gram) <= 16777215;
}
constexpr std::int32_t signed_hx711(std::uint32_t bits) {
    bits &= 0xffffff;
    return (bits & 0x800000) ? static_cast<std::int32_t>(bits) - 0x1000000 : static_cast<std::int32_t>(bits);
}
struct StabilityStats {
    unsigned samples{};
    std::uint64_t span_ms{};
    double std_counts{}, drift_counts_s{}, residual_counts{}, peak_to_peak_counts{};
    double std_limit{}, drift_limit{};
    // Bit mask: 1=warmup/unavailable, 2=noise, 4=drift, 8=filter not settled.
    unsigned failed{1};
};
class Scale {
public:
    void ingest(std::int32_t raw, std::uint64_t now_ms);
    void invalidate();
    std::optional<std::int32_t> last_raw() const { return last_raw_; }
    std::uint64_t sample_ms() const { return last_ms_; }
    float flow() const { return flow_g_s_; }
    void check_timeout(std::uint64_t now_ms);
    bool restore(Calibration calibration);
    bool capture_zero();
    void cancel_calibration() { captured_zero_.reset(); }
    bool calibrate(float known_grams);
    bool tare();
    void clear_tare() { if (tare_g_ != 0) { tare_g_ = 0; ++display_epoch_; } }
    void set_tare(float grams) { tare_g_ = grams; ++display_epoch_; }
    std::uint32_t display_epoch() const { return display_epoch_; }
    bool ready() const { return connected_ && filtered_.has_value() && calibration_.has_value() && !overload_; }
    StabilityStats stability_stats() const;
    bool stable() const;
    std::uint64_t received() const { return received_; }
    std::uint64_t faults() const { return faults_; }
    std::uint64_t stable_for_ms() const { return stable_since_ ? last_ms_ - *stable_since_ : 0; }
    std::optional<Calibration> calibration() const { return calibration_; }
    float gross() const { return calibration_ ? (filtered_.value_or(0) - calibration_->zero) / calibration_->counts_per_gram : 0; }
    float reference_gross() const { return calibration_ ? (mean() - calibration_->zero) / calibration_->counts_per_gram : 0; }
    float net() const { return gross() - tare_g_; }
    const char* status() const;
    std::optional<Calibration> take_pending_save();
    void saved(bool success) { save_failed_ = !success; }
private:
    float mean() const;
    void clear_window();
    struct Sample { double counts{}; std::uint64_t ms{}; };
    std::array<Sample, 128> samples_{};
    std::array<std::int32_t, 3> median_{};
    unsigned median_count_{}, median_cursor_{};
    std::optional<double> filtered_;
    float flow_g_s_{};
    unsigned count_{}, cursor_{};
    std::uint64_t last_ms_{};
    std::optional<std::int32_t> last_raw_;
    std::optional<std::uint64_t> stable_since_;
    std::uint64_t received_{}, faults_{};
    bool connected_{}, overload_{}, save_failed_{};
    float tare_g_{};
    std::uint32_t display_epoch_{};
    std::optional<float> captured_zero_;
    std::optional<Calibration> calibration_, pending_save_;
};
}
