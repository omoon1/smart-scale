#include "scale.hxx"
#include "weight_display.hxx"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace smart_scale;

int main() {
    // Recorded-data replay can replace these deterministic synthetic signals.
    // Compare identical +/-0.2 g oscillations against the previous fixed EMA.
    for (const unsigned interval : {100U, 13U}) {
        Scale scale; assert(scale.restore({0, 1000}));
        double legacy = 100000, adaptive_energy = 0, legacy_energy = 0;
        std::optional<double> flow_reference;
        unsigned quiet_samples = 0, samples = 0;
        for (unsigned ms = interval; ms <= 20000; ms += interval) {
            const int raw = static_cast<int>(100000 + 200 * std::sin(ms * 0.001 * 6.283185307 / 1.4));
            scale.ingest(raw, ms);
            // Use the same median-of-three input as the production filter.
            if (auto median = scale.recent_gross()) {
                const double alpha = -std::expm1(-static_cast<double>(interval) / 1000.0 / 0.3);
                legacy += alpha * (*median * 1000 - legacy);
                if (!flow_reference) flow_reference = *median;
                else {
                    const double previous = *flow_reference;
                    *flow_reference += alpha * (*median - previous);
                    assert(std::abs(scale.flow() - (*flow_reference - previous) * 1000 / interval) < 0.002);
                }
            }
            if (ms > 10000) {
                adaptive_energy += std::pow(scale.gross() - 100, 2);
                legacy_energy += std::pow(legacy / 1000 - 100, 2);
                quiet_samples += scale.quiet(); ++samples;
            }
        }
        std::cout << interval << " ms: stationary RMS ratio=" << std::sqrt(adaptive_energy / legacy_energy)
                  << " quiet=" << quiet_samples << '/' << samples << '\n';
        assert(std::isfinite(legacy_energy) && legacy_energy > 0);
        assert(adaptive_energy < legacy_energy * 0.65);
        assert(quiet_samples > samples * 8 / 10);
        // Fast response after a quiet period; do not reseed with the new step.
        unsigned ms = 20000 + interval;
        for (; ms <= 20800; ms += interval) scale.ingest(102000, ms);
        assert(scale.gross() > 101.8F && scale.gross() <= 102.0F);
        for (; ms <= 24000; ms += interval) scale.ingest(102000, ms);
        assert(scale.quiet() && std::abs(scale.gross() - 102) < 0.01F);
        // An isolated spike never opens the fast path or contaminates flow.
        const auto before = scale.gross();
        scale.ingest(700000, ms); ms += interval;
        scale.ingest(102000, ms); ms += interval;
        assert(std::abs(scale.gross() - before) < 0.001F);
        assert(std::abs(scale.flow()) < 0.001F);
        // Slow additions are preserved even below the motion threshold.
        const auto start = ms;
        for (; ms < start + 5000; ms += interval)
            scale.ingest(102000 + static_cast<int>((ms - start) * 0.1), ms);
        assert(scale.gross() > 102.35F);
        scale.check_timeout(ms + 1001);
        assert(!scale.filtered_counts() && !scale.ready() && !scale.quiet());
        for (unsigned i = 0; i < 3; ++i) scale.ingest(90000, ms += interval + 1100 * (i == 0));
        assert(scale.ready() && std::abs(scale.gross() - 90) < 0.001F);
    }
    // Uncalibrated acquisition uses count-space filtering; no fabricated grams.
    Scale uncalibrated;
    for (unsigned ms = 100; ms <= 5000; ms += 100) uncalibrated.ingest(-78000, ms);
    assert(uncalibrated.filtered_counts() && !uncalibrated.ready());
    assert(uncalibrated.quiet() && uncalibrated.capture_zero());
    for (unsigned ms = 5100; ms <= 11000; ms += 100) uncalibrated.ingest(-98000, ms);
    assert(uncalibrated.calibrate(20));
    assert(uncalibrated.calibration()->counts_per_gram == -1000);

    // All live readouts share this timed presentation policy. UI redraws with
    // no fresh sensor sample cannot age a candidate into an accepted value.
    WeightDisplay display;
    assert(*display.update(0.0F, 100, true) == 0);
    for (unsigned ms = 200; ms <= 2000; ms += 100)
        assert(*display.update(ms % 200 ? -0.1F : 0.1F, ms, true) == 0);
    assert(*display.update(0.1F, 2100, true) == 0);
    for (unsigned i = 0; i < 100; ++i) assert(*display.update(0.1F, 2100, true) == 0);
    for (unsigned ms = 2200; ms <= 2500; ms += 100) display.update(0.1F, ms, true);
    assert(std::abs(*display.update(0.1F, 2600, true) - 0.1F) < 0.001F);
    for (unsigned ms = 2700; ms <= 3100; ms += 100) display.update(-0.1F, ms, true);
    assert(std::abs(*display.update(-0.1F, 3200, true) + 0.1F) < 0.001F);
    assert(!display.update(std::nullopt, 3300, true));
    assert(*display.update(200.0F, 3400, true) == 200);
    display.reset();
    assert(*display.update(0.0F, 3400, false) == 0); // Tare epoch.
    assert(*display.update(2.0F, 3500, false) == 2); // No extra pour delay.
    assert(*display.update(8.0F, 100, true) == 8); // Timestamp restart resets.
    // Independent gross/net caches cannot influence measurement/calibration.
    Scale real; assert(real.restore({0, 1000}));
    for (unsigned ms = 100; ms <= 6000; ms += 100) real.ingest(100123, ms);
    WeightDisplay gross, net;
    const auto measured = real.gross();
    assert(real.tare());
    assert(*gross.update(measured, real.sample_ms(), true) != *net.update(real.net(), real.sample_ms(), true));
    assert(real.gross() == measured && real.net() == 0);
    std::cout << "Adaptive filtering and timed presentation checks passed\n";
}
