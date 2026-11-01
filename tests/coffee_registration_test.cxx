#include "session.hxx"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace smart_scale;
struct Fixture {
    Controller c{true};
    std::uint64_t now{};
    unsigned interval;
    float polarity;
    Fixture(unsigned dt, float sign) : interval(dt), polarity(sign) {
        assert(c.scale().restore({0, sign * 1000}));
        assert(c.register_equipment({0, Kind::other, 100.0F, {}}, true));
        assert(c.register_equipment({0, Kind::portafilter, 300.0F, {}}, true));
        assert(c.register_equipment({0, Kind::pour_over_set, 500.0F, {}}, true));
    }
    void sample(float g) { c.sensor_sample(static_cast<int>(std::lround(g * polarity * 1000)), now += interval); c.tick(interval * .001F); }
    void hold(float g) { for (unsigned ms = 0; ms < 5000; ms += interval) sample(g); }
    void flow(float baseline) {
        const auto start = now;
        while (c.session().phase == Phase::ready && now - start < 3000)
            sample(baseline + (now + interval - start) * .002F);
        assert(c.session().phase == Phase::brewing);
        assert(c.session().net_g > 1 && c.session().net_g < 6);
    }
};
int main() {
    for (unsigned dt : {100U, 13U}) for (float sign : {1.0F, -1.0F}) {
        Fixture f(dt, sign);
        auto& c = f.c;
        f.hold(500);
        assert(!c.coffee_candidate() && !c.start_brew());
        f.hold(504.9F); assert(!c.coffee_candidate() && !c.save_coffee());
        f.hold(505); assert(c.coffee_candidate() && std::abs(*c.coffee_candidate() - 5) < .01F);
        f.hold(518); assert(!c.session().dose_g && !c.session().brew_prepared);
        assert(c.save_coffee());
        assert(c.session().dose_source == DoseSource::measured);
        assert(std::abs(*c.session().dose_g - 18) < .01F);
        assert(!c.save_coffee() && !c.coffee_candidate());
        assert(c.session().brew_prepared && std::abs(c.scale().net()) < .01F);
        const auto epoch = c.scale().display_epoch();
        assert(c.set_dose(20)); // Edit does not tare or overwrite from scale.
        f.hold(518);
        assert(c.scale().display_epoch() == epoch && c.session().dose_g == 20);
        const auto object_before = c.session().active_object->id;
        const auto faults_before = c.scale().faults();
        c.resume_measurement();
        f.now += 360000; // sleep does not become a sensor timeout or brew duration
        assert(!c.scale().ready());
        f.hold(518);
        assert(c.scale().ready() && c.scale().stable());
        assert(c.scale().faults() == faults_before);
        assert(c.session().phase == Phase::ready && c.session().dose_g == 20);
        assert(c.session().active_object->id == object_before);
        assert(std::abs(c.scale().net()) < .01F);
        f.flow(518); f.hold(528);
        assert(std::abs(c.session().net_g - 10) < .02F); // Coffee excluded, all water retained.
        assert(!c.save_coffee() && !c.set_dose(22));
        c.reset(); assert(!c.session().dose_g && !c.coffee_candidate());
        f.hold(0); f.hold(300); f.hold(318);
        assert(c.save_coffee() && !c.session().brew_prepared && !c.start_brew());
        f.hold(0); assert(c.session().dose_g && !c.coffee_candidate());
        f.hold(100); assert(c.session().brew_prepared);
        f.flow(100); // Portafilter dose survives transfer to a cup.

        Fixture immediate(dt, sign);
        immediate.hold(500); immediate.hold(518);
        assert(immediate.c.save_coffee());
        immediate.flow(518); // No extra stationary sample after the tap is required.

        Fixture invalid(dt, sign);
        invalid.hold(500); invalid.hold(561);
        assert(!invalid.c.coffee_candidate() && !invalid.c.save_coffee());
        invalid.hold(518); assert(invalid.c.coffee_candidate());
        invalid.c.sensor_fault(); assert(!invalid.c.save_coffee());
        invalid.hold(518); assert(!invalid.c.coffee_candidate()); // Reconnect must establish a new reference.
        invalid.hold(0); invalid.hold(500); invalid.hold(518);
        assert(invalid.c.coffee_candidate());
        assert(invalid.c.scale().restore({1000, sign * 1000}));
        assert(!invalid.c.save_coffee()); // Never subtract references from different calibrations.
        std::cout << "Coffee registration checks passed dt=" << dt << " polarity=" << sign << '\n';
    }
}
