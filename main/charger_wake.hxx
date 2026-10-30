#pragma once
#include <cstdint>

namespace smart_scale {
// AXP2101: INTEN2[7] VBUS insert; INTSTS2[7] is write-one-to-clear.
// Preserve every other interrupt enable/status and all charging/rail settings.
template<class Read, class Write>
bool arm_charger_wake(Read read, Write write, bool& connected) {
    std::uint8_t id{}, enabled{}, status0{}, status1{};
    if (!read(0x03, id) || id != 0x4a || !read(0x41, enabled)) return false;
    if (!(enabled & 0x80) && !write(0x41, enabled | 0x80)) return false;
    if (!write(0x49, 0x80)) return false; // stale insertion only
    // Check after arming/acknowledging so insertion during setup cannot be lost.
    if (!read(0x00, status0) || !read(0x01, status1)) return false;
    connected = (status0 & 0x20) && !(status1 & 0x08);
    return true;
}
template<class Read, class Write>
bool acknowledge_charger_wake(Read read, Write write, bool& inserted) {
    std::uint8_t status{};
    if (!read(0x49, status)) return false;
    inserted = status & 0x80;
    return !inserted || write(0x49, 0x80);
}
}
