#pragma once
#include "board349.hxx"

namespace smart_scale {
enum class TouchSleepCheck { released, pressed, pending_irq, io_error, invalid, timeout };
// Caller holds the LVGL lock. Read returns a fresh RAW decoded report, not a
// UI-qualified release. Do not acknowledge the expander while qualifying A5.
template<class Read, class IrqHigh, class Now, class Wait>
TouchSleepCheck qualify_sleep_touch(Read read, IrqHigh irq_high, Now now, Wait wait) {
    board349::TouchIdleQualifier qualifier;
    const auto started = now();
    // Also bound iterations if a test/platform clock does not advance.
    for (unsigned attempt = 0; attempt < 21; ++attempt) {
        board349::TouchReport raw;
        if (!read(raw)) return TouchSleepCheck::io_error;
        const auto sampled = now();
        if (raw.state == board349::TouchState::pressed) return TouchSleepCheck::pressed;
        if (raw.state == board349::TouchState::invalid) return TouchSleepCheck::invalid;
        if (sampled < started || sampled - started >= 500000) return TouchSleepCheck::timeout;
        // An explicit release proceeds to the existing expander acknowledge /
        // final IRQ checks; only inferred A5 release needs a quiet IRQ here.
        if (raw.state == board349::TouchState::released) return TouchSleepCheck::released;
        if (!irq_high()) return TouchSleepCheck::pending_irq;
        const auto report = qualifier.update(raw, true, true, sampled);
        if (report.state == board349::TouchState::released) return TouchSleepCheck::released;
        wait(25); // milliseconds; I2C work may add latency, checked by qualifier
    }
    return TouchSleepCheck::timeout;
}
inline const char* touch_sleep_reason(TouchSleepCheck result) {
    switch (result) {
    case TouchSleepCheck::released: return "released";
    case TouchSleepCheck::pressed: return "touch";
    case TouchSleepCheck::pending_irq: return "irq_low";
    case TouchSleepCheck::io_error: return "i2c_error";
    case TouchSleepCheck::invalid: return "invalid_packet";
    case TouchSleepCheck::timeout: return "qualification_timeout";
    }
    return "unknown";
}
}
