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
        if (!std::isfinite(*item.empty_g) || *item.empty_g <= 0) continue;
        if (std::abs(total - *item.empty_g) > config::object_tolerance_g) continue;
        if (result.equipment && result.equipment->id != item.id) return {std::nullopt, true};
        result.equipment = item; // Mass alone cannot distinguish empty from filled within ±40 g.
    }
    return result;
}
Controller::Controller(bool live) : live_(live) {
    if (live_) { equipment_count_ = 0; return; }
    equipment_[0] = {1, Kind::cup, "Ceramic cup", "Cup only", 142.6F, std::nullopt};
    equipment_[1] = {2, Kind::portafilter, "Portafilter A", "Clean, with basket", 482.3F, 18.0F};
    equipment_[2] = {3, Kind::pour_over_set, "V60 set", "Dripper + server + filter", 326.4F, 20.0F};
    load_scenario(Scenario::known);
}
const char* Controller::recognition_status() const {
    if (!live_) return "demo";
    if (session_.phase != Phase::ready) return "session_locked";
    if (!equipment_count_) return "no_profiles";
    if (!scale_.ready()) return "sensor_unavailable";
    if (session_.active_object) return "matched";
    if (object_ambiguous_) return "ambiguous";
    if (recognition_checked_) return "no_match";
    if (!scale_.stable()) return "waiting_stable";
    return session_.loaded ? "pending" : "empty";
}
void Controller::mark_brew_error(BrewError error) {
    if (session_.phase != Phase::brewing) return;
    if (!session_.uncertain) {
        session_.uncertain = true;
        session_.brew_error = error;
        brew_error_elapsed_s_ = 0;
    }
}
void Controller::resume_measurement() {
    scale_.resume_measurement();
    flow_start_.reset();
    resuming_measurement_ = true;
}
void Controller::sensor_fault() {
    if (!live_) return;
    scale_.invalidate();
    coffee_baseline_g_.reset();
    session_.brew_prepared = false;
    flow_start_.reset();
    empty_since_.reset();
    below_object_since_.reset();
    mark_brew_error(BrewError::sensor_fault);
}
void Controller::sensor_sample(std::optional<std::int32_t> raw, std::uint64_t now) {
    if (!live_) return;
    if (raw) scale_.ingest(*raw, now);
    scale_.check_timeout(now);
    if (resuming_measurement_) {
        if (!scale_.ready() || !scale_.stable()) return;
        resuming_measurement_ = false;
        flow_start_.reset();
    }
    if (auto_brew_epoch_ != scale_.display_epoch()) {
        flow_start_.reset();
        empty_since_.reset(); // A measurement gap/restart cannot complete a removal hold.
    }
    auto_brew_epoch_ = scale_.display_epoch();
    if (session_.phase == Phase::result) {
        // Keep the final metrics immutable until fresh physical removal is confirmed.
        // Net zero (tare), stale samples, and an error timeout cannot clear a result.
        const auto recent = scale_.recent_gross();
        if (!raw || !scale_.ready() || !recent || std::abs(*recent) > 2.0F) {
            empty_since_.reset();
            return;
        }
        if (!empty_since_) empty_since_ = scale_.sample_ms();
        if (scale_.sample_ms() - *empty_since_ >= 300) {
            session_.loaded = false;
            reset();
        }
        return;
    }
    if (!scale_.ready()) {
        coffee_baseline_g_.reset();
        session_.brew_prepared = false;
        flow_start_.reset();
        empty_since_.reset();
        below_object_since_.reset();
        mark_brew_error(BrewError::sensor_fault);
        return;
    }
    // Removal needs only a persistent near-zero median, not a fresh 3.2 s drift
    // window. Otherwise a normal lift/replacement never rearms recognition.
    const auto recent = scale_.recent_gross();
    if (session_.phase == Phase::brewing && session_.removal_threshold_g && recent) {
        // A registered empty mass can differ from today's starting mass. Lock
        // a conservative physical threshold once; never move it up as coffee
        // is added or compare it with beverage net/display values.
        if (*recent < *session_.removal_threshold_g) {
            if (!below_object_since_) below_object_since_ = scale_.sample_ms();
            if (scale_.sample_ms() - *below_object_since_ >= config::brew_removal_hold_ms)
                mark_brew_error(BrewError::object_removed);
        } else below_object_since_.reset();
    }
    if (recent && std::abs(*recent) <= 2.0F) {
        if (!empty_since_) empty_since_ = scale_.sample_ms();
        if (scale_.sample_ms() - *empty_since_ >= 300) {
            session_.loaded = false;
            flow_start_.reset();
            mark_brew_error(BrewError::object_removed);
            if (session_.phase == Phase::remove_load) session_.phase = Phase::ready;
            if (session_.phase == Phase::ready) {
                session_.active_object.reset();
                session_.vessel.reset();
                recognition_checked_ = object_ambiguous_ = false;
                coffee_baseline_g_.reset();
                scale_.clear_tare();
                tared_vessel_id_.reset(); vessel_baseline_g_.reset();
                session_.brew_prepared = false;
            }
            return; // Do not classify residual low-pass weight from the removed object.
        }
    } else empty_since_.reset();
    if (scale_.stable()) {
        session_.loaded = scale_.gross() > 2.0F;
        if (!session_.loaded) {
            if (session_.phase == Phase::remove_load) session_.phase = Phase::ready;
            if (session_.phase == Phase::ready) {
                session_.active_object.reset();
                recognition_checked_ = object_ambiguous_ = false;
                coffee_baseline_g_.reset();
                scale_.clear_tare();
            }
        } else if (session_.phase == Phase::ready && !recognition_checked_) {
            recognition_checked_ = true; // Decide once per stable placement, not while filling.
            session_.vessel.reset(); // Unknown placement must not reuse the previous vessel.
            const auto match = recognize(scale_.reference_gross(), equipment_.data(), equipment_count_);
            object_ambiguous_ = match.ambiguous;
            if (match.equipment) {
                session_.active_object = match.equipment;
                coffee_baseline_g_ = scale_.reference_gross();
                coffee_calibration_ = scale_.calibration();
                if (match.equipment->kind == Kind::portafilter) scale_.set_tare(*coffee_baseline_g_);
                if (match.equipment->kind == Kind::portafilter) session_.weighing_tool = match.equipment;
                else session_.vessel = match.equipment;
            }
            target();
        }
    }
    if (session_.phase == Phase::ready && session_.active_object && !coffee_baseline_g_ && scale_.stable()) {
        coffee_baseline_g_ = scale_.reference_gross();
        coffee_calibration_ = scale_.calibration();
        if (session_.active_object->kind == Kind::portafilter) scale_.set_tare(*coffee_baseline_g_);
    }
    update_brew_preparation();
    const bool eligible = auto_brew_enabled_ && session_.brew_prepared;
    if (!eligible) flow_start_.reset();
    else if (raw && recent) {
        if (auto flow = flow_start_.update(*recent, scale_.reference_gross(), scale_.sample_ms(), scale_.stable())) {
            // Keep the tare taken at vessel recognition, including all liquid
            // added while waiting for flow confirmation or coffee dose entry.
            scale_.set_tare(*vessel_baseline_g_);
            begin_brew(static_cast<float>(scale_.sample_ms() - flow->started_ms) / 1000);
            auto_brew_origin_ms_ = flow->started_ms;
        }
    }
}
void Controller::update_brew_preparation() {
    if (!live_) return;
    session_.brew_prepared = false;
    const auto& object = session_.active_object;
    if (session_.phase != Phase::ready) return;
    const bool vessel = session_.loaded && object && object->kind != Kind::portafilter &&
                        object->empty_g && std::isfinite(*object->empty_g) && *object->empty_g > 0 &&
                        !object_ambiguous_;
    if (!vessel) {
        tared_vessel_id_.reset(); vessel_baseline_g_.reset(); flow_start_.reset(); return;
    }
    const auto calibration = scale_.calibration();
    if (vessel_calibration_ && calibration &&
        (vessel_calibration_->zero != calibration->zero ||
         vessel_calibration_->counts_per_gram != calibration->counts_per_gram)) {
        tared_vessel_id_.reset(); vessel_baseline_g_.reset(); flow_start_.reset();
    }
    if (!tared_vessel_id_ || *tared_vessel_id_ != object->id) {
        flow_start_.reset();
        if (!scale_.tare()) return;
        tared_vessel_id_ = object->id;
        vessel_baseline_g_ = scale_.gross();
        vessel_calibration_ = calibration;
        session_.net_g = 0;
        session_.baseline_known = true;
    }
    if (brew_zero_pending_) {
        // First dose registration establishes the water baseline once. Editing
        // an existing dose must never discard liquid already on the platform.
        if (!scale_.tare()) return;
        vessel_baseline_g_ = scale_.gross();
        session_.net_g = 0;
        brew_zero_pending_ = false;
    }
    session_.brew_prepared = scale_.ready() && session_.dose_g &&
        std::isfinite(*session_.dose_g) && *session_.dose_g >= .1F && *session_.dose_g <= 60 &&
        session_.dose_source != DoseSource::unknown;
    if (!session_.brew_prepared) flow_start_.reset();
}
bool Controller::tare() {
    if (!live_ || session_.phase != Phase::ready || !scale_.tare()) return false;
    session_.net_g = 0;
    coffee_baseline_g_ = scale_.reference_gross();
    coffee_calibration_ = scale_.calibration();
    session_.baseline_known = true;
    flow_start_.reset();
    if (tared_vessel_id_) vessel_baseline_g_ = scale_.gross();
    update_brew_preparation();
    return true;
}
bool Controller::start_brew() {
    if (!live_ || !session_.brew_prepared || !scale_.ready() || !scale_.stable()) return false;
    scale_.set_tare(*vessel_baseline_g_);
    begin_brew();
    return true;
}
void Controller::begin_brew(float elapsed_s) {
    session_.brew_prepared = false;
    flow_start_.reset();
    auto_brew_origin_ms_.reset();
    session_.phase = Phase::brewing;
    session_.baseline_known = true;
    session_.uncertain = false;
    session_.brew_error = BrewError::none;
    session_.empty_reference_g.reset();
    session_.removal_threshold_g.reset();
    const auto& object = session_.active_object;
    if (object && object->empty_g && std::isfinite(*object->empty_g) && *object->empty_g > 0) {
        session_.empty_reference_g = object->empty_g;
        session_.removal_threshold_g = std::min({*object->empty_g, scale_.gross(), scale_.reference_gross()})
                                       - config::brew_removal_margin_g;
    }
    below_object_since_.reset();
    brew_error_elapsed_s_ = 0;
    session_.net_g = session_.seconds = session_.flow_g_s = 0;
    session_.samples[0] = {0, 0};
    session_.sample_count = 1;
    if (elapsed_s > 0) {
        session_.seconds = elapsed_s;
        session_.net_g = scale_.net();
        session_.flow_g_s = scale_.flow();
        session_.samples[1] = {elapsed_s, session_.net_g};
        session_.sample_count = 2;
    }
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
    if (live_) {
        if (!scale_.ready() || scale_.recent_gross().value_or(0) <= 2.0F || !equipment_[index].empty_g) return false;
        recognition_checked_ = true;
        object_ambiguous_ = false;
        const auto& item = equipment_[index];
        if (!session_.active_object || session_.active_object->id != item.id) {
            coffee_baseline_g_ = scale_.stable() ? std::optional<float>{scale_.reference_gross()} : std::nullopt;
            coffee_calibration_ = scale_.calibration();
            if (coffee_baseline_g_ && item.kind == Kind::portafilter) scale_.set_tare(*coffee_baseline_g_);
        }
        session_.active_object = item;
        if (item.kind == Kind::portafilter) session_.weighing_tool = item;
        else session_.vessel = item;
        target();
        update_brew_preparation();
        return true;
    }
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
    session_.baseline_known = true; // Live selection is gated on a stable sensor.
    session_.net_g = 0;
    target();
}
void Controller::remove_object() {
    if (!live_ && session_.phase == Phase::result) {
        session_.loaded = false;
        reset();
        return;
    }
    coffee_baseline_g_.reset(); brew_zero_pending_ = false;
    session_.loaded = false;
    session_.brew_prepared = false;
    flow_start_.reset(); tared_vessel_id_.reset(); vessel_baseline_g_.reset();
    if (session_.phase == Phase::remove_load) session_.phase = Phase::ready;
    mark_brew_error(BrewError::object_removed);
    // Session identity, dose and last valid weight survive removal.
}
std::optional<float> Controller::coffee_candidate() const {
    if (!live_ || session_.phase != Phase::ready || session_.dose_g || !session_.loaded ||
        !session_.active_object || object_ambiguous_ || !coffee_baseline_g_ ||
        !scale_.ready() || !scale_.stable()) return {};
    const auto calibration = scale_.calibration();
    if (!coffee_calibration_ || !calibration || coffee_calibration_->zero != calibration->zero ||
        coffee_calibration_->counts_per_gram != calibration->counts_per_gram) return {};
    const float grams = scale_.reference_gross() - *coffee_baseline_g_;
    // Reject transient movement even during stability's exit debounce.
    const auto recent = scale_.recent_gross();
    if (!recent || std::abs(*recent - scale_.reference_gross()) > config::registration_movement_g ||
        !std::isfinite(grams) || grams < 5.0F || grams > 60.0F) return {};
    return grams;
}
bool Controller::save_coffee() {
    const auto candidate = coffee_candidate();
    return candidate && set_dose(*candidate, DoseSource::measured);
}
void Controller::arm_flow_start() {
    if (!auto_brew_enabled_ || !session_.brew_prepared || !scale_.ready() || !scale_.stable()) return;
    if (const auto recent = scale_.recent_gross()) {
        auto_brew_epoch_ = scale_.display_epoch();
        flow_start_.update(*recent, scale_.reference_gross(), scale_.sample_ms(), true);
    }
}
void Controller::set_auto_brew_enabled(bool enabled) {
    if (auto_brew_enabled_ == enabled) return;
    flow_start_.reset();
    auto_brew_enabled_ = enabled;
    arm_flow_start();
}
bool Controller::set_dose(float grams, DoseSource source) {
    if (session_.phase != Phase::ready || !std::isfinite(grams) ||
        grams < 0.1F || grams > 60.0F) return false;
    if (!session_.dose_g && live_ && session_.active_object &&
        session_.active_object->kind != Kind::portafilter) brew_zero_pending_ = true;
    session_.dose_g = grams;
    session_.dose_source = source;
    flow_start_.reset();
    target();
    update_brew_preparation();
    arm_flow_start();
    return true;
}
void Controller::weigh_beans(float grams) {
    if (session_.phase != Phase::ready || !session_.active_object ||
        session_.active_object->kind != Kind::portafilter || !session_.loaded) return;
    if (set_dose(grams, DoseSource::measured)) session_.net_g = grams;
}
bool Controller::set_brew_guide(const BrewGuide& guide) {
    if (session_.phase != Phase::ready || !guide.valid()) return false;
    session_.guide = guide;
    return true;
}
bool Controller::set_mode(Mode mode) {
    if (session_.phase != Phase::ready) return false;
    if (session_.mode != mode) session_.guide.reset();
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
    if (live_ && !session_.active_object) {
        recognition_checked_ = object_ambiguous_ = false;
    }
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
    if (live_ && !session_.active_object) recognition_checked_ = object_ambiguous_ = false;
    // Deleting a profile is not a physical removal or a tare. Retain the reading.
    target();
    demo_running_ = false;
    update_brew_preparation();
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
    empty_since_.reset();
    archive(false);
}
void Controller::reset() {
    coffee_baseline_g_.reset(); coffee_calibration_.reset(); brew_zero_pending_ = false;
    tared_vessel_id_.reset(); vessel_baseline_g_.reset();
    flow_start_.reset(); auto_brew_origin_ms_.reset();
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
    below_object_since_.reset();
    brew_error_elapsed_s_ = 0;
    if (live_) {
        scale_.clear_tare();
        empty_since_.reset();
        recognition_checked_ = object_ambiguous_ = false;
    }
}
void Controller::next() { reset(); }
void Controller::load_scenario(Scenario scenario) {
    if (live_) return;
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
    if (live_) return;
    if (session_.phase == Phase::ready || session_.phase == Phase::brewing) demo_running_ = true;
}
void Controller::pause_demo() { session_.paused = !session_.paused; }
void Controller::tick(float dt) {
    if (live_) {
        if (!std::isfinite(dt) || dt <= 0) return;
        const bool valid = scale_.ready();
        if (session_.phase == Phase::brewing) {
            if (auto_brew_origin_ms_) {
                session_.seconds = static_cast<float>(scale_.sample_ms() - *auto_brew_origin_ms_) / 1000;
                auto_brew_origin_ms_.reset();
            } else session_.seconds += dt;
            if (!valid) mark_brew_error(BrewError::sensor_fault);
            else if (!session_.loaded) mark_brew_error(BrewError::object_removed);
            if (!session_.uncertain) {
                const float net = scale_.net();
                session_.flow_g_s = scale_.flow();
                session_.net_g = net;
                if (session_.sample_count == session_.samples.size()) {
                    for (std::size_t i = 1; i <= 300; ++i) session_.samples[i] = session_.samples[i * 2];
                    session_.sample_count = 301;
                }
                session_.samples[session_.sample_count++] = {session_.seconds, net};
            } else session_.flow_g_s = 0;
        } else if (session_.phase == Phase::ready && valid) {
            session_.net_g = scale_.net();
        }
        if (session_.uncertain && session_.phase == Phase::brewing) {
            brew_error_elapsed_s_ += static_cast<double>(dt);
            if (brew_error_elapsed_s_ >= config::brew_error_return_s) {
                reset(); // Archive an active failed brew as aborted exactly once.
                // Automatic error recovery goes directly to the main screen,
                // including when a load remains or the sensor is unavailable.
                session_.phase = Phase::ready;
                session_.loaded = scale_.ready() && scale_.recent_gross().value_or(0) > 2.0F;
                if (scale_.ready()) session_.net_g = scale_.net();
            }
        }
        return;
    }
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

