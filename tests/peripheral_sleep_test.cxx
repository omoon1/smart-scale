#include "peripheral_sleep.hxx"
#include <array>
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>
using smart_scale::PeripheralSleep;
struct Bus {
    std::array<std::array<std::uint8_t, 256>, 128> regs{};
    int calls{}, fail{-1};
    bool corrupt{};
    std::vector<std::pair<unsigned, unsigned>> writes;
    Bus() {
        regs[0x6b][0] = 5; regs[0x6b][2] = 0x78; regs[0x6b][8] = 0x83;
        regs[0x51][0] = 0x05; regs[0x51][1] = 0xb0;
        regs[0x51][0x11] = 0x1f; regs[0x51][4] = 0x37;
    }
    bool read(std::uint8_t a, std::uint8_t r, std::uint8_t& v) {
        v = regs[a][r];
        if (corrupt && !writes.empty()) v ^= 0x80;
        return calls++ != fail;
    }
    bool write(std::uint8_t a, std::uint8_t r, std::uint8_t v) {
        writes.emplace_back(a, r);
        // Model an ambiguous failure: device accepts the write, host sees error.
        if (a == 0x51 && r == 1) v = (v & ~0x48) | (regs[a][r] & v & 0x48);
        regs[a][r] = v;
        return calls++ != fail;
    }
};
bool enter(PeripheralSleep& p, Bus& b) {
    return p.enter([&](auto a, auto r, auto& v){return b.read(a,r,v);},
                   [&](auto a, auto r, auto v){return b.write(a,r,v);});
}
bool restore(PeripheralSleep& p, Bus& b) {
    return p.restore([&](auto a, auto r, auto& v){return b.read(a,r,v);},
                     [&](auto a, auto r, auto v){return b.write(a,r,v);});
}
int main() {
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        PeripheralSleep p; Bus b; const auto original = b.regs;
        assert(enter(p,b)); assert(p.needs_restore());
        assert((b.regs[0x6b][8] & 3) == 0);
        assert((b.regs[0x6b][2] & 0x1a) == 2);
        assert((b.regs[0x51][0] & 4) == 0);
        assert((b.regs[0x51][1] & 0xb7) == 7);
        assert((b.regs[0x51][0x11] & 2) == 0);
        assert(b.regs[0x51][4] == 0x37); // time untouched
        assert((b.regs[0x51][0x11] & 4) == 4); // countdown stays running
        assert(!enter(p,b)); // cannot overwrite saved state
        assert(restore(p,b)); assert(!p.needs_restore());
        assert(b.regs == original);
        const auto calls = b.calls; assert(restore(p,b)); assert(b.calls == calls);
    }
    // Each read, write and verification can fail; restore original state even
    // when a failed write was delivered. Snapshot errors cause no writes.
    for (int fail = 0; fail < 16; ++fail) {
        PeripheralSleep p; Bus b; const auto original = b.regs;
        b.fail = fail; assert(!enter(p,b));
        if (fail < 6) assert(b.writes.empty());
        b.fail = -1; assert(restore(p,b)); assert(b.regs == original);
    }
    for (int fail = 0; fail < 10; ++fail) {
        PeripheralSleep p; Bus b; const auto original = b.regs;
        assert(enter(p,b)); b.fail = b.calls + fail;
        assert(!restore(p,b)); assert(p.needs_restore());
        b.fail = -1; assert(restore(p,b)); assert(b.regs == original);
    }
    { PeripheralSleep p; Bus b; b.regs[0x6b][0] = 0;
      assert(!enter(p,b)); assert(b.writes.empty()); }
    { PeripheralSleep p; Bus b; b.corrupt = true;
      assert(!enter(p,b)); assert(p.needs_restore());
      b.corrupt = false; assert(restore(p,b)); }
    { PeripheralSleep p; Bus b;
      assert(enter(p,b)); b.regs[0x51][1] |= 0x48; // new pending RTC events
      assert(restore(p,b)); assert((b.regs[0x51][1] & 0x48) == 0x48); }
    std::cout << "peripheral sleep tests passed\n";
}
