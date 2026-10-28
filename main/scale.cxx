#include "scale.hxx"
#include <algorithm>

namespace smart_scale {
float Scale::mean() const {
    double sum = 0;
    for (unsigned i = 0; i < count_; ++i) sum += samples_[i];
    return count_ ? static_cast<float>(sum / count_) : 0;
}
void Scale::ingest(std::int32_t raw, std::uint64_t now) {
    if (connected_ && now - last_ms_ > 1000) clear_window();
    last_ms_ = now;
    connected_ = true;
    overload_ = raw <= -8388608 || raw >= 8388607;
    if (overload_) { clear_window(); return; }
    samples_[cursor_] = raw;
    cursor_ = (cursor_ + 1) % samples_.size();
    count_ = std::min<unsigned>(count_ + 1, samples_.size());
    if (calibration_ && std::abs(gross()) > 10000) overload_ = true;
}
void Scale::check_timeout(std::uint64_t now) {
    if (now - last_ms_ > 1000) {
        connected_ = false;
        captured_zero_.reset(); // A disconnected calibration sequence must restart.
        clear_window();
    }
}
bool Scale::stable() const {
    if (!connected_ || overload_ || count_ != samples_.size()) return false;
    const auto bounds = std::minmax_element(samples_.begin(), samples_.end());
    const float limit = calibration_ ? std::max(20.0F, std::abs(calibration_->counts_per_gram) * 0.3F) : 500.0F;
    return static_cast<double>(*bounds.second) - *bounds.first <= limit;
}
bool Scale::restore(Calibration c) {
    if (!valid_calibration(c)) return false;
    calibration_ = c;
    tare_g_ = 0;
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
