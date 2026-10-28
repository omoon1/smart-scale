#include "session.hxx"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace smart_scale;
int checks{};
#define CHECK(test) do { ++checks; if (!(test)) { std::cerr << __LINE__ << ": " << #test << '\n'; std::exit(1); } } while (false)
bool near(float a, float b) { return std::abs(a - b) < 0.001F; }
int main() {
    Controller c;
    const auto pf = c.equipment()[1];
    for (float dose : {15.0F, 18.0F, 19.2F, 21.0F}) {
        const auto r = recognize(482.3F + dose, &pf, 1);
        CHECK(r.equipment.has_value()); CHECK(r.dose_g.has_value()); CHECK(near(*r.dose_g, dose));
    }
    for (float dose : {14.9F, 21.1F}) CHECK(!recognize(482.3F + dose, &pf, 1).equipment);
    auto duplicate = pf; duplicate.id = 99;
    const Equipment ambiguous[] = {pf, duplicate};
    CHECK(!recognize(500.3F, ambiguous, 2).equipment);
    CHECK(!recognize(std::numeric_limits<float>::quiet_NaN(), &pf, 1).equipment);

    c.load_scenario(Scenario::sequential);
    c.place(1); c.weigh_beans(12.0F); c.weigh_beans(19.2F); c.weigh_beans(18.2F);
    CHECK(c.session().phase == Phase::ready);
    c.remove_object(); CHECK(near(*c.session().dose_g, 18.2F));
    c.place(0);
    CHECK(c.session().weighing_tool->id == 2); CHECK(c.session().vessel->id == 1);
    CHECK(near(c.session().net_g, 0)); CHECK(near(*c.session().dose_g, 18.2F));
    c.liquid(2, 1);
    CHECK(c.session().phase == Phase::brewing); CHECK(near(c.session().seconds, 1));
    CHECK(!c.select_equipment(1)); CHECK(!c.set_dose(20)); CHECK(!c.set_mode(Mode::pour_over));
    c.liquid(40, 10); CHECK(c.session().phase == Phase::brewing); // No automatic stop.
    c.finish(); CHECK(c.record_count() == 1); CHECK(!c.latest_record().aborted);
    CHECK(near(c.latest_record().session.net_g, 42));
    c.next(); CHECK(!c.session().dose_g); CHECK(c.session().phase == Phase::remove_load);
    c.liquid(1, 1); CHECK(c.session().phase == Phase::remove_load);
    c.remove_object(); CHECK(c.session().phase == Phase::ready);
    CHECK(c.record_count() == 1); CHECK(c.equipment_count() == 3);

    c.load_scenario(Scenario::unknown); c.run_demo();
    for (int i = 0; i < 15; ++i) c.tick(0.1F);
    CHECK(c.session().phase == Phase::brewing); CHECK(!c.session().vessel);
    CHECK(!c.session().dose_g); CHECK(!c.session().ratio()); CHECK(c.session().uncertain);
    c.reset(); CHECK(c.latest_record().aborted); CHECK(c.record_count() == 2);

    c.load_scenario(Scenario::pour_over); c.liquid(40, 8); c.liquid(0, 10);
    CHECK(c.session().phase == Phase::brewing); CHECK(near(c.session().seconds, 18));
    CHECK(near(c.session().net_g, 40));
    c.remove_object(); c.liquid(30, 5);
    CHECK(c.session().uncertain); CHECK(near(c.session().net_g, 40));
    CHECK(c.session().vessel); CHECK(c.session().dose_g);
    c.reset(); c.remove_object();
    c.set_mode(Mode::pour_over); c.place(0); c.set_dose(20);
    CHECK(!c.session().target_g); // Cup-only quantity is not input water.

    Equipment item{0, Kind::cup, "New cup", "Cup only", 156.8F, std::nullopt};
    auto blank = item; blank.name = "   "; CHECK(!c.register_equipment(blank, true));
    CHECK(!c.register_equipment(item, false)); CHECK(c.register_equipment(item, true));
    c.reset(); CHECK(c.equipment_count() == 4);
    c.remove_object();
    CHECK(!c.set_dose(0)); CHECK(!c.set_dose(60.1F));
    CHECK(!c.set_dose(std::numeric_limits<float>::infinity()));
    CHECK(c.set_dose(0.1F)); CHECK(c.set_dose(60.0F));
    while (c.equipment_count() < config::max_equipment) CHECK(c.register_equipment(item, true));
    CHECK(!c.register_equipment(item, true));

    c.load_scenario(Scenario::known);
    for (int i = 0; i < 10000; ++i) c.liquid(0.1F, 0.1F);
    CHECK(c.session().sample_count <= config::max_samples);
    CHECK(c.session().samples[0].seconds == 0);
    for (std::size_t i = 1; i < c.session().sample_count; ++i)
        CHECK(c.session().samples[i].seconds > c.session().samples[i - 1].seconds);
    CHECK(near(c.session().samples[c.session().sample_count - 1].grams, c.session().net_g));
    for (int i = 0; i < 12; ++i) { c.finish(); c.load_scenario(Scenario::known); c.liquid(1, 1); }
    CHECK(c.record_count() == config::max_records);
    std::cout << checks << " checks passed\n";
}
