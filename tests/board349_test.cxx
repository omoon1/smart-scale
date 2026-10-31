#include "board349.hxx"
#include "power.hxx"
#include "expander_wake.hxx"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    namespace b = smart_scale::board349;
    // Unique indices expose transposed axes, reversal, strip seams and overrun.
    std::vector<unsigned> logical(b::frame_pixels), physical(b::frame_pixels);
    for (std::size_t i = 0; i < logical.size(); ++i) logical[i] = i;
    for (int y = 0; y < b::native_height; y += b::strip_rows)
        b::pack_strip(logical.data(), physical.data() + y * b::native_width, y);
    std::vector<bool> seen(b::frame_pixels);
    for (const auto index : physical) { assert(index < seen.size()); assert(!seen[index]); seen[index] = true; }
    assert(physical.front() == 171 * 640);
    assert(physical[171] == 0);
    assert(physical[639 * 172] == 640 * 172 - 1);
    assert(physical.back() == 639);
    // Independent inverse mapping checks every pixel across all ten DMA strips.
    for (int y = 0; y < b::height; ++y)
        for (int x = 0; x < b::width; ++x)
            assert(physical[x * b::native_width + (b::height - 1 - y)] == logical[y * b::width + x]);

    std::array<std::uint8_t, b::touch_packet_bytes> packet{};
    auto sample = [&](int x, int y, unsigned count) {
        packet[1] = count; packet[2] = (x >> 8) & 15; packet[3] = x & 255;
        packet[4] = (y >> 8) & 15; packet[5] = y & 255;
    };
    b::TouchPoint point;
    for (const auto x : {0, 639}) for (const auto y : {0, 171}) {
        sample(x, y, 1);
        assert(b::decode_touch(packet.data(), packet.size(), point));
        assert(point.x == 639 - x && point.y == 171 - y);
    }
    sample(319, 85, 1); assert(b::decode_touch(packet.data(), packet.size(), point));
    assert(point.x == 320 && point.y == 86);
    for (const auto count : {0, 2, 4, 255}) {
        sample(10, 10, count); assert(!b::decode_touch(packet.data(), packet.size(), point));
    }
    sample(640, 0, 1); assert(!b::decode_touch(packet.data(), packet.size(), point));
    sample(0, 172, 1); assert(!b::decode_touch(packet.data(), packet.size(), point));
    sample(4095, 4095, 1); assert(!b::decode_touch(packet.data(), packet.size(), point));
    sample(0, 0, 1); assert(!b::decode_touch(packet.data(), packet.size()-1, point));
    assert(!b::decode_touch(nullptr, packet.size(), point));
    // The request length must match the response size (old code requested 14/read 32).
    assert(b::touch_command[7] == packet.size());
    sample(0, 0, 0);
    assert(b::decode_touch_report(packet.data(), packet.size()).state == b::TouchState::released);
    sample(10, 10, 2); // Valid second contact: single-touch UI ignores it; sleep must not.
    packet[8] = 0; packet[9] = 20; packet[10] = 0; packet[11] = 20;
    auto report = b::decode_touch_report(packet.data(), packet.size());
    assert(report.state == b::TouchState::pressed && report.contacts == 2);
    assert(!b::decode_touch(packet.data(), packet.size(), point));
    packet[10] = 1; // Second contact outside panel bounds invalidates entire report.
    assert(b::decode_touch_report(packet.data(), packet.size()).state == b::TouchState::invalid);

    // Reproduce all 20 false-wake cycles: malformed contact count is an error,
    // never a touch/release, and must not acknowledge the expander IRQ.
    smart_scale::PowerPolicy policy(0);
    smart_scale::WakeTouchGate gate;
    gate.filter(true, true);
    unsigned port_reads = 0;
    auto read_port = [&](uint32_t& value) { ++port_reads; value = 0xff; return true; };
    auto read_touch = [&](bool& pressed) {
        const auto r = b::decode_touch_report(packet.data(), packet.size());
        pressed = r.state == b::TouchState::pressed;
        return r.state == b::TouchState::pressed || r.state == b::TouchState::released;
    };
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        const uint64_t now = 60000 + cycle * 30000;
        sample(0, 0, 0xa5);
        uint32_t baseline{};
        assert(smart_scale::prepare_expander_wake(read_port, read_touch, []{return true;}, baseline, 1U, 0U)
               == smart_scale::WakePreparation::io_error);
        assert(policy.update(now, false) == smart_scale::PowerState::screen_off);
        policy.retry_sleep(now);
        assert(!policy.sleep_due(now + 29999));
        assert(policy.sleep_due(now + 30000));
        assert(gate.suppressed()); // invalid input cannot clear a waking gesture
    }
    assert(port_reads == 0);
    sample(0, 0, 0);
    assert(b::decode_touch_report(packet.data(), packet.size()).state == b::TouchState::released);
    gate.filter(false, true); assert(!gate.suppressed());
    sample(10, 10, 1);
    uint32_t baseline{};
    assert(smart_scale::prepare_expander_wake(read_port, read_touch, []{return true;}, baseline, 1U, 0U)
           == smart_scale::WakePreparation::touch);
    // Replay device evidence: valid contact -> all-A5 idle -> next contact.
    b::TouchIdleQualifier idle;
    packet.fill(0xa5);
    const auto no_data = b::decode_touch_report(packet.data(), packet.size());
    assert(no_data.state == b::TouchState::no_data);
    packet[5] = 0; // A near-match is still malformed, never idle.
    assert(b::decode_touch_report(packet.data(), packet.size()).state == b::TouchState::invalid);
    packet.fill(0xa5);
    auto press = b::TouchReport{b::TouchState::pressed, 1, {10,10}, "contact"};
    auto apply = [&](b::TouchReport r, bool dark) {
        return (r.state == b::TouchState::released || r.state == b::TouchState::pressed)
            ? gate.filter(r.state == b::TouchState::pressed, dark) : false;
    };
    assert(!apply(idle.update(press, true, true, 0), true));
    for (unsigned t = 30; t <= 300; t += 30) {
        assert(!apply(idle.update(no_data, true, true, t*1000), false));
        assert(gate.suppressed());
    }
    // A held/contact report resets the entire idle qualification window.
    assert(!apply(idle.update(press, true, true, 310000), false));
    for (unsigned t = 340; t <= 610; t += 30) {
        assert(!apply(idle.update(no_data, true, true, t*1000), false));
        assert(gate.suppressed());
    }
    assert(!apply(idle.update(no_data, true, true, 640000), false));
    assert(!gate.suppressed());
    assert(apply(idle.update(press, true, true, 670000), false));
    // IRQ low, read failure, malformed packets, and stale samples cannot release.
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        idle.reset();
        for (unsigned t=0; t<=240; t+=30) idle.update(no_data,true,true,t*1000);
        if (scenario == 0) idle.update(no_data,true,false,270000);
        if (scenario == 1) idle.update(no_data,false,true,270000);
        if (scenario == 2) idle.update({},true,true,270000);
        const auto r = idle.update(no_data,true,true,scenario==3 ? 500000 : 300000);
        assert(r.state == b::TouchState::no_data);
    }
    idle.reset();
    for (unsigned t=0; t<=300; t+=30) report=idle.update(no_data,true,true,t*1000);
    assert(report.state == b::TouchState::released);
    auto idle_read = [&](bool& pressed) { pressed=false; return report.state==b::TouchState::released; };
    assert(smart_scale::prepare_expander_wake(read_port,idle_read,[]{return true;},baseline,1U,0U)
           == smart_scale::WakePreparation::ready);
    // A new pending IRQ still blocks entry after the touch checks.
    assert(smart_scale::prepare_expander_wake(read_port,idle_read,[]{return false;},baseline,1U,0U)
           == smart_scale::WakePreparation::pending_irq);
    idle.reset(); // resume starts a fresh interval even after a very long sleep
    assert(idle.update(no_data,true,true,90000000).state == b::TouchState::no_data);
    // UI release must not depend on the shared wake IRQ or sub-100ms rendering.
    b::TouchIdleQualifier ui_idle{b::TouchIdleQualifier::Use::ui};
    idle.reset();
    assert(!apply(ui_idle.update(press,true,false,0),true));
    for (unsigned t : {30U, 180U, 330U}) {
        report = ui_idle.update(no_data,true,false,t*1000);
        assert(idle.update(no_data,true,false,t*1000).state == b::TouchState::no_data);
        assert(!apply(report,false));
    }
    assert(!gate.suppressed());
    assert(apply(ui_idle.update(press,true,false,360000),false));
    assert(!apply(ui_idle.update({b::TouchState::released,0,{},"release"},true,false,390000),false));
    ui_idle.reset(); idle.reset();
    for (unsigned t : {0U, 150U, 300U}) {
        report=ui_idle.update(no_data,true,true,t*1000);
        assert(idle.update(no_data,true,true,t*1000).state == b::TouchState::no_data);
    }
    assert(report.state == b::TouchState::released);
    // UI never clears suppression on a timer alone, error, invalid packet, or hold.
    for (unsigned scenario=0; scenario<5; ++scenario) {
        ui_idle.reset();
        assert(!apply(ui_idle.update(press,true,false,0),true));
        apply(ui_idle.update(no_data,true,false,30000),false);
        apply(ui_idle.update(no_data,true,false,180000),false);
        if (scenario==0) report=ui_idle.update(no_data,false,false,330000);
        if (scenario==1) report=ui_idle.update({},true,false,330000);
        if (scenario==2) report=ui_idle.update(press,true,false,330000);
        if (scenario==3) report=ui_idle.update(no_data,true,false,2000000);
        if (scenario==4) { ui_idle.reset(); report=ui_idle.update(no_data,true,false,330000); }
        assert(!apply(report,false));
        assert(gate.suppressed());
    }
    std::puts("Rev1.1 frame rotation, DMA strip seams, touch bounds and release checks passed");
}
