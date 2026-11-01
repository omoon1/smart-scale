#include "session.hxx"
#include "theme.hxx"
#include <cassert>
#include <limits>
using namespace smart_scale;
int main() {
    auto guide = default_brew_guide(true);
    assert(guide.valid());
    assert(guide.fraction_at(-1) == 0);
    assert(guide.fraction_at(30) == .2F);
    assert(guide.fraction_at(45) == .2F);
    assert(std::abs(guide.fraction_at(75) - .4F) < .0001F);
    assert(guide.fraction_at(180) == 1 && guide.fraction_at(200) == 1);
    auto invalid = guide; invalid.count = 13; assert(!invalid.valid());
    invalid = guide; invalid.points[2].seconds = 30; assert(!invalid.valid());
    invalid = guide; invalid.points[2].fraction = .1F; assert(!invalid.valid());
    invalid = guide; invalid.points[2].seconds = std::numeric_limits<float>::quiet_NaN(); assert(!invalid.valid());
    invalid = guide; invalid.points[5].fraction = .9F; assert(!invalid.valid());
    Controller c;
    c.load_scenario(Scenario::pour_over);
    assert(c.set_brew_guide(guide));
    assert(!c.set_brew_guide(invalid));
    c.run_demo(); c.tick(1);
    assert(c.session().phase == Phase::brewing);
    assert(!c.set_brew_guide(default_brew_guide(false)));
    c.finish();
    assert(c.latest_record().session.brew_guide().count == 6);
    c.reset(); c.remove_object();
    assert(c.set_mode(Mode::espresso));
    assert(c.session().brew_guide().count == 2);
    assert(parse_theme("sage_green") == Theme::sage_green);
    assert(parse_theme("espresso_amber") == Theme::espresso_amber);
    assert(parse_theme("porcelain_gray") == Theme::porcelain_gray);
    assert(!parse_theme("unknown"));
    assert(!valid_theme(static_cast<Theme>(255)));
}
