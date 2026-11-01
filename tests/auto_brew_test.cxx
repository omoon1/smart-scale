#include "session.hxx"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <cstdlib>
using namespace smart_scale;

// Exercise the complete calibrated acquisition/controller path, including median
// filtering, recognition and tare. Both load-cell polarities and sample rates.
template<class Signal>
void scenario(const char* name, unsigned interval, float polarity, Signal signal,
              unsigned duration, bool expect_start, bool jitter = false) {
    Controller c(true);
    assert(c.scale().restore({0, polarity * 1000}));
    assert(c.register_equipment({0, Kind::pour_over_set, 500.0F, {}}, true));
    std::uint64_t now = 0;
    auto feed = [&](float grams, unsigned dt) {
        c.sensor_sample(static_cast<int>(std::lround(grams * polarity * 1000)), now += dt);
        c.tick(dt * .001F);
    };
    while (now < 5000) feed(500, interval);
    assert(c.set_dose(18) && c.session().brew_prepared);
    const auto start = now;
    unsigned count = 0;
    while (now - start < duration && c.session().phase == Phase::ready) {
        const unsigned dt = jitter ? interval + (++count % 3) * interval / 4 : interval;
        feed(500 + signal(static_cast<float>(now + dt - start) / 1000), dt);
    }
    const bool started = c.session().phase == Phase::brewing;
    if (started != expect_start) {
        std::cerr << "FAILED " << name << " interval=" << interval << " polarity=" << polarity << '\n';
        std::abort();
    }
    if (started) {
        assert(c.session().seconds > 0 && c.session().net_g > 0);
        assert(c.session().seconds <= (now - start) * .001F + .001F);
        assert(!c.session().uncertain);
    }
    std::cout << name << " passed dt=" << interval << " polarity=" << polarity
              << " elapsed_ms=" << now - start << '\n';
}
void varied_flows() {
    for (unsigned dt : {100U, 13U}) for (float sign : {1.0F, -1.0F}) {
        scenario("slow_drip_0.1", dt, sign, [](float t) { return .1F * t; }, 6500, true);
        scenario("slow_noisy_0.15", dt, sign, [](float t) { return .15F * t + .015F * std::sin(t * 8); }, 6500, true);
        scenario("fast_pour_30", dt, sign, [](float t) { return 30 * t; }, 3000, true);
        scenario("intermittent_drops", dt, sign, [](float t) { return .16F * std::floor(t / .7F); }, 6500, true);
        scenario("pause_resume", dt, sign, [](float t) { return t < .5F ? t : t < 2 ? .5F : .5F + (t - 2) * 2; }, 6000, true);
        scenario("jittered_samples", dt, sign, [](float t) { return 2 * t; }, 4000, true, true);
        scenario("slow_drift", dt, sign, [](float t) { return .03F * t; }, 20000, false);
        scenario("stationary_noise", dt, sign, [](float t) { return .3F * std::sin(t * 8); }, 10000, false);
        scenario("single_step", dt, sign, [](float t) { return t < .5F ? 0.F : 10.F; }, 10000, false);
        scenario("tap_return", dt, sign, [](float t) { return t > .5F && t < .8F ? 4.F : 0.F; }, 10000, false);
        scenario("small_step", dt, sign, [](float t) { return t < .5F ? 0.F : .7F; }, 10000, false);
        scenario("step_then_flow", dt, sign, [](float t) { return t < .5F ? 0.F : t < 2 ? 10.F : 10.F + 2 * (t - 2); }, 6000, true);
    }
    FlowStartDetector detector;
    assert(!detector.update(100, 100, 100, true));
    assert(!detector.update(101, 101, 300, false));
    assert(!detector.update(102, 102, 2000, false)); // Timeout requires stable rearm.
    for (unsigned ms = 2200; ms < 4000; ms += 200) assert(!detector.update(102 + ms * .001F, 100, ms, false));
    assert(!detector.update(100, 100, 4100, true));
    assert(!detector.update(101, 101, 4300, false));
    assert(!detector.update(102, 102, 4200, false)); // Backwards timestamp discards evidence.
    assert(!detector.update(103, 103, 4500, false));
    assert(!detector.update(std::numeric_limits<float>::quiet_NaN(), 100, 4700, true));
    assert(!detector.update(100, std::numeric_limits<float>::infinity(), 4900, true));
}
int main() {
    varied_flows();
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
                // Match the firmware loop: drain readings, then check timeout.
                c.sensor_sample(std::nullopt, now);
                c.sensor_sample(std::nullopt, now + interval / 2);
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
            const auto result = c.latest_record().session;
            const auto records = c.record_count();
            // Tared net zero with the container present is not physical removal.
            for (unsigned i = 0; i < 1000 / interval; ++i) feed(100);
            assert(c.session().phase == Phase::result);
            assert(c.session().net_g == result.net_g && c.session().seconds == result.seconds);
            c.finish(); // Repeated completion must not duplicate the archive.
            assert(c.record_count() == records);
            c.sensor_fault(); c.tick(600);
            c.sensor_sample(std::nullopt, now += 1000);
            assert(c.session().phase == Phase::result);
            // Brief near-zero readings followed by replacement do not clear it.
            for (unsigned i = 0; i < 200 / interval; ++i) feed(0);
            assert(c.session().phase == Phase::result);
            for (unsigned i = 0; i < 1000 / interval; ++i) feed(110);
            assert(c.session().phase == Phase::result);
            for (unsigned i = 0; i < 1500 / interval; ++i) feed(0);
            assert(c.session().phase == Phase::ready && !c.session().dose_g);
            assert(!c.session().active_object && !c.session().brew_prepared);
            assert(c.record_count() == records && c.latest_record().session.net_g == result.net_g);

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
