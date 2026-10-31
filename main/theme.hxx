#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

namespace smart_scale {
// Stable IDs for the future mobile settings transport. Never reorder/reuse IDs.
enum class Theme : std::uint8_t { sage_green = 0, espresso_amber = 1, porcelain_gray = 2 };
constexpr bool valid_theme(Theme theme) { return static_cast<unsigned>(theme) <= 2; }
constexpr std::optional<Theme> parse_theme(std::string_view name) {
    if (name == "sage_green") return Theme::sage_green;
    if (name == "espresso_amber") return Theme::espresso_amber;
    if (name == "porcelain_gray") return Theme::porcelain_gray;
    return std::nullopt;
}
struct Palette { unsigned bg, panel, border, ink, muted, accent, target; };
constexpr Palette palette(Theme theme) {
    switch (theme) {
    case Theme::espresso_amber: return {0x211914, 0x32261E, 0x70523A, 0xFFF3E4, 0xCEB79E, 0xEAB16C, 0xA8C8C1};
    case Theme::porcelain_gray: return {0xF3F3F0, 0xFFFFFF, 0xB8BFBC, 0x252B29, 0x59635F, 0x50565C, 0x68788A};
    default: return {0x0C1415, 0x172224, 0x344345, 0xF2F6F4, 0xA4B8B4, 0x9DEBCB, 0xFFC36B};
    }
}
Theme load_theme();
bool save_theme(Theme theme);
}
