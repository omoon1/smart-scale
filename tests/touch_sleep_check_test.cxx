#include "touch_sleep_check.hxx"
#include "expander_wake.hxx"
#include <cassert>
#include <iostream>
using namespace smart_scale;
using namespace smart_scale::board349;
struct Hardware {
    uint64_t time{};
    unsigned reads{}, spacing{25}, contact_at{}, invalid_at{}, fault_at{}, low_at{};
    bool release{}, frozen{};
    TouchSleepCheck run() {
        return qualify_sleep_touch([&](TouchReport& report) {
            ++reads;
            report = {TouchState::no_data, 0, {}, "idle_a5_candidate"};
            if (release) report.state = TouchState::released;
            if (reads == contact_at) report.state = TouchState::pressed;
            if (reads == invalid_at) report.state = TouchState::invalid;
            return reads != fault_at;
        }, [&] { return reads != low_at; }, [&] { return time; },
        [&](unsigned ms) { assert(ms == 25); if (!frozen) time += spacing * 1000; });
    }
};
int main() {
    // Reproduce normal UI cadence: sleep qualifier never completes at 125 ms.
    TouchIdleQualifier old;
    for (uint64_t t=0; t<2000000; t+=125000)
        assert(old.update({TouchState::no_data,0,{},"a5"},true,true,t).state == TouchState::no_data);
    Hardware h; h.time=2000000;
    assert(h.run() == TouchSleepCheck::released); assert(h.reads == 13);
    assert(h.time == 2300000); // fresh 300 ms, no UI history reuse
    Hardware release; release.release=true;
    assert(release.run() == TouchSleepCheck::released); assert(release.reads == 1);
    Hardware release_irq; release_irq.release=true; release_irq.low_at=1;
    assert(release_irq.run() == TouchSleepCheck::released); // final IRQ guards still required
    for (unsigned n=1; n<=13; ++n) {
        Hardware contact; contact.contact_at=n; assert(contact.run()==TouchSleepCheck::pressed);
        Hardware invalid; invalid.invalid_at=n; assert(invalid.run()==TouchSleepCheck::invalid);
        Hardware fault; fault.fault_at=n; assert(fault.run()==TouchSleepCheck::io_error);
        Hardware irq; irq.low_at=n; assert(irq.run()==TouchSleepCheck::pending_irq);
    }
    Hardware slow; slow.spacing=125;
    assert(slow.run()==TouchSleepCheck::timeout); assert(slow.time==500000);
    Hardware delayed; delayed.spacing=100;
    assert(delayed.run()==TouchSleepCheck::released); assert(delayed.time==300000);
    Hardware frozen; frozen.frozen=true;
    assert(frozen.run()==TouchSleepCheck::timeout); assert(frozen.reads==21);
    // No stale inferred release across repeated attempts / wake rechecks.
    for (unsigned cycle=0;cycle<100;++cycle) {
        Hardware repeated; assert(repeated.run()==TouchSleepCheck::released);
        assert(repeated.time==300000);
    }
    // Real preparation uses the same bounded helper on both sides of port ack.
    uint32_t baseline{}; unsigned checks{};
    auto prepare = [&](bool late_contact, bool final_irq) {
        checks=0;
        return prepare_expander_wake([](uint32_t& v){v=0xff;return true;},
            [&](bool& pressed) {
                Hardware probe;
                if (++checks == 2 && late_contact) probe.contact_at=7;
                const auto result=probe.run();
                pressed=result==TouchSleepCheck::pressed;
                return pressed || result==TouchSleepCheck::released;
            }, [&]{return final_irq;}, baseline, 1U, 0U);
    };
    assert(prepare(false,true)==WakePreparation::ready);
    assert(prepare(true,true)==WakePreparation::touch);
    assert(prepare(false,false)==WakePreparation::pending_irq);
    std::cout << "bounded touch sleep qualification tests passed\n";
}
