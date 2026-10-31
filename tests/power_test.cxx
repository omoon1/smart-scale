#include "power.hxx"
#include "battery.hxx"
#include "battery_history.hxx"
#include <type_traits>
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
    // No caller can accidentally reintroduce a charging-specific policy input.
    static_assert(std::is_same_v<decltype(&PowerPolicy::update),
                  PowerState (PowerPolicy::*)(std::uint64_t, bool)>);
    const BatterySnapshot sources[] = {
        decode_axp2101(8, 0x40, 72),    // battery
        decode_axp2101(0x28, 0x20, 72), // external, charging
        decode_axp2101(0x28, 4, 100),   // external, full
        decode_voltage_battery(3800),  // source unknown
        BatterySnapshot{},            // failed telemetry
    };
    for (const auto& source : sources) {
        PowerPolicy policy;
        BatteryHistory history;
        // Telemetry goes to display/history, never to policy (as in main).
        for (const auto now : {0ULL, 29999ULL, 30000ULL, 59999ULL, 60000ULL}) {
            history.update(now / 1000, BatteryHistory::state(source), source);
            assert(!battery_text(source).empty());
            const auto expected = now < 30000 ? PowerState::active :
                now < 60000 ? PowerState::dimmed : PowerState::screen_off;
            assert(policy.update(now, false) == expected);
            assert(policy.sleep_due(now) == (now >= 60000));
            assert(policy.idle_ms(now) == now);
        }
    }
    // Plug/unplug, unknown and failed readings cannot wake a dim/dark screen
    // or postpone its original deadline. Repeat transitions across that deadline.
    PowerPolicy transitions;
    BatteryHistory history;
    for (unsigned i = 0; i < 12; ++i) {
        const auto now = 50000ULL + i * 1000;
        const auto& source = sources[i % 5];
        history.update(now / 1000, BatteryHistory::state(source), source);
        assert(transitions.update(now, false) ==
               (now < 60000 ? PowerState::dimmed : PowerState::screen_off));
        assert(transitions.idle_ms(now) == now);
        assert(transitions.sleep_due(now) == (now >= 60000));
    }
    transitions.activity(62000); // Touch still wakes and restarts idle.
    assert(transitions.update(62000, false) == PowerState::active);
    assert(!transitions.sleep_due(121999));
    assert(transitions.sleep_due(122000));
    assert(transitions.update(122000, true) == PowerState::active);
    assert(!transitions.sleep_due(200000)); // Brewing/editing still inhibits sleep.
    assert(transitions.update(200000, false) == PowerState::active);
    assert(!transitions.sleep_due(259999)); // Release gets a fresh full minute.
    assert(transitions.sleep_due(260000));
    transitions.retry_sleep(260000);
    assert(!transitions.sleep_due(289999));
    assert(transitions.sleep_due(290000));
}
