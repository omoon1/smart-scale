#pragma once
#include <cstdint>

namespace smart_scale {
// The QSPI command phase is 32 bits: opcode 02, command in bits 15:8.
constexpr uint32_t lcd_qspi_command(uint8_t command) {
    return 0x02000000U | (static_cast<uint32_t>(command) << 8);
}
// Conservatively dirty before transmission: an I/O error may still have reached
// the panel. All exits (including aborted CPU sleep) must attempt restore.
class LcdSleep {
public:
    bool needs_restore() const { return dirty_; }
    template<class Send, class Wait>
    bool enter(Send send, Wait wait) {
        if (dirty_) return false;
        dirty_ = true;
        if (!send(0x28)) return false;
        const bool ok = send(0x10);
        wait(150); // Also wait on uncertain delivery before sending Sleep Out.
        return ok;
    }
    template<class Send, class Wait>
    bool restore(Send send, Wait wait) {
        if (!dirty_) return true;
        const bool ok = send(0x11);
        wait(150); // >120 ms; leave margin for RTOS tick rounding.
        if (!ok || !send(0x29)) return false;
        dirty_ = false;
        return true;
    }
private:
    bool dirty_{};
};
}
