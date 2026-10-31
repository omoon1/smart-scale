#include "power.hxx"
#include "battery.hxx"
#include <cassert>
#include <limits>
using namespace smart_scale;
int main() {
    assert(!smart_scale::decode_voltage_battery(0).valid);
    assert(!smart_scale::decode_voltage_battery(4400).valid);
    const auto voltage = smart_scale::decode_voltage_battery(3800);
    assert(voltage.percent == 50 && voltage.estimated && !voltage.source_known);
    assert(!voltage.charging && !voltage.full && !voltage.external_power);
    assert(smart_scale::battery_text(voltage) == "~50%");
    assert(smart_scale::decode_voltage_battery(4200).percent == 100);

    PowerPolicy p;
    assert(p.update(29999, false) == PowerState::active);
    assert(p.update(30000, false) == PowerState::dimmed);
    assert(p.update(59999, false) == PowerState::dimmed);
    assert(p.update(60000, false) == PowerState::screen_off);
    assert(!p.sleep_due(59999)); assert(p.sleep_due(60000));
    assert(p.report_sleep_once()); assert(!p.report_sleep_once());
    p.activity(360001); assert(!p.sleep_due(360001)); assert(p.report_sleep_once());
    assert(p.update(900000, true) == PowerState::active);
    assert(p.update(1000000, false) == PowerState::active);
    assert(!p.sleep_due(1000000));
    assert(p.update(1030000, false) == PowerState::dimmed);
    PowerPolicy noise;
    for (unsigned t = 0; t <= 360000; t += 100) noise.weight(100.0F + (t % 200 ? .1F : -.1F), t);
    assert(noise.sleep_due(360000));
    noise.weight(101.0F, 360100); noise.weight(101.0F, 360399);
    assert(noise.sleep_due(360399));
    noise.weight(101.0F, 360400); assert(!noise.sleep_due(360400));
    PowerPolicy slow;
    for (unsigned t=0; t<=1000; t+=100) slow.weight(100.0F + t * .001F,t);
    assert(slow.update(30000, false) == PowerState::active);
    PowerPolicy invalid;
    invalid.weight(std::nullopt, 360000);
    invalid.weight(std::numeric_limits<float>::quiet_NaN(), 360000);
    assert(invalid.sleep_due(360000));
    WakeTouchGate gate;
    assert(!gate.filter(true,true)); assert(!gate.filter(true,false));
    assert(!gate.filter(false,false)); assert(gate.filter(true,false));
    auto b = decode_axp2101(0x28,0x20,72);
    assert(b.valid && b.present && b.external_power && b.charging && b.percent == 72);
    assert(battery_text(b) == "72%");
    assert(battery_color(b) == 0x55C878);
    b = decode_axp2101(0x28, 0x20, 19); // charging overrides red
    assert(battery_color(b) == 0x55C878);
    b = decode_axp2101(8, 0x40, 19); assert(battery_color(b) == 0xF06464);
    b = decode_axp2101(8, 0x40, 0); assert(battery_color(b) == 0xF06464);
    b = decode_axp2101(8, 0x40, 20); assert(battery_color(b) == 0xA6A6A6);
    b = decode_axp2101(8, 0x40, 100); assert(battery_color(b) == 0xA6A6A6);
    assert(battery_color({}) == 0xA6A6A6);
    b = decode_axp2101(0x28,4,100); assert(b.full && !b.charging);
    b = decode_axp2101(0x20,0,75); assert(!b.present && !b.percent && battery_text(b)=="USB");
    b = decode_axp2101(8,0x40,255); assert(!b.percent && !b.external_power);
    assert(battery_text({}) == "Battery --");
    PowerPolicy uptime(0x100000000ULL);
    assert(uptime.update(0x100000000ULL+60000, false)==PowerState::screen_off);
    PowerPolicy charging;
    assert(charging.update(0, false, true) == PowerState::active);
    assert(charging.update(29999, false, true) == PowerState::active);
    assert(charging.update(30000, false, true) == PowerState::dimmed);
    assert(charging.update(60000, false, true) == PowerState::dimmed);
    assert(charging.update(599999, false, true) == PowerState::dimmed);
    assert(charging.update(600000, false, true) == PowerState::screen_off);
    assert(!charging.sleep_due(600000));
    assert(charging.update(9ULL*3600000, false, true) == PowerState::screen_off);
    assert(!charging.sleep_due(9ULL*3600000));
    charging.activity(9ULL*3600000+1); // Touch wakes a charging screen and restarts its timer.
    assert(charging.update(9ULL*3600000+1, false, true) == PowerState::active);
    assert(charging.update(9ULL*3600000+30001, false, true) == PowerState::dimmed);
    assert(charging.update(9ULL*3600000+600001, false, true) == PowerState::screen_off);
    const auto unplug = 9ULL*3600000+600002;
    assert(charging.update(unplug, false, false) == PowerState::active);
    assert(charging.update(unplug+30000, false, false) == PowerState::dimmed);
    assert(charging.update(unplug+60000, false, false) == PowerState::screen_off);
    assert(!charging.sleep_due(unplug+59999));
    assert(charging.sleep_due(unplug+60000));
    assert(charging.update(unplug+60001, false, true) == PowerState::active);
    assert(!charging.sleep_due(unplug+60001));
    assert(charging.update(unplug+90001, false, true) == PowerState::dimmed);
    assert(charging.update(unplug+90002, true, true) == PowerState::active);
}
