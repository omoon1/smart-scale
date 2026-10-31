#pragma once
#include <array>
#include <cstdint>

namespace smart_scale {
// Rev1.1 only. The caller owns the I2C bus/handles and serializes all users.
// A failed write may have reached the IC: mark dirty BEFORE delivery and keep
// the snapshot until reverse-order restoration and readback both succeed.
class PeripheralSleep {
public:
    static constexpr std::uint8_t imu = 0x6b, rtc = 0x51;
    bool needs_restore() const { return dirty_ != 0; }

    template<class Read, class Write>
    bool enter(Read read, Write write) {
        if (needs_restore()) return false;
        std::uint8_t identity{};
        if (!read(imu, 0x00, identity) || identity != 0x05) return false;
        // Snapshot everything before the first mutation. No guessed defaults on
        // missing devices, timeouts or a mismatched IMU identity.
        for (auto& r : registers_)
            if (!read(r.address, r.reg, r.saved)) return false;
        for (unsigned i = 0; i < registers_.size(); ++i) {
            auto& r = registers_[i];
            const auto value = static_cast<std::uint8_t>((r.saved & ~r.clear) | r.set);
            dirty_ |= 1U << i;
            if (!write(r.address, r.reg, value) || !verify(read, r, value)) return false;
        }
        return true;
    }
    template<class Read, class Write>
    bool restore(Read read, Write write) {
        for (unsigned i = registers_.size(); i-- > 0;) {
            if (!(dirty_ & (1U << i))) continue;
            auto& r = registers_[i];
            // PCF85063 AF/TF are write-zero-to-clear. Writing 1 preserves
            // events that arrived after the snapshot, including during sleep.
            const auto value = static_cast<std::uint8_t>(r.saved | r.preserve_flags);
            if (!write(r.address, r.reg, value) || !verify(read, r, value)) return false;
            dirty_ &= ~(1U << i);
        }
        return true;
    }
private:
    struct Register {
        std::uint8_t address, reg, clear, set, preserve_flags, saved{};
    };
    template<class Read>
    static bool verify(Read read, const Register& r, std::uint8_t expected) {
        std::uint8_t actual{};
        return read(r.address, r.reg, actual) &&
            ((actual ^ expected) & static_cast<std::uint8_t>(~r.preserve_flags)) == 0;
    }
    std::array<Register, 5> registers_{{
        // QMI8658 vendor powerDown: disable accel/gyro, then power-down.
        // Also disable INT1/INT2 output enable; restore CTRL1 before CTRL7.
        {imu, 0x08, 0x03, 0x00, 0x00},
        {imu, 0x02, 0x18, 0x02, 0x00},
        // RTC stays running: mask correction/alarm/periodic/timer IRQs and
        // disable CLKOUT. Do not touch date/time, offset or timer countdown.
        {rtc, 0x00, 0x04, 0x00, 0x00},
        {rtc, 0x01, 0xb7, 0x4f, 0x48},
        {rtc, 0x11, 0x02, 0x00, 0x00},
    }};
    unsigned dirty_{};
};
}
