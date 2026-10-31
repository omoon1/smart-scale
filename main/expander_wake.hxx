#pragma once
#include <cstdint>

namespace smart_scale {
// Waveshare 3.5 V2: TP_INT -> TCA9554 P2, shared INT -> ESP32 GPIO4.
inline constexpr int expander_wake_gpio = 4;
inline constexpr std::uint32_t expander_touch_mask = 1U << 2;
inline constexpr std::uint32_t expander_pmu_mask = 1U << 5;
enum class WakePreparation { ready, touch, pending_irq, io_error };

// Reading the whole input port acknowledges ALL expander interrupts. Read touch
// on both sides of that acknowledgement so a held finger cannot be slept through.
// The caller stops LVGL and owns the shared I2C lock throughout this sequence.
template<class ReadPort, class ReadTouch, class IrqHigh>
WakePreparation prepare_expander_wake(ReadPort read_port, ReadTouch read_touch,
                                      IrqHigh irq_high, std::uint32_t& baseline,
                                      std::uint32_t touch_mask = expander_touch_mask,
                                      std::uint32_t pending_mask = expander_pmu_mask) {
    bool pressed{};
    if (!read_touch(pressed)) return WakePreparation::io_error;
    if (pressed) return WakePreparation::touch;
    if (!read_port(baseline)) return WakePreparation::io_error;
    if (!(baseline & touch_mask)) return WakePreparation::touch;
    // A latched PMU IRQ would hide a later VBUS insertion on the shared line.
    if ((baseline & pending_mask) != pending_mask) return WakePreparation::pending_irq;
    if (!read_touch(pressed)) return WakePreparation::io_error;
    if (pressed) return WakePreparation::touch;
    return irq_high() ? WakePreparation::ready : WakePreparation::pending_irq;
}

enum class ExpanderWake { touch, shared, ambiguous, io_error };
inline ExpanderWake classify_expander_wake(std::uint32_t before, std::uint32_t after,
                                         bool pressed, bool read_ok,
                                         std::uint32_t touch_mask = expander_touch_mask) {
    if (!read_ok) return ExpanderWake::io_error;
    const auto changed = (before ^ after) & 0xffU;
    if (pressed || !(after & touch_mask) || (changed & touch_mask))
        return ExpanderWake::touch;
    // A pulse may have returned to its original level before we read it. Wake
    // conservatively in that case; only a known non-touch change stays dark.
    return changed ? ExpanderWake::shared : ExpanderWake::ambiguous;
}
}
