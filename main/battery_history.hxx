#pragma once
#include "battery.hxx"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace smart_scale {
enum class UsageState { charging, active, sleep, unknown };
struct BatteryDay {
    int32_t day{}; // UTC epoch day; zero means undated
    std::array<uint32_t, 3> seconds{};
    uint8_t first{255}, last{255}, minimum{255}, maximum{255};
    uint32_t flags{};
};
struct BatteryHistoryData {
    uint32_t version{1};
    int32_t newest_day{};
    std::array<BatteryDay, 7> days{};
    BatteryDay undated{};
};
static_assert(sizeof(BatteryHistoryData) == 200);
class BatteryHistory {
public:
    static constexpr uint32_t incomplete = 1, approximate = 2;
    static bool dated(int64_t wall) { return wall >= 1704067200 && wall < 4102444800LL; }
    static UsageState state(const BatterySnapshot& b, bool sleeping = false) {
        if (!b.valid) return UsageState::unknown;
        if (b.external_power) return UsageState::charging;
        return b.present ? (sleeping ? UsageState::sleep : UsageState::active) : UsageState::unknown;
    }
    const BatteryHistoryData& data() const { return data_; }
    bool restore(const BatteryHistoryData& value) {
        if (value.version != 1 || value.newest_day < 0 || value.newest_day > 47482) return false;
        for (const auto& d : value.days) {
            if (d.day < 0 || d.day > value.newest_day) return false;
            uint64_t total = 0;
            for (auto s : d.seconds) total += s;
            if (total > 86400) return false;
            for (auto p : {d.first, d.last, d.minimum, d.maximum}) if (p > 100 && p != 255) return false;
        }
        data_ = value;
        for (auto& d : data_.days) if (d.day) d.flags |= incomplete;
        data_.undated.flags |= incomplete;
        dirty_ = true;
        started_ = false; // Never account for unobserved time across a restart.
        return true;
    }
    void update(uint64_t seconds, int64_t wall, UsageState next, const BatterySnapshot& b,
                uint32_t interval_flags = 0) {
        if (dated(wall)) advance(static_cast<int32_t>(wall / 86400));
        if (started_ && seconds >= previous_seconds_) {
            const auto elapsed = seconds - previous_seconds_;
            const bool continuous = dated(wall) && dated(previous_wall_) &&
                std::abs((wall - previous_wall_) - static_cast<int64_t>(elapsed)) <= 2;
            if (continuous) {
                // Bound work after a very long sleep to the retained seven-day window.
                int64_t cursor = std::max(previous_wall_, int64_t(data_.newest_day - 6) * 86400);
                const int64_t end = previous_wall_ + static_cast<int64_t>(elapsed);
                while (cursor < end) {
                    const auto day = static_cast<int32_t>(cursor / 86400);
                    const auto stop = std::min(end, int64_t(day + 1) * 86400);
                    if (day >= data_.newest_day - 6 && day <= data_.newest_day)
                        add(row(day), stop - cursor, current_, interval_flags);
                    cursor = stop;
                }
            } else if (elapsed) add(data_.undated, elapsed, current_, interval_flags | incomplete);
        }
        if (b.valid && b.present && b.percent && *b.percent <= 100) {
            auto& d = dated(wall) && wall / 86400 >= data_.newest_day - 6 ? row(wall / 86400) : data_.undated;
            const auto p = static_cast<uint8_t>(*b.percent);
            if (d.first == 255) d.first = p;
            d.last = p; d.minimum = std::min(d.minimum, p);
            d.maximum = d.maximum == 255 ? p : std::max(d.maximum, p);
            dirty_ = true;
        }
        previous_seconds_ = seconds; previous_wall_ = wall; current_ = next; started_ = true;
    }
    bool dirty() const { return dirty_; }
    void saved() { dirty_ = false; }
private:
    void advance(int32_t day) {
        if (day <= data_.newest_day) return;
        data_.newest_day = day;
        for (auto& d : data_.days) if (d.day < day - 6) d = BatteryDay{};
        dirty_ = true;
    }
    BatteryDay& row(int32_t day) {
        auto& d = data_.days[day % 7];
        if (d.day != day) { d = BatteryDay{}; d.day = day; }
        return d;
    }
    void add(BatteryDay& d, uint64_t seconds, UsageState state, uint32_t flags) {
        d.flags |= flags;
        if (state == UsageState::unknown) d.flags |= incomplete;
        else {
            auto& total = d.seconds[static_cast<unsigned>(state)];
            uint64_t limit = std::numeric_limits<uint32_t>::max();
            if (d.day) {
                uint64_t used = 0; for (auto s : d.seconds) used += s;
                limit = used < 86400 ? 86400 - used : 0;
                if (seconds > limit) d.flags |= incomplete;
            }
            total += static_cast<uint32_t>(std::min(seconds, std::min(limit, uint64_t(UINT32_MAX - total))));
        }
        dirty_ = true;
    }
    BatteryHistoryData data_{};
    uint64_t previous_seconds_{};
    int64_t previous_wall_{};
    UsageState current_{UsageState::unknown};
    bool started_{}, dirty_{};
};
}
