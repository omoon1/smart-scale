#include "session.hxx"
#include <cassert>
#include <iostream>
using namespace smart_scale;

int main() {
    // Registration and the next placement can differ by a fraction of a gram.
    // A -0.1 g beverage reading is not evidence that the cup was removed.
    for (const unsigned interval : {100U, 13U}) {
        for (const float polarity : {1.0F, -1.0F}) {
            Controller c(true);
            assert(c.scale().restore({0, polarity * 1000}));
            assert(c.register_equipment({0, Kind::cup, "Cup", "", 100.0F, {}}, true));
            std::uint64_t now = 0;
            auto feed = [&](float grams, unsigned duration) {
                for (unsigned ms = 0; ms < duration; ms += interval) {
                    c.sensor_sample(static_cast<int>(grams * polarity * 1000), now += interval);
                    c.tick(interval / 1000.0F);
                }
            };
            feed(99.6F, 6000);
            assert(c.set_dose(18));
            assert(c.start_brew());
            assert(c.session().removal_threshold_g && *c.session().removal_threshold_g < 98.61F);
            feed(99.5F, 3000);
            assert(!c.session().uncertain);
            assert(c.session().net_g < -0.09F && c.session().net_g > -0.11F);
            for (unsigned i = 0; i < 20; ++i) {
                feed(99.4F, 300); feed(99.8F, 300);
                assert(!c.session().uncertain);
            }
            feed(120, 6000); feed(99.5F, 3000);
            assert(!c.session().uncertain); // Liquid decrease, cup remains.
            feed(90, 1000);
            assert(c.session().brew_error == BrewError::object_removed);
        }
    }
    for (const float polarity : {1.0F, -1.0F}) {
        Controller c(true);
        assert(c.scale().restore({0, polarity * 1000}));
        assert(c.register_equipment({0, Kind::cup, "Cup", "", 100.0F, {}}, true));
        std::uint64_t now = 0;
        auto sample = [&](float grams, unsigned n = 60, bool tick = true) {
            for (unsigned i = 0; i < n; ++i) {
                c.sensor_sample(static_cast<int>(grams * polarity * 1000), now += 100);
                if (tick) c.tick(.1F);
            }
        };
        sample(100);
        assert(c.set_dose(18));
        assert(c.session().active_object && c.start_brew());
        assert(c.session().empty_reference_g == 100.0F);
        sample(120);
        // Reproduce the previous false positive: a >5 g drop between UI ticks,
        // while gross mass remains safely above the registered empty object.
        sample(110, 20, false); c.tick(2);
        assert(!c.session().uncertain && c.session().net_g < 11);
        sample(109.8F); assert(!c.session().uncertain);
        sample(99.8F); assert(!c.session().uncertain); // Empty-object noise margin.
        sample(90, 2); // Median now detects the step; hold is not complete.
        assert(!c.session().uncertain);
        sample(100, 10); assert(!c.session().uncertain); // Brief lift is ignored.
        // A persistent drop below empty mass marks removal even with a nonzero
        // platform reading; it does not require the scale to become empty.
        sample(90, 6, false);
        assert(c.session().uncertain && c.session().brew_error == BrewError::object_removed);
        const auto equipment_count = c.equipment_count();
        c.tick(179); assert(c.session().phase == Phase::brewing);
        c.sensor_fault(); // Another fault cannot restart the countdown.
        c.tick(1);
        assert(c.session().phase == Phase::ready && !c.session().uncertain);
        assert(c.record_count() == 1 && c.latest_record().aborted);
        assert(c.latest_record().session.brew_error == BrewError::object_removed);
        assert(c.equipment_count() == equipment_count && !c.session().dose_g);
        c.tick(180); assert(c.record_count() == 1); // No duplicate archive.

        // Sensor faults remain independent errors and return to ready even
        // when the same physical cup has been put back on the platform.
        sample(100); assert(c.set_dose(18)); assert(c.start_brew());
        c.sensor_fault(); c.tick(100);
        sample(100, 60, false); // Sensor returns; failed brew remains latched.
        assert(c.session().uncertain && c.session().brew_error == BrewError::sensor_fault);
        c.tick(79); assert(c.session().phase == Phase::brewing);
        c.tick(1);
        assert(c.session().phase == Phase::ready && c.session().loaded);
        assert(c.record_count() == 2 && c.latest_record().aborted);
        // Manual Finish on an error screen does not cancel automatic return or
        // create an extra record when that return occurs.
        sample(100); assert(c.set_dose(18)); assert(c.start_brew());
        c.sensor_fault(); c.tick(90); c.finish();
        const auto records = c.record_count();
        c.tick(90);
        assert(c.session().phase == Phase::ready && c.record_count() == records);
    }
    // Neither manual Start nor automatic flow may bypass container registration.
    Controller unknown(true); assert(unknown.scale().restore({0, 1000}));
    assert(unknown.set_dose(18));
    for (unsigned ms = 100; ms <= 6000; ms += 100) unknown.sensor_sample(100000, ms);
    assert(!unknown.start_brew() && !unknown.session().brew_prepared);
    std::cout << "Brew removal threshold and 180-second return checks passed\n";
}
