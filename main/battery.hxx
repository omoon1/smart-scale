#pragma once
#include <cstdint>
#include <optional>
#include <cstdio>
#include <string>

namespace smart_scale {
struct BatterySnapshot {
    bool valid{}, present{}, external_power{}, charging{}, full{};
    std::optional<unsigned> percent;
    bool estimated{};
    bool source_known{true};
};
// AXP2101 register meanings: status 0x00/0x01, SOC 0xA4.
// Read-only: do not change cell/charger configuration to obtain telemetry.
inline BatterySnapshot decode_axp2101(std::uint8_t status0, std::uint8_t status1,
                                      std::uint8_t percent) {
    BatterySnapshot b;
    b.valid = true;
    b.present = status0 & (1U << 3);
    b.external_power = (status0 & (1U << 5)) && !(status1 & (1U << 3));
    b.charging = b.present && ((status1 >> 5) == 1);
    b.full = b.present && b.external_power && (status1 & 7) == 4;
    if (b.present && percent <= 100) b.percent = percent;
    return b;
}
// Generic 1S Li-ion approximation, not a fuel gauge. Charger STAT is not
// routed to the MCU on Rev1.1; never infer charging from terminal voltage.
inline BatterySnapshot decode_voltage_battery(int mv) {
    BatterySnapshot b;
    if (mv < 3000 || mv > 4350) return b;
    b.valid = b.present = b.estimated = true;
    b.source_known = false;
    constexpr int volts[] = {3000, 3300, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
    constexpr unsigned soc[] = {0, 5, 10, 20, 35, 50, 65, 80, 90, 100};
    b.percent = 100;
    for (unsigned i = 1; i < 10; ++i) {
        if (mv <= volts[i]) {
            b.percent = soc[i-1] + (mv - volts[i-1]) * (soc[i] - soc[i-1]) / (volts[i] - volts[i-1]);
            break;
        }
    }
    return b;
}
inline std::string battery_text(const BatterySnapshot& b) {
    if (!b.valid) return "Battery --";
    if (!b.present) return b.external_power ? "USB" : "No battery";
    std::string value = b.percent ? std::to_string(*b.percent) + "%" : "--%";
    return (b.estimated ? "~" : "") + value;
}
// Charging takes precedence over the low-battery threshold. Unknown/absent
// telemetry stays neutral instead of implying an empty or charging battery.
inline unsigned battery_color(const BatterySnapshot& b) {
    if (b.valid && b.present && b.charging) return 0x55C878;
    if (b.valid && b.present && b.percent && *b.percent < 20) return 0xF06464;
    return 0xA6A6A6;
}
}
