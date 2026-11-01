#pragma once
#include "session.hxx"
#include <bit>
#include <span>

namespace smart_scale::equipment_storage {
// No schema/version or generated IDs: each fixed 11-byte record is
// kind:u8, icon_id:u8, has_dose:u8, empty_g:f32le, usual_dose_g:f32le.
inline constexpr std::size_t record_bytes = 11;
inline std::optional<std::vector<std::uint8_t>> encode(const std::vector<Equipment>& items) {
    if (items.size() > config::max_equipment) return std::nullopt;
    EquipmentMap seen;
    std::vector<std::uint8_t> bytes;
    for (const auto& item : items) {
        if (!valid_equipment(item) || !seen.emplace(equipment_key(item), item).second) return std::nullopt;
        bytes.push_back(static_cast<std::uint8_t>(item.kind));
        bytes.push_back(static_cast<std::uint8_t>(item.id));
        bytes.push_back(item.usual_dose_g.has_value());
        for (const auto value : {*item.empty_g, item.usual_dose_g.value_or(0)}) {
            auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
        }
    }
    return bytes;
}
inline std::optional<std::vector<Equipment>> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() % record_bytes || bytes.size() > config::max_equipment * record_bytes) return std::nullopt;
    std::vector<Equipment> items;
    EquipmentMap seen;
    for (std::size_t pos = 0; pos < bytes.size(); pos += record_bytes) {
        if (bytes[pos + 2] > 1) return std::nullopt;
        const auto read_float = [&](std::size_t offset) {
            std::uint32_t bits = 0;
            for (unsigned i = 0; i < 4; ++i) bits |= static_cast<std::uint32_t>(bytes[pos + offset + i]) << (i * 8);
            return std::bit_cast<float>(bits);
        };
        Equipment item{bytes[pos + 1], static_cast<Kind>(bytes[pos]), read_float(3), std::nullopt};
        if (bytes[pos + 2]) item.usual_dose_g = read_float(7);
        if (!valid_equipment(item) || !seen.emplace(equipment_key(item), item).second) return std::nullopt;
        items.push_back(item);
    }
    return items;
}
}
