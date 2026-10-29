#include "registration_measurement.hxx"
#include <cassert>
#include <iostream>
using namespace smart_scale;

int main() {
    for (const unsigned interval : {100U, 13U}) {
        Scale scale; assert(scale.restore({0, 1000}));
        RegistrationMeasurement reading;
        std::uint64_t now = 0;
        auto feed = [&](int raw) {
            scale.ingest(raw, now += interval);
            return reading.update(scale);
        };
        for (; now < 6000;) feed(100123);
        assert(scale.stable());
        auto snapshot = reading.update(scale);
        assert(snapshot && std::abs(snapshot->grams - 100.123F) < 0.001F);
        // Mild noise exceeds the entry stddev threshold, but is below the exit
        // threshold. Keep the button usable with a recent qualified mean.
        bool used_while_unstable = false;
        for (unsigned i = 0; i < 1200 / interval; ++i) {
            snapshot = feed(100123 + (i % 2 ? 140 : -140));
            assert(snapshot);
            assert(std::abs(snapshot->grams - 100.123F) < 0.03F);
            if (!scale.stable()) {
                used_while_unstable = true;
                assert(!scale.capture_zero() && !scale.tare()); // Strict gates unchanged.
            }
        }
        assert(used_while_unstable);
        // No stable history may be kept indefinitely during continuous noise.
        for (unsigned i = 0; i < 3000 / interval; ++i) snapshot = feed(100123 + (i % 2 ? 140 : -140));
        assert(!snapshot);
        for (; !scale.stable();) feed(100123);
        assert(reading.update(scale));
        // Sustained severe noise invalidates before the age timeout even when
        // both the median motion and mean-shift guards remain below threshold.
        const auto severe_start = now;
        bool rejected_severe = false;
        for (unsigned i = 0; i < 1500 / interval; ++i) {
            if (!feed(100123 + (i % 2 ? 250 : -250))) { rejected_severe = true; break; }
        }
        assert(rejected_severe && now - severe_start < config::registration_snapshot_ms);
        for (unsigned i = 0; i < 6000 / interval; ++i) feed(100123);
        assert(reading.update(scale));
        // A single spike is rejected; two samples establish an actual step.
        feed(500123); assert(reading.update(scale));
        feed(100123); assert(reading.update(scale));
        feed(101123); assert(!feed(101123));
        assert(!reading.update(scale)); // Handler cannot consume the old mean.
        for (unsigned i = 0; i < 6000 / interval; ++i) feed(101123);
        assert(reading.update(scale));
        // Removal and changes of calibration invalidate immediately.
        feed(0); assert(!feed(0));
        for (unsigned i = 0; i < 6000 / interval; ++i) feed(100123);
        assert(reading.update(scale));
        assert(scale.restore({0, 2000}));
        // A new identity cannot reuse old units, although it can freshly
        // qualify the same stable count history in the new calibration.
        snapshot = reading.update(scale);
        assert(!snapshot || std::abs(snapshot->grams - 50.0615F) < 0.001F);
        scale.invalidate(); assert(!reading.update(scale));
        for (unsigned i = 0; i < 6000 / interval; ++i) feed(100123);
        assert(reading.update(scale));
        scale.check_timeout(now + 1001); assert(!reading.update(scale));
        reading.reset(); assert(!reading.update(scale));
        std::cout << "Registration snapshot checks passed at " << interval << " ms\n";
    }
    // No previously qualified sample => no registration through the grace path.
    Scale noisy; assert(noisy.restore({0, 1000}));
    RegistrationMeasurement unqualified;
    for (unsigned ms = 100; ms <= 6000; ms += 100) {
        noisy.ingest(100000 + (ms % 200 ? 140 : -140), ms);
        assert(!unqualified.update(noisy));
    }
}
