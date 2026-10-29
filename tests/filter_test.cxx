#include "session.hxx"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace smart_scale;
void near(float a, float b, float tolerance = 0.02F) { assert(std::abs(a - b) < tolerance); }
int main() {
    for (const unsigned interval : {100U, 13U}) {
        for (const float polarity : {1.0F, -1.0F}) {
            for (const double period : {1.4, 5.0, 20.0}) {
                Scale fast; assert(fast.restore({0, polarity * 1000}));
                unsigned first_stable = 0;
                for (unsigned ms = interval; ms <= 15000; ms += interval) {
                    const auto raw = static_cast<int>(polarity * (100000 + 200 * std::sin(ms * .001 * 6.283185307 / period)));
                    fast.ingest(raw, ms);
                    if (!first_stable && fast.stable()) first_stable = ms;
                    if (first_stable) assert(fast.stable());
                }
                assert(first_stable && first_stable <= 1000);
                auto now = fast.sample_ms();
                fast.ingest(static_cast<int>(polarity * 110000), now += interval);
                fast.ingest(static_cast<int>(polarity * 110000), now += interval);
                assert(!fast.stable()); // Two samples establish a real step.
                const auto moved = now;
                while (now - moved < 1000) fast.ingest(static_cast<int>(polarity * 110000), now += interval);
                assert(fast.stable());
                assert(std::abs(fast.reference_gross() - 110) < .001F);
                fast.check_timeout(now + 1001); assert(!fast.stable());
            }
        }
    }
    Scale confirmation;
    assert(confirmation.restore({0, 1000}));
    for (unsigned ms = 100; ms <= 800; ms += 100) confirmation.ingest(100000, ms);
    assert(!confirmation.stable());
    confirmation.ingest(100000, 900);
    assert(confirmation.stable());
    confirmation.ingest(101000, 1000); confirmation.ingest(101000, 1100);
    assert(!confirmation.stable() && confirmation.stability_stats().failed != 0);
    confirmation.invalidate();
    assert(confirmation.stability_stats().failed == 1 && confirmation.faults() == 1);
    Scale s;
    assert(s.restore({0, 1000}));
    std::uint64_t t = 0;
    for (unsigned i = 0; i < 20; ++i) s.ingest(100000, t += 13);
    assert(!s.stable()); // Many samples are not a substitute for elapsed settling time.
    for (unsigned i = 0; i < 300; ++i) s.ingest(100000, t += 13);
    assert(s.stable()); near(s.gross(), 100);
    s.ingest(500000, t += 13); // Isolated non-saturating spike rejected by median.
    s.ingest(100000, t += 13);
    near(s.gross(), 100); assert(s.stable());
    const float before = s.gross();
    s.ingest(0, t); s.ingest(0, t - 1); near(s.gross(), before);
    for (unsigned i = 0; i < 100; ++i) s.ingest(100000 + (i % 4 < 2 ? 500 : -500), t += 13);
    assert(!s.stable()); // Low-pass output alone would hide noisy data.
    s.invalidate(); assert(!s.ready() && !s.stable());
    for (unsigned i = 0; i < 60; ++i) s.ingest(200000, t += 100);
    near(s.gross(), 200); assert(s.stable());
    // Slow drift has low spread but must fail the slope limit (0.05 g/s).
    for (unsigned i = 1; i <= 60; ++i) s.ingest(200000 + 10 * i, t += 100);
    assert(!s.stable() && !s.tare() && !s.capture_zero());
    assert((s.stability_stats().failed & 4) != 0);
    assert(s.gross() > 200.5F); near(s.flow(), 0.1F, 0.01F);
    s.ingest(8388607, t += 100); assert(!s.ready());
    s.ingest(0, t += 100); assert(!s.ready());
    s.ingest(0, t += 100); assert(!s.ready());
    s.ingest(0, t += 100); near(s.gross(), 0); assert(!s.stable());
    for (unsigned i = 0; i < 40; ++i) s.ingest(0, t += 100);
    assert(s.stable() && s.capture_zero());
    s.check_timeout(t + 1001); assert(!s.ready());
    t += 2000;
    for (unsigned i = 0; i < 30; ++i) s.ingest(100000, t += 100);
    near(s.gross(), 100); assert(!s.calibrate(100)); // Old captured zero was discarded.

    // Time-constant filtering is comparable at 10 and ~80 SPS, and respects real decreases.
    auto step = [](unsigned dt) {
        Scale model; assert(model.restore({0, 1000}));
        for (unsigned ms = dt; ms <= 2000; ms += dt) model.ingest(0, ms);
        for (unsigned ms = 2000 + dt; ms <= 4000; ms += dt) model.ingest(10000, ms);
        assert(model.gross() > 9.9F && model.gross() <= 10.0F);
        for (unsigned ms = 4000 + dt; ms <= 6000; ms += dt) model.ingest(-10000, ms);
        assert(model.gross() < -9.9F);
        return model.gross();
    };
    near(step(100), step(13), 0.03F);

    // Flow follows sample timestamps even when UI updates are batched/delayed.
    Controller c(true); assert(c.scale().restore({0, 1000}));
    assert(c.register_equipment({0, Kind::cup, "Cup", "", 100.0F, {}}, true));
    assert(c.set_dose(18));
    t = 0;
    for (unsigned i = 0; i < 40; ++i) c.sensor_sample(100000, t += 100);
    c.tick(4); assert(c.start_brew());
    for (unsigned i = 1; i <= 60; ++i) c.sensor_sample(100000 + 100 * i, t += 100);
    c.tick(6);
    near(c.session().flow_g_s, 1.0F, 0.01F);
    assert(c.session().net_g > 5.5F);
    c.sensor_fault(); // Same path used for queue overflow and acquisition gaps.
    assert(c.session().uncertain && !c.scale().ready());
    for (unsigned i = 0; i < 40; ++i) c.sensor_sample(120000, t += 100);
    c.tick(4); assert(c.session().uncertain);
    // Synthetic stationary oscillation matching the reported noise scale, not a
    // replay of the sparse diagnostic logs. Test 10 SPS, ~11 SPS and ~80 SPS.
    for (const unsigned interval : {100U, 89U, 13U}) {
        Scale noisy;
        assert(noisy.restore({0, 1000}));
        unsigned stable_samples = 0, observed = 0;
        for (unsigned ms = interval; ms <= 15000; ms += interval) {
            const auto raw = static_cast<std::int32_t>(263000 + 80 * std::sin(ms * 0.001 * 6.283185307 / 1.4));
            noisy.ingest(raw, ms);
            if (ms > 5000) { ++observed; if (noisy.stable()) ++stable_samples; }
        }
        assert(stable_samples > observed * 9 / 10);
        assert(noisy.stability_stats().drift_span_ms >= config::drift_min_ms);
        // Same noise plus real 0.10 g/s drift must still be rejected.
        for (unsigned ms = 15000 + interval; ms <= 22000; ms += interval) {
            const auto raw = static_cast<std::int32_t>(263000 + (ms - 15000) * 0.1 +
                80 * std::sin(ms * 0.001 * 6.283185307 / 1.4));
            noisy.ingest(raw, ms);
        }
        assert(!noisy.stable());
        assert((noisy.stability_stats().failed & 4) != 0);
        // A sudden load change still fails the short-window checks immediately.
        noisy.ingest(273000, 22100); noisy.ingest(273000, 22200);
        assert(!noisy.stable() && (noisy.stability_stats().failed & (2 | 8)) != 0);
    }
    std::cout << "Timestamp, spike rejection, drift, and recovery tests passed\n";
}
