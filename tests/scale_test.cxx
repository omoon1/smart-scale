#include "session.hxx"
#include "weight_display.hxx"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace smart_scale;
void near(float a, float b) { assert(std::abs(a - b) < 0.01F); }
int main() {
    assert(signed_hx711(0) == 0);
    assert(signed_hx711(0x7fffff) == 8388607);
    assert(signed_hx711(0x800000) == -8388608);
    assert(signed_hx711(0xffffff) == -1);
    Scale scale;
    std::uint64_t now = 0;
    auto feed = [&](int raw) { for (int i = 0; i < 60; ++i) scale.ingest(raw, now += 100); };
    assert(!scale.ready() && !scale.tare() && !scale.capture_zero());
    assert(!scale.restore({0, 0}));
    assert(!scale.restore({0, std::numeric_limits<float>::quiet_NaN()}));
    feed(-78000);
    assert(!scale.ready() && scale.stable());
    assert(!scale.calibrate(500)); // No empty baseline captured.
    assert(scale.capture_zero());
    assert(!scale.calibrate(0) && !scale.calibrate(500)); // Unchanged load.
    feed(-511000); // Reverse-polarity cell: -866 counts/g.
    assert(scale.calibrate(500) && scale.ready());
    near(scale.gross(), 500);
    auto calibration = scale.take_pending_save();
    assert(calibration && !scale.take_pending_save());
    near(calibration->counts_per_gram, -866);
    assert(scale.tare()); near(scale.net(), 0); near(scale.gross(), 500);
    feed(-519660); near(scale.net(), 10);
    scale.clear_tare(); near(scale.net(), 510);
    Scale rebooted;
    assert(rebooted.restore(*calibration) && !rebooted.ready());
    for (int i = 0; i < 60; ++i) rebooted.ingest(-78000, 100 * i);
    near(rebooted.net(), 0);
    scale.check_timeout(now + 1001);
    assert(!scale.ready() && !scale.stable() && !scale.tare());
    feed(-78000); assert(scale.ready() && scale.stable());
    assert(scale.capture_zero());
    scale.check_timeout(now + 1001);
    now += 1001;
    feed(-511000); assert(!scale.calibrate(500)); // Restart after disconnect.
    scale.ingest(8388607, now += 100);
    assert(!scale.ready() && !scale.stable());
    feed(-78000);
    for (int i = 0; i < 3; ++i) scale.ingest(-79000, now += 100);
    assert(!scale.stable());
    scale.saved(false);
    assert(std::string(scale.status()).find("not saved") != std::string::npos);

    Controller live(true);
    assert(live.equipment_count() == 0 && !live.session().loaded);
    live.run_demo(); live.load_scenario(Scenario::known);
    assert(!live.demo_running() && !live.session().dose_g);
    assert(live.scale().restore({0, 100}));
    auto sample = [&](int raw, int count = 60) {
        for (int i = 0; i < count; ++i) { live.sensor_sample(raw, now += 100); live.tick(0.1F); }
    };
    sample(0); assert(!live.start_brew()); // No vessel.
    sample(10000); near(live.session().net_g, 100);
    assert(live.tare()); near(live.scale().net(), 0);
    assert(live.set_dose(18)); assert(live.start_brew());
    sample(12000); near(live.session().net_g, 20);
    sample(12000); near(live.session().flow_g_s, 0);
    sample(11990); near(live.session().net_g, 19.9F); // No positive-only noise ratchet.
    assert(!live.tare());
    live.sensor_sample(std::nullopt, now += 1100); live.tick(1.1F);
    assert(live.session().uncertain); near(live.session().net_g, 19.9F);
    sample(13000); assert(live.session().uncertain); // Do not silently resume broken brew.
    live.finish(); assert(live.record_count() == 1 && live.latest_record().session.uncertain);
    const float result = live.session().net_g;
    sample(0); near(live.session().net_g, result);
    live.next(); sample(0); assert(live.session().phase == Phase::ready);
    sample(10000); assert(live.start_brew()); sample(11000);
    sample(0); assert(live.session().uncertain); // Vessel removal invalidates result.
    live.reset(); sample(0);
    assert(live.session().phase == Phase::ready && live.record_count() == 2);
    // Display stabilization leaves the measured value untouched, including tiny additions.
    WeightDisplay display;
    near(*display.update(0.0F), 0);
    for (float noise : {0.1F, -0.1F, 0.14F, -0.14F}) near(*display.update(noise), 0);
    near(*display.update(0.16F), 0.2F);
    near(*display.update(0.10F), 0);
    display.reset();
    near(*display.update(100.04F), 100.0F);
    for (float noise : {100.06F, 99.96F, 100.10F, 99.90F}) near(*display.update(noise), 100.0F);
    near(*display.update(100.16F), 100.2F);
    near(*display.update(100.21F), 100.2F);
    near(*display.update(100.36F), 100.4F); // Slow additions accumulate against held value.
    near(*display.update(100.23F), 100.2F); // Real decreases are not suppressed.
    assert(!display.update(std::nullopt));
    near(*display.update(25.0F), 25.0F);
    assert(!display.update(std::numeric_limits<float>::quiet_NaN()));
    display.reset(); near(*display.update(-0.16F), -0.2F);
    auto epoch = live.scale().display_epoch();
    sample(10000); assert(live.tare()); assert(live.scale().display_epoch() != epoch);
    sample(10010); near(live.session().net_g, 0.1F);
    WeightDisplay zero_display;
    near(*zero_display.update(live.session().net_g), 0);
    near(live.session().net_g, 0.1F); // UI deadband does not erase sensor data.

    Controller detection(true);
    assert(detection.scale().restore({0, 100}));
    assert(detection.register_equipment({0, Kind::other, 150.0F, {}}, true));
    assert(detection.register_equipment({0, Kind::portafilter, 500.0F, 18.0F}, true));
    auto place = [&](int raw) {
        for (int i = 0; i < 60; ++i) { detection.sensor_sample(raw, now += 100); detection.tick(0.1F); }
    };
    place(0); place(11000); // Inclusive lower boundary.
    assert(detection.session().active_object && equipment_key(*detection.session().active_object) == equipment_key(Kind::other, 0));
    near(detection.scale().net(), 0); // Recognized vessel automatically tares once.
    place(19000); assert(equipment_key(*detection.session().active_object) == equipment_key(Kind::other, 0));
    place(0); place(19000); // Inclusive upper boundary.
    assert(equipment_key(*detection.session().active_object) == equipment_key(Kind::other, 0));
    place(0); place(19010); assert(!detection.session().active_object);
    place(18000); assert(!detection.session().active_object); // No recognition while filling an unknown load.
    place(0); place(48000);
    assert(detection.session().active_object->kind == Kind::portafilter);
    assert(!detection.session().dose_g); // ±40 g is not a coffee-dose tolerance.
    assert(detection.tare()); place(49800); near(*detection.session().dose_g, 18);
    assert(detection.session().active_object->kind == Kind::portafilter);
    assert(detection.register_equipment({1, Kind::other, 200.0F, {}}, true));
    place(0); place(17500);
    assert(!detection.session().active_object && detection.object_ambiguous());
    assert(!detection.session().vessel);
    assert(detection.select_equipment(1));
    assert(!detection.object_ambiguous()); near(detection.scale().net(), 0);
    assert(detection.start_brew()); place(20000);
    assert(equipment_key(*detection.session().active_object) == equipment_key(Kind::other, 1)); // Identity locked during brewing.
    Controller registration(true);
    assert(registration.scale().restore({0, 100}));
    assert(std::string(registration.recognition_status()) == "no_profiles");
    auto readings = [&](int raw, int count) {
        for (int i = 0; i < count; ++i) { registration.sensor_sample(raw, now += 100); registration.tick(0.1F); }
    };
    readings(26300, 60);
    assert(!registration.session().active_object);
    assert(registration.register_equipment({0, Kind::other, 263.0F, {}}, true));
    readings(26300, 1); // Register while the already-unknown object remains on the platform.
    assert(registration.session().active_object && equipment_key(*registration.session().active_object) == equipment_key(Kind::other, 0));
    assert(std::string(registration.recognition_status()) == "matched");
    assert(registration.register_equipment({1, Kind::other, 400.0F, {}}, true));
    readings(0, 1); // A single low sample cannot clear identity.
    assert(registration.session().active_object);
    readings(26300, 4);
    assert(registration.session().active_object);
    readings(0, 6); // Ordinary brief removal, much shorter than drift stability.
    assert(!registration.session().active_object && !registration.session().loaded);
    readings(40000, 60);
    assert(registration.session().active_object && equipment_key(*registration.session().active_object) == equipment_key(Kind::other, 1));
    // Deleting an ambiguous candidate rearms matching without another physical lift.
    assert(registration.register_equipment({2, Kind::other, 400.0F, {}}, true));
    readings(0, 6); readings(40000, 60);
    assert(registration.object_ambiguous());
    const auto duplicate_id = equipment_key(registration.equipment()[2]);
    assert(registration.remove_equipment(duplicate_id)); readings(40000, 1);
    assert(registration.session().active_object && !registration.object_ambiguous());
    readings(0, 6); readings(26300, 3);
    assert(!registration.scale().stable());
    assert(registration.select_equipment(0)); // Explicit identity selection need not wait for drift stability.
    assert(equipment_key(*registration.session().active_object) == equipment_key(Kind::other, 0));
    assert(!registration.tare()); // Measurement operations retain the strict stability gate.
    std::cout << "HX711 calibration and live-session tests passed\n";
}
