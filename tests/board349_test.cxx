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
    sample(10, 10, 0);
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
    sample(10, 10, 0);
    assert(b::decode_touch_report(packet.data(), packet.size()).state == b::TouchState::released);
    gate.filter(false, true); assert(!gate.suppressed());
    sample(10, 10, 1);
    uint32_t baseline{};
    assert(smart_scale::prepare_expander_wake(read_port, read_touch, []{return true;}, baseline, 1U, 0U)
           == smart_scale::WakePreparation::touch);
    // Uniform responses release both UI and sleep immediately, for every byte.
    b::TouchIdleQualifier idle;
    b::TouchIdleQualifier ui_idle{b::TouchIdleQualifier::Use::ui};
    for (unsigned byte=0; byte<256; ++byte) {
        packet.fill(byte);
        const auto release=b::decode_touch_report(packet.data(),packet.size());
        assert(release.state==b::TouchState::released && release.contacts==0);
        assert(idle.update(release,true,true,0).state==b::TouchState::released);
        assert(ui_idle.update(release,true,false,0).state==b::TouchState::released);
        gate.filter(true,true);
        gate.filter(false,false);
        assert(!gate.suppressed());
        assert(idle.update(release,false,true,0).state==b::TouchState::invalid);
    }
    for (const auto byte : {0xa5, 0xd5, 0x84}) {
        packet.fill(byte); packet[5]=0;
        assert(b::decode_touch_report(packet.data(),packet.size()).state==b::TouchState::invalid);
    }
    auto idle_read = [&](bool& pressed) { pressed=false; return true; };
    assert(smart_scale::prepare_expander_wake(read_port,idle_read,[]{return true;},baseline,1U,0U)
           == smart_scale::WakePreparation::ready);
    assert(smart_scale::prepare_expander_wake(read_port,idle_read,[]{return false;},baseline,1U,0U)
           == smart_scale::WakePreparation::pending_irq);
    std::puts("Rev1.1 frame rotation, DMA strip seams, touch bounds and release checks passed");
}
