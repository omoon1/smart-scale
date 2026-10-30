#include "expander_wake.hxx"
#include "power.hxx"
#include <cassert>
#include <cstdint>
#include <vector>
using namespace smart_scale;

struct Hardware {
    std::uint32_t port{0xff};
    bool irq{true}, fail_port{}, fail_touch{}, pressed{}, pressed_after_ack{};
    unsigned reads{};
    std::vector<char> calls;
    WakePreparation prepare(std::uint32_t& baseline) {
        return prepare_expander_wake(
            [&](std::uint32_t& value) { calls.push_back('P'); value = port; return !fail_port; },
            [&](bool& value) { calls.push_back('T'); value = reads++ ? pressed_after_ack : pressed; return !fail_touch; },
            [&] { calls.push_back('I'); return irq; }, baseline);
    }
};
int main() {
    std::uint32_t baseline{};
    Hardware idle;
    assert(idle.prepare(baseline) == WakePreparation::ready);
    assert(baseline == 0xff);
    assert((idle.calls == std::vector<char>{'T','P','T','I'}));
    Hardware held; held.pressed = true;
    assert(held.prepare(baseline) == WakePreparation::touch);
    assert(held.calls.size() == 1); // Do not acknowledge a held touch then sleep.
    Hardware low; low.port &= ~expander_touch_mask;
    assert(low.prepare(baseline) == WakePreparation::touch);
    Hardware raced; raced.pressed_after_ack = true;
    assert(raced.prepare(baseline) == WakePreparation::touch);
    Hardware pending; pending.irq = false;
    assert(pending.prepare(baseline) == WakePreparation::pending_irq);
    Hardware port_fault; port_fault.fail_port = true;
    assert(port_fault.prepare(baseline) == WakePreparation::io_error);
    Hardware touch_fault; touch_fault.fail_touch = true;
    assert(touch_fault.prepare(baseline) == WakePreparation::io_error);

    assert(classify_expander_wake(0xff,0xfb,false,true) == ExpanderWake::touch);
    assert(classify_expander_wake(0xff,0xdf,true,true) == ExpanderWake::touch); // simultaneous PMU + touch
    assert(classify_expander_wake(0xff,0xdf,false,true) == ExpanderWake::shared); // PMU input
    assert(classify_expander_wake(0xff,0xef,false,true) == ExpanderWake::shared); // RTC input
    assert(classify_expander_wake(0xff,0xff,false,true) == ExpanderWake::ambiguous); // short pulse
    assert(classify_expander_wake(0xff,0xdf,false,false) == ExpanderWake::io_error);

    PowerPolicy power;
    assert(power.update(360000,false) == PowerState::screen_off);
    assert(power.sleep_due(360000)); assert(power.report_sleep_once());
    power.retry_sleep(400000); // shared event or failed entry: do not reset user idle
    assert(power.update(400000,false) == PowerState::screen_off);
    assert(!power.sleep_due(429999)); assert(power.sleep_due(430000));
    assert(power.report_sleep_once()); assert(!power.report_sleep_once());
    power.retry_sleep(430000);
    power.activity(430001); // a real touch cancels the retry backoff
    assert(power.update(430001,false) == PowerState::active);
    assert(!power.sleep_due(790000)); assert(power.sleep_due(790001));
    power.retry_sleep(790001);
    assert(power.update(790002,true) == PowerState::active);
    assert(!power.sleep_due(900000)); // brewing still inhibits sleep
}
