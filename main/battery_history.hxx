#pragma once
#include "battery.hxx"
#include <array>
#include <algorithm>
#include <cstdint>

namespace smart_scale {
enum class UsageState : uint8_t { charging, active, sleep, unknown };
struct BatteryPoint {
    uint64_t seconds{}; // Monotonic uptime, including light sleep; never wall time.
    uint8_t percent{255};
    UsageState state{UsageState::unknown}; // State of the interval AFTER this point.
};
class BatteryHistory {
public:
    static constexpr std::size_t capacity = 2048;
    static constexpr uint64_t retention_seconds = 3 * 86400;
    static constexpr uint64_t sample_seconds = 300;
    static UsageState state(const BatterySnapshot& b, bool sleeping = false) {
        if (!b.valid) return UsageState::unknown;
        if (b.external_power) return UsageState::charging;
        return b.present ? (sleeping ? UsageState::sleep : UsageState::active) : UsageState::unknown;
    }
    void update(uint64_t seconds, UsageState state, const BatterySnapshot& battery) {
        if (started_ && seconds < live_.seconds) clear();
        BatteryPoint point{seconds, 255, state};
        if (battery.valid && battery.present && battery.percent && *battery.percent <= 100)
            point.percent = static_cast<uint8_t>(*battery.percent);
        if (started_ && point.seconds == live_.seconds && point.percent == live_.percent && point.state == live_.state) return;
        const bool transition = started_ && (point.state != live_.state || (point.percent == 255) != (live_.percent == 255));
        if (!started_ || transition || seconds - at(count_ - 1).seconds >= sample_seconds) append(point);
        live_ = point; started_ = true; ++revision_;
        // Keep one preceding anchor so an interval crossing the left boundary can be clipped.
        const auto cutoff = seconds > retention_seconds ? seconds - retention_seconds : 0;
        while (count_ > 1 && at(1).seconds <= cutoff) { first_ = (first_ + 1) % capacity; --count_; }
    }
    void clear() { first_ = count_ = 0; live_ = {}; started_ = false; ++revision_; }
    std::size_t size() const { return count_; }
    uint64_t revision() const { return revision_; }
    uint64_t now() const { return live_.seconds; }
    const BatteryPoint& latest() const { return live_; }
    const BatteryPoint& at(std::size_t i) const { return points_[(first_ + i) % capacity]; }
    // Project measured endpoints into a trailing time window. Unknown telemetry
    // breaks the trace. Sleep joins are estimates and the UI renders them dashed.
    template<class Draw> void segments(uint64_t window, Draw draw) const {
        if (!started_ || !window) return;
        const double start = static_cast<double>(live_.seconds) - window;
        auto segment = [&](const BatteryPoint& a, const BatteryPoint& b) {
            if (b.seconds <= a.seconds || a.percent > 100 || b.percent > 100 || a.state == UsageState::unknown) return;
            const double left = std::max(start, static_cast<double>(a.seconds));
            const double right = std::min(static_cast<double>(live_.seconds), static_cast<double>(b.seconds));
            if (left >= right) return;
            auto level = [&](double t) { return a.percent + (static_cast<double>(b.percent) - a.percent) *
                (t - a.seconds) / (b.seconds - a.seconds); };
            draw((left - start) / window, level(left), (right - start) / window, level(right), a.state);
        };
        for (std::size_t i = 1; i < count_; ++i) segment(at(i-1), at(i));
        if (count_) segment(at(count_-1), live_);
    }
private:
    void append(const BatteryPoint& point) {
        if (count_ && at(count_-1).seconds == point.seconds) {
            points_[(first_ + count_ - 1) % capacity] = point;
            return;
        }
        if (count_ == capacity) { first_ = (first_ + 1) % capacity; --count_; }
        points_[(first_ + count_) % capacity] = point; ++count_;
    }
    std::array<BatteryPoint, capacity> points_{};
    BatteryPoint live_{};
    std::size_t first_{}, count_{};
    uint64_t revision_{};
    bool started_{};
};
static_assert(sizeof(BatteryPoint) == 16);
static_assert(sizeof(BatteryHistory) < 33 * 1024);
}
