#include "session.hxx"
#include <algorithm>
#include <cmath>

namespace smart_scale {
std::optional<float> Session::ratio() const {
    if (!dose_g || *dose_g <= 0 || !baseline_known || uncertain) return std::nullopt;
    return net_g / *dose_g;
}
Recognition recognize(float total, const Equipment* items, std::size_t count) {
    Recognition result;
    if (!std::isfinite(total) || total < 0) return result;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& item = items[i];
        if (!item.empty_g) continue;
        const auto difference = total - *item.empty_g;
        const bool empty = std::abs(difference) <= config::object_tolerance_g;
        const bool filled = item.kind == Kind::portafilter && item.usual_dose_g &&
            std::abs(difference - *item.usual_dose_g) <= config::combination_tolerance_g + 0.0001F;
        if (!empty && !filled) continue;
        if (result.equipment && result.equipment->id != item.id) return {}; // Ambiguous identity.
        result.equipment = item;
        result.dose_g = filled ? std::optional<float>{difference} : std::nullopt;
    }
    return result;
}
Controller::Controller() {
    equipment_[0] = {1, Kind::cup, "Ceramic cup", "Cup only", 142.6F, std::nullopt};
    equipment_[1] = {2, Kind::portafilter, "Portafilter A", "Clean, with basket", 482.3F, 18.0F};
    equipment_[2] = {3, Kind::pour_over_set, "V60 set", "Dripper + server + filter", 326.4F, 20.0F};
    load_scenario(Scenario::known);
}
const Record& Controller::latest_record() const {
    return records_[(next_record_ + records_.size() - 1) % records_.size()];
}
void Controller::target() {
    session_.target_g.reset();
    if (!session_.dose_g) return;
    if (session_.mode == Mode::espresso) session_.target_g = *session_.dose_g * 2;
    else if (session_.vessel && session_.vessel->kind == Kind::pour_over_set)
        session_.target_g = *session_.dose_g * 15;
}
bool Controller::select_equipment(std::size_t index) {
    if (session_.phase != Phase::ready || index >= equipment_count_) return false;
    place(index);
    return true;
}
void Controller::place(std::size_t index) {
    if (session_.phase != Phase::ready || index >= equipment_count_) return;
    const auto& item = equipment_[index];
    session_.active_object = item;
    if (item.kind == Kind::portafilter) session_.weighing_tool = item;
    else session_.vessel = item;
    session_.loaded = true;
    session_.baseline_known = true; // Stable baseline supplied by the dummy source.
    session_.net_g = 0;
    target();
}
void Controller::remove_object() {
    session_.loaded = false;
    if (session_.phase == Phase::remove_load) session_.phase = Phase::ready;
    if (session_.phase == Phase::brewing) session_.uncertain = true;
    // Session identity, dose and last valid weight survive removal.
}
bool Controller::set_dose(float grams, DoseSource source) {
    if (session_.phase != Phase::ready || !std::isfinite(grams) ||
        grams < 0.1F || grams > 60.0F) return false;
    session_.dose_g = grams;
    session_.dose_source = source;
    target();
    return true;
}
void Controller::weigh_beans(float grams) {
    if (session_.phase != Phase::ready || !session_.active_object ||
        session_.active_object->kind != Kind::portafilter || !session_.loaded) return;
    if (set_dose(grams, DoseSource::measured)) session_.net_g = grams;
}
bool Controller::set_mode(Mode mode) {
    if (session_.phase != Phase::ready) return false;
    session_.mode = mode;
    target();
    return true;
}
bool Controller::register_equipment(Equipment item, bool stable) {
    const auto first = item.name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    item.name = item.name.substr(first, item.name.find_last_not_of(" \t\r\n") - first + 1);
    if (session_.phase != Phase::ready || !stable || equipment_count_ == equipment_.size() ||
        item.name.empty() || item.name.size() > 24 || !item.empty_g ||
        !std::isfinite(*item.empty_g) || *item.empty_g <= 0) return false;
    if (next_equipment_id_ == 0) return false;
    item.id = next_equipment_id_++;
    equipment_[equipment_count_++] = std::move(item);
    return true;
}
bool Controller::remove_equipment(std::uint32_t id) {
    if (session_.phase != Phase::ready) return false;
    std::size_t index = 0;
    while (index < equipment_count_ && equipment_[index].id != id) ++index;
    if (index == equipment_count_) return false;
    for (std::size_t i = index + 1; i < equipment_count_; ++i)
        equipment_[i - 1] = std::move(equipment_[i]);
    equipment_[--equipment_count_] = {};
    for (auto* selected : {&session_.active_object, &session_.vessel, &session_.weighing_tool})
        if (*selected && (*selected)->id == id) selected->reset();
    // Deleting a profile is not a physical removal or a tare. Retain the reading.
    target();
    demo_running_ = false;
    return true;
}
void Controller::place_demo_kind(Kind kind) {
    for (std::size_t i = 0; i < equipment_count_; ++i) {
        if (equipment_[i].kind == kind) { place(i); return; }
    }
    // Demo scenarios must never reinterpret a shifted list index as another type.
    session_.active_object.reset();
    if (kind == Kind::portafilter) session_.weighing_tool.reset();
    else session_.vessel.reset();
    session_.loaded = true;
    session_.baseline_known = false;
    target();
}
void Controller::liquid(float delta, float elapsed) {
    if ((session_.phase != Phase::ready && session_.phase != Phase::brewing) ||
        !std::isfinite(delta) || delta < 0 || !std::isfinite(elapsed) || elapsed <= 0) return;
    if (session_.phase == Phase::ready) {
        if (delta == 0) return;
        session_.phase = Phase::brewing;
        session_.net_g = 0;
        session_.samples[0] = {0, 0};
        session_.sample_count = 1;
    }
    session_.seconds += elapsed;
    session_.uncertain = session_.uncertain || !session_.baseline_known || !session_.loaded;
    session_.flow_g_s = session_.uncertain ? 0 : delta / elapsed;
    if (!session_.uncertain) session_.net_g += delta;
    const Point point{session_.seconds, session_.net_g};
    if (session_.sample_count < session_.samples.size()) {
        session_.samples[session_.sample_count++] = point;
    } else {
        // Keep the origin and decimate history, maintaining actual timestamps.
        for (std::size_t i = 1; i <= 300; ++i) session_.samples[i] = session_.samples[i * 2];
        session_.sample_count = 301;
        session_.samples[session_.sample_count++] = point;
    }
}
void Controller::archive(bool aborted) {
    records_[next_record_] = {session_, aborted};
    next_record_ = (next_record_ + 1) % records_.size();
    record_count_ = std::min(record_count_ + 1, records_.size());
}
void Controller::finish() {
    if (session_.phase != Phase::brewing) return;
    demo_running_ = false;
    session_.phase = Phase::result;
    archive(false);
}
void Controller::reset() {
    if (session_.phase == Phase::brewing) archive(true);
    const bool loaded = session_.loaded;
    const auto mode = session_.mode;
    session_ = {};
    session_.mode = mode;
    session_.loaded = loaded;
    session_.phase = loaded ? Phase::remove_load : Phase::ready;
    demo_running_ = false;
    demo_time_ = 0;
    demo_stage_ = 0;
}
void Controller::next() { reset(); }
void Controller::load_scenario(Scenario scenario) {
    if (session_.phase == Phase::brewing) return;
    session_ = {};
    scenario_ = scenario;
    demo_time_ = 0;
    demo_stage_ = 0;
    demo_running_ = false;
    if (scenario == Scenario::known) {
        place_demo_kind(Kind::cup);
        set_dose(18.2F, DoseSource::measured);
    } else if (scenario == Scenario::pour_over) {
        set_mode(Mode::pour_over);
        place_demo_kind(Kind::pour_over_set);
        set_dose(20.0F, DoseSource::measured);
    } else if (scenario == Scenario::unknown) {
        session_.loaded = true; // No trustworthy empty baseline: show ? throughout.
    }
}
void Controller::run_demo() {
    if (session_.phase == Phase::ready || session_.phase == Phase::brewing) demo_running_ = true;
}
void Controller::pause_demo() { session_.paused = !session_.paused; }
void Controller::tick(float dt) {
    if (!demo_running_ || !std::isfinite(dt) || dt <= 0) return;
    demo_time_ += dt;
    if (scenario_ == Scenario::sequential && session_.phase == Phase::ready) {
        if (demo_stage_ == 0 && demo_time_ >= 1) { place_demo_kind(Kind::portafilter); ++demo_stage_; }
        if (demo_stage_ == 1 && demo_time_ >= 2) { weigh_beans(12); ++demo_stage_; }
        if (demo_stage_ == 2 && demo_time_ >= 3) { weigh_beans(19.2F); ++demo_stage_; }
        if (demo_stage_ == 3 && demo_time_ >= 4) { weigh_beans(18.2F); ++demo_stage_; }
        if (demo_stage_ == 4 && demo_time_ >= 5) { remove_object(); ++demo_stage_; }
        if (demo_stage_ == 5 && demo_time_ >= 6) { place_demo_kind(Kind::cup); ++demo_stage_; }
        if (demo_time_ < 8) return;
    }
    if (demo_time_ < 1) return;
    const float t = session_.seconds;
    float flow = session_.mode == Mode::espresso ? 1.1F + 0.6F * std::sin(t * 0.11F) : 5.0F;
    if (session_.mode == Mode::pour_over && t >= 8 && t < 18) flow = 0; // Bloom pause.
    if (session_.paused) flow = 0;
    liquid(flow * dt, dt); // Target only alerts; never stops the machine/demo.
}
}
