#include "battery_history.hxx"
#include <cassert>
#include <iostream>
using namespace smart_scale;
constexpr int64_t midnight = 1793318400; // 2026-10-30 UTC
BatterySnapshot battery(unsigned p = 72, bool usb = false) { return {true, true, usb, usb, false, p}; }
const BatteryDay& day(const BatteryHistory& h, int64_t wall) { return h.data().days[(wall/86400)%7]; }
int main() {
    BatteryHistory h;
    h.update(0, midnight, UsageState::active, battery(80));
    h.update(3600, midnight+3600, UsageState::sleep, battery(75));
    h.update(7200, midnight+7200, UsageState::charging, battery(72), BatteryHistory::approximate);
    h.update(10800, midnight+10800, UsageState::active, battery(90,true));
    assert((day(h, midnight).seconds == std::array<uint32_t,3>{3600,3600,3600}));
    assert(day(h,midnight).first == 80 && day(h,midnight).last == 90);
    assert(day(h,midnight).minimum == 72 && day(h,midnight).maximum == 90);
    assert(day(h,midnight).flags & BatteryHistory::approximate);
    BatteryHistory crossing;
    crossing.update(0, midnight-60, UsageState::sleep, battery());
    crossing.update(120, midnight+60, UsageState::active, battery());
    assert(day(crossing, midnight-60).seconds[2] == 60);
    assert(day(crossing, midnight).seconds[2] == 60);
    crossing.update(86400*10+120, midnight+86400*10+60, UsageState::active, battery());
    for (const auto& d : crossing.data().days) assert(!d.day || d.day >= midnight/86400+4);
    BatteryHistory undated;
    undated.update(0, 0, UsageState::active, battery());
    undated.update(120, 120, UsageState::sleep, {});
    assert(undated.data().undated.seconds[1] == 120);
    undated.update(120, midnight, UsageState::active, battery());
    undated.update(180, midnight+60, UsageState::active, battery());
    assert(day(undated, midnight).seconds[1] == 60);
    assert(undated.data().undated.seconds[1] == 120);
    BatteryHistory restored;
    assert(restored.restore(h.data()));
    restored.update(0, midnight+20000, UsageState::active, battery());
    restored.update(60, midnight+20060, UsageState::unknown, {});
    assert(day(restored,midnight).seconds[1] == 3660); // no reboot gap
    restored.update(120,midnight+20120,UsageState::active,battery());
    assert(day(restored,midnight).seconds[1] == 3660); // unknown source excluded
    assert(day(restored,midnight).flags & BatteryHistory::incomplete);
    auto invalid=h.data(); invalid.version=99; assert(!restored.restore(invalid));
    invalid=h.data(); invalid.days[(midnight/86400)%7].seconds[0]=999999; assert(!restored.restore(invalid));
    BatteryHistory jumped;
    jumped.update(0,midnight,UsageState::active,battery());
    jumped.update(60,midnight+3600,UsageState::active,battery());
    assert(jumped.data().undated.seconds[1] == 60);
    assert(day(jumped,midnight).seconds[1] == 0);
    assert(BatteryHistory::state(battery(100,true),true)==UsageState::charging);
    assert(BatteryHistory::state(battery(),true)==UsageState::sleep);
    assert(BatteryHistory::state({})==UsageState::unknown);
    std::cout << "Battery history tests passed\n";
}
