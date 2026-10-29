#include "scale.hxx"
#include "weight_display.hxx"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace smart_scale;

int main() {
    // Sensor-window noise must not repeatedly open the fast display path.
    // A settled tenth-gram change is accepted once that window clears.
    for (const bool quiet : {false, true}) {
        WeightDisplay d;
        d.update(0.0F, 100, quiet, true, .12F);
        for (unsigned ms = 200; ms <= 10000; ms += 100)
            assert(*d.update(ms % 1000 < 500 ? -.2F : .2F, ms, quiet, true, .12F) == 0);
        for (unsigned ms = 10100; ms <= 15000; ms += 100)
            d.update(.1F, ms, quiet, true, .01F);
        assert(std::abs(*d.update(.1F, 15100, quiet, true, .01F) - .1F) < .001F);
        assert(*d.update(2.0F, 15200, quiet, true, 100) == 2); // Bounded gate.
    }
    // Both sides of rounding boundaries, positive and negative, including tare.
    // Each excursion lasts >300 ms: the previous policy accepted both sides.
    for (const float center : {0.05F, 100.05F, -100.05F}) {
        WeightDisplay d;
        const float initial = *d.update(center, 100, true);
        for (unsigned ms = 200; ms <= 6000; ms += 100) {
            const float value = center + (ms % 1400 < 700 ? -0.01F : 0.01F);
            assert(*d.update(value, ms, true) == initial);
        }
    }
    // A real full digit added near a rounding boundary must still be visible;
    // the extra boundary margin cannot turn into a permanent dead zone.
    for (const float baseline : {100.049F, 100.051F, -100.049F, -100.051F}) {
        for (const float step : {0.1F, -0.1F}) {
            WeightDisplay d; d.update(baseline, 100, true);
            std::optional<float> shown;
            for (unsigned ms = 200; ms <= 3000; ms += 100) shown = d.update(baseline + step, ms, true);
            assert(std::abs(*shown - std::round((baseline + step) * 10) / 10) < 0.005F);
        }
    }
    // A genuine tenth-gram step survives the zero band, in either direction.
    // Confirmation time is based on fresh samples, not redraw count.
    for (const unsigned interval : {100U, 13U}) {
        for (const float delta : {0.1F, -0.1F, 0.2F, -0.2F}) {
            WeightDisplay d; d.update(0.0F, 100, true);
            unsigned accepted = 0;
            for (unsigned ms = 100 + interval; ms <= 2100; ms += interval) {
                const auto v = d.update(delta, ms, true);
                if (!accepted && std::abs(*v - delta) < 0.001F) accepted = ms - 100;
            }
            assert(accepted >= 1000 && accepted <= 1700);
            std::cout << "quiet step=" << delta << "g dt=" << interval << "ms accepted=" << accepted << "ms\n";
        }
        WeightDisplay moving; moving.update(0.0F, 100, false);
        assert(*moving.update(0.2F, 100 + interval, false) > 0.19F);
        WeightDisplay quiet; quiet.update(0.0F, 100, true);
        assert(*quiet.update(2.0F, 100 + interval, true) == 2.0F);
    }
    WeightDisplay stale; stale.update(0.0F, 100, true);
    stale.update(0.1F, 200, true);
    for (unsigned i = 0; i < 1000; ++i) assert(*stale.update(0.1F, 200, true) == 0);
    assert(!stale.update(std::numeric_limits<float>::quiet_NaN(), 300, true));
    assert(*stale.update(5.0F, 400, true) == 5);
    assert(!stale.update(std::nullopt, 500, true));
    assert(*stale.update(-5.0F, 600, true) == -5);
    assert(*stale.update(8.0F, 1, true) == 8); // Clock restart.
    assert(*stale.update(9.0F, 2000, true) == 9); // Missing history.

    // Full sensor -> adaptive filter -> UI cadence replay around a rounding
    // boundary. Measure displayed transitions, not only internal RMS noise.
    for (const unsigned interval : {100U, 13U}) {
        for (const double period : {1.4, 5.0, 20.0}) {
            Scale scale; assert(scale.restore({0, 1000}));
            WeightDisplay display, baseline;
            unsigned last_ui = 0, changes = 0, baseline_changes = 0;
            std::optional<float> previous, baseline_previous;
            for (unsigned ms = interval; ms <= 70000; ms += interval) {
                const int raw = static_cast<int>(100050 + 200 * std::sin(ms * .001 * 6.283185307 / period));
                scale.ingest(raw, ms);
                if (ms - last_ui < 100) continue;
                last_ui = ms;
                const auto value = scale.ready() ? std::optional<float>{scale.gross()} : std::nullopt;
                const auto shown = display.update(value, scale.sample_ms(), scale.quiet(), true,
                                                   scale.stability_stats().std_counts / 1000);
                const auto old = baseline.update(value, scale.sample_ms(), scale.quiet());
                if (ms > 10000 && shown && previous && *shown != *previous) ++changes;
                if (ms > 10000 && old && baseline_previous && *old != *baseline_previous) ++baseline_changes;
                previous = shown;
                baseline_previous = old;
            }
            std::cout << "replay dt=" << interval << "ms period=" << period << "s display_changes/60s="
                      << baseline_changes << " -> " << changes << '\n';
            assert(changes <= baseline_changes);
            // Slow oscillations remain observable, rather than being hidden as
            // a permanent lock. Fast stationary boundary flicker is suppressed.
            assert(changes <= (period == 1.4 ? 2U : period == 5 ? 48U : 14U));
        }
    }
    std::cout << "Rounding-boundary, small-step, and display replay checks passed\n";
}
