#pragma once
#include "board349.hxx"

namespace smart_scale {
enum class TouchSleepCheck { released, pressed, pending_irq, io_error, invalid };
// Caller holds the LVGL lock. Read one fresh RAW report. Uniform packets are
// idle immediately; existing expander acknowledgement and final IRQ checks remain.
template<class Read, class IrqHigh>
TouchSleepCheck qualify_sleep_touch(Read read, IrqHigh irq_high) {
    board349::TouchReport raw;
    if (!read(raw)) return TouchSleepCheck::io_error;
    switch (raw.state) {
    case board349::TouchState::pressed: return TouchSleepCheck::pressed;
    case board349::TouchState::released: return TouchSleepCheck::released;
    case board349::TouchState::no_data:
        return irq_high() ? TouchSleepCheck::released : TouchSleepCheck::pending_irq;
    case board349::TouchState::invalid: return TouchSleepCheck::invalid;
    }
    return TouchSleepCheck::invalid;
}
inline const char* touch_sleep_reason(TouchSleepCheck result) {
    switch (result) {
    case TouchSleepCheck::released: return "released";
    case TouchSleepCheck::pressed: return "touch";
    case TouchSleepCheck::pending_irq: return "irq_low";
    case TouchSleepCheck::io_error: return "i2c_error";
    case TouchSleepCheck::invalid: return "invalid_packet";
    }
    return "unknown";
}
}
