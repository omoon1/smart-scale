#include "session.hxx"
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
    auto feed = [&](int raw) { for (int i = 0; i < 10; ++i) scale.ingest(raw, now += 100); };
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
    for (int i = 0; i < 10; ++i) rebooted.ingest(-78000, 100 * i);
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
    scale.ingest(-79000, now += 100);
    assert(!scale.stable());
    scale.saved(false);
    assert(std::string(scale.status()).find("not saved") != std::string::npos);

    Controller live(true);
    assert(live.equipment_count() == 0 && !live.session().loaded);
    live.run_demo(); live.load_scenario(Scenario::known);
    assert(!live.demo_running() && !live.session().dose_g);
    assert(live.scale().restore({0, 100}));
    auto sample = [&](int raw, int count = 10) {
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
    std::cout << "HX711 calibration and live-session tests passed\n";
}
