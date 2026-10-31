#pragma once
#include <array>
#include <cmath>
#include <cstddef>

namespace smart_scale {
struct GuidePoint { float seconds{}, fraction{}; };
// Fractions of the final mass target, with linear pours and flat pause windows.
struct BrewGuide {
    std::array<GuidePoint, 12> points{};
    std::size_t count{};
    bool valid() const {
        if (count < 2 || count > points.size() || points[0].seconds != 0 || points[0].fraction != 0) return false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& p = points[i];
            if (!std::isfinite(p.seconds) || !std::isfinite(p.fraction) || p.seconds < 0 || p.seconds > 1800 ||
                p.fraction < 0 || p.fraction > 1 ||
                (i && (p.seconds <= points[i-1].seconds || p.fraction < points[i-1].fraction))) return false;
        }
        return points[count-1].fraction == 1;
    }
    float duration() const { return points[count-1].seconds; }
    float fraction_at(float seconds) const {
        if (seconds <= 0) return 0;
        for (std::size_t i = 1; i < count; ++i) {
            if (seconds <= points[i].seconds) {
                const auto& a = points[i-1]; const auto& b = points[i];
                return a.fraction + (b.fraction-a.fraction) * (seconds-a.seconds)/(b.seconds-a.seconds);
            }
        }
        return 1;
    }
};
inline BrewGuide default_brew_guide(bool pour_over) {
    if (pour_over) return {{{{0,0}, {30,.2F}, {60,.2F}, {90,.6F}, {120,.6F}, {180,1}}}, 6};
    return {{{{0,0}, {30,1}}}, 2};
}
}
