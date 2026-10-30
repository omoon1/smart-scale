#pragma once
#include <cstdint>
#include <optional>
#include <cstdio>
#include <string>

namespace smart_scale {
struct BatterySnapshot {
    bool valid{}, present{}, external_power{}, charging{}, full{};
    std::optional<unsigned> percent;
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
inline std::string battery_text(const BatterySnapshot& b) {
    if (!b.valid) return "Battery --";
    if (!b.present) return b.external_power ? "USB" : "No battery";
    std::string value = b.percent ? std::to_string(*b.percent) + "%" : "--%";
    if (b.charging) return value + " Charging";
    if (b.full) return value + " Full";
    return value + (b.external_power ? " USB" : " Battery");
}
}
