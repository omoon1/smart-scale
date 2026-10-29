#include "session.hxx"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace smart_scale;

int main() {
    for (const float rate : {0.1F, 1.0F, 8.0F}) {
        FlowStartDetector detector;
        for (unsigned ms = 100; ms <= 2000; ms += 100)
            assert(!detector.update(100, 100, ms, true));
        std::optional<FlowStart> started;
        for (unsigned ms = 2100; ms <= 4000 && !started; ms += 100) {
            const float grams = 100 + (ms - 2000) * .001F * rate;
            started = detector.update(grams, grams, ms, false);
            if (!started) assert(!detector.update(grams, grams, ms, false)); // Redraw cannot age confirmation.
        }
        assert(static_cast<bool>(started) == (rate >= 1));
        if (started) assert(std::abs(started->baseline_g - 100) < .001F);
    }
    for (const unsigned interval : {100U, 13U}) {
        for (const float polarity : {1.0F, -1.0F}) {
            Controller c(true);
            assert(c.scale().restore({0, polarity * 1000}));
            assert(c.register_equipment({0, Kind::other, 100.0F, {}}, true));
            std::uint64_t now = 0;
            auto feed = [&](float grams) {
                c.sensor_sample(static_cast<int>(grams * polarity * 1000), now += interval);
                c.tick(interval / 1000.0F);
            };
            for (; now < 3000;) feed(100);
            assert(c.session().phase == Phase::ready);
            assert(!c.session().brew_prepared && std::abs(c.scale().net()) < .001F);
            // A vessel alone must never auto-start, even under real flow.
            for (unsigned i = 1; i <= 2000 / interval; ++i) feed(100 + i * interval * .002F);
            assert(c.session().phase == Phase::ready && !c.session().brew_prepared);
            assert(c.session().net_g > 3); // A missing dose never continually re-tares added liquid.
            for (unsigned i = 0; i < 3000 / interval; ++i) feed(100);
            assert(c.set_dose(18));
            assert(c.session().brew_prepared);
            const auto tare_epoch = c.scale().display_epoch();
            for (unsigned i = 0; i < 1000 / interval; ++i) feed(100);
            assert(c.scale().display_epoch() == tare_epoch); // Never repeatedly tare.
            for (unsigned i = 0; i < 5000 / interval; ++i)
                feed(100 + .2F * std::sin(i * interval * .001 * 6.283185307 / 1.4));
            assert(c.session().phase == Phase::ready); // Noise is not flow.
            for (unsigned i = 0; i < 2000 / interval; ++i) feed(100);
            c.set_auto_brew_enabled(false); // Dialogs block auto-start.
            for (unsigned i = 0; i < 2000 / interval; ++i) feed(100 + i * interval * .002F);
            assert(c.session().phase == Phase::ready);
            c.set_auto_brew_enabled(true);
            for (unsigned i = 0; i < 3000 / interval; ++i) feed(100);
            const auto start = now;
            while (now - start < 3000 && c.session().phase == Phase::ready)
                feed(100 + (now + interval - start) * .002F); // 2 g/s.
            assert(c.session().phase == Phase::brewing);
            assert(now - start >= 1000 && now - start <= 1600);
            assert(c.session().seconds >= 1 && c.session().seconds < 1.5F);
            assert(c.session().net_g > 1); // Confirmation liquid wasn't tared away.
            assert(!c.session().uncertain && c.session().sample_count >= 2);
            for (unsigned i = 0; i < 2000 / interval; ++i) feed(106);
            assert(std::abs(c.session().net_g - 6) < .15F);
            c.finish();
            assert(!c.session().brew_prepared);
            for (unsigned i = 0; i < 2000 / interval; ++i) feed(110);
            assert(c.session().phase == Phase::result);

            Controller beans(true);
            assert(beans.scale().restore({0, polarity * 1000}));
            assert(beans.register_equipment({0, Kind::portafilter, 100.0F, {}}, true));
            assert(beans.set_dose(18));
            for (unsigned ms = interval; ms < 6000; ms += interval)
                beans.sensor_sample(static_cast<int>((100000 + (ms > 3000 ? (ms - 3000) * 2 : 0)) * polarity), ms);
            assert(beans.session().phase == Phase::ready);

            Controller placement(true);
            assert(placement.scale().restore({0, polarity * 1000}));
            assert(placement.register_equipment({0, Kind::other, 100.0F, {}}, true));
            assert(placement.set_dose(18));
            for (unsigned ms = interval; ms < 6000; ms += interval) {
                const float grams = ms < 1000 ? 0 : ms < 3000 ? 100 : 101;
                placement.sensor_sample(static_cast<int>(grams * polarity * 1000), ms);
            }
            assert(placement.session().phase == Phase::ready); // Step then flat.
            assert(placement.session().brew_prepared);
            assert(placement.remove_equipment(equipment_key(placement.equipment()[0])));
            assert(!placement.session().brew_prepared);
            placement.sensor_fault();
            for (unsigned i = 0; i < 2; ++i)
                placement.sensor_sample(static_cast<int>(102000 * polarity), 6100 + i * interval);
            assert(placement.session().phase == Phase::ready);
            Controller no_vessel(true);
            assert(no_vessel.scale().restore({0, polarity * 1000}) && no_vessel.set_dose(18));
            for (unsigned ms = interval; ms < 6000; ms += interval)
                no_vessel.sensor_sample(static_cast<int>((100000 + (ms > 3000 ? (ms - 3000) * 2 : 0)) * polarity), ms);
            assert(!no_vessel.session().brew_prepared && no_vessel.session().phase == Phase::ready);
            std::cout << "Auto brew checks passed dt=" << interval << " polarity=" << polarity << '\n';
        }
    }
}
