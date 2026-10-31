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
    const auto prime = [](PowerPolicy& policy) {
        for (unsigned t = 0; t <= 1000; t += 100) policy.weight(100.0F, t);
    };
    for (float amplitude : {.1F, 1.0F, 3.0F}) {
        for (unsigned period : {200U, 2000U, 10000U}) {
            PowerPolicy noise;
            prime(noise);
            for (unsigned t = 1100; t <= 120000; t += 100) {
                noise.weight(100.0F + (t % period < period / 2 ? amplitude : -amplitude), t);
                assert(noise.idle_ms(t) == t);
            }
            assert(noise.sleep_due(120000));
        }
    }
    // Drift accumulates far beyond the step threshold without extending idle.
    for (float rate : {-1.0F, -.1F, .1F, 1.0F}) {
        PowerPolicy drift;
        prime(drift);
        for (unsigned t = 1100; t <= 120000; t += 100) {
            drift.weight(100.0F + (t - 1000) * rate / 1000, t);
            assert(drift.idle_ms(t) == t);
        }
        assert(drift.sleep_due(120000));
    }
    for (float step : {-100.0F, -8.0F, 8.0F, 250.0F}) {
        PowerPolicy object;
        prime(object);
        for (unsigned t = 1100; t < 1600; t += 100) object.weight(100 + step, t);
        assert(object.idle_ms(1599) == 1599);
        object.weight(100 + step, 1600);
        assert(object.idle_ms(1600) == 0);
        for (unsigned t = 1700; t <= 61600; t += 100) object.weight(100 + step, t);
        assert(object.sleep_due(61600)); // one event, not one per held sample
    }
    PowerPolicy spike;
    prime(spike);
    spike.weight(350.0F, 1100);
    for (unsigned t = 1200; t <= 60000; t += 100) spike.weight(100.0F, t);
    assert(spike.sleep_due(60000));
    PowerPolicy reversal;
    prime(reversal);
    for (unsigned t = 1100; t <= 60000; t += 100)
        reversal.weight(t % 200 ? 120.0F : 80.0F, t);
    assert(reversal.sleep_due(60000));
    PowerPolicy unstable;
    prime(unstable);
    for (unsigned t = 1100; t <= 60000; t += 100)
        unstable.weight(t % 200 ? 120.0F : 125.0F, t);
    assert(unstable.sleep_due(60000));
    PowerPolicy small_dose;
    prime(small_dose);
    for (unsigned t = 1100; t <= 60000; t += 100) small_dose.weight(103.0F, t);
    assert(small_dose.sleep_due(60000));
    // Repeated UI reads cannot confirm a step without fresh sensor data.
    PowerPolicy duplicate;
    prime(duplicate);
    duplicate.weight(350.0F, 1100);
    for (unsigned i = 0; i < 100; ++i) duplicate.weight(350.0F, 1100);
    assert(duplicate.idle_ms(2000) == 2000);
    duplicate.weight(350.0F, 2000); // stale gap rebases, never confirms
    assert(duplicate.idle_ms(2000) == 2000);
    for (auto bad : {std::optional<float>{}, std::optional<float>{std::numeric_limits<float>::quiet_NaN()},
                     std::optional<float>{std::numeric_limits<float>::infinity()}}) {
        PowerPolicy recovery;
        prime(recovery);
        recovery.weight(350.0F, 1100); // interrupt a pending candidate
        recovery.weight(bad, 1200);
        for (unsigned t = 1300; t <= 60000; t += 100) recovery.weight(500.0F, t);
        assert(recovery.sleep_due(60000));
    }
    PowerPolicy resumed;
    prime(resumed);
    resumed.reset_weight(); // called on every acquisition resume, even sleep aborts
    for (unsigned t = 1100; t <= 60000; t += 100) resumed.weight(500.0F, t);
    assert(resumed.sleep_due(60000));
    resumed.activity(60000); // user wake still resets idle immediately
    assert(!resumed.sleep_due(60000));
    WeightActivitySettings tuning;
    tuning.change_g = 12; tuning.hold_ms = 800;
    PowerPolicy tuned(0, tuning);
    prime(tuned);
    for (unsigned t = 1100; t < 1900; t += 100) tuned.weight(120.0F, t);
    assert(tuned.idle_ms(1899) == 1899);
    tuned.weight(120.0F, 1900);
    assert(tuned.idle_ms(1900) == 0);
    PowerPolicy touch_first;
    prime(touch_first);
    for (unsigned t = 1100; t < 1600; t += 100) touch_first.weight(120, t);
    touch_first.activity(1650);
    touch_first.weight(120, 1600); // queued sample cannot backdate a touch
    assert(touch_first.idle_ms(1650) == 0);
    PowerPolicy backoff;
    prime(backoff);
    backoff.retry_sleep(60000);
    backoff.reset_weight();
    for (unsigned t = 60000; t <= 90000; t += 100) backoff.weight(500, t);
    assert(backoff.sleep_due(90000)); // resume offset cannot cancel retry deadline
    WeightActivityDetector backwards;
    for (unsigned t = 0; t <= 1000; t += 100) assert(!backwards.sample(100, t));
    assert(!backwards.sample(120, 1100));
    assert(!backwards.sample(120, 900));
    assert(!backwards.sample(120, 1400));
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
