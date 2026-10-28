#include "ui.hxx"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace smart_scale::ui {
namespace {
constexpr auto bg = 0x0C1415, panel = 0x172224, border = 0x344345;
constexpr auto ink = 0xF2F6F4, muted = 0xA4B8B4, mint = 0x9DEBCB, amber = 0xFFC36B;
lv_color_t color(unsigned rgb) { return lv_color_hex(rgb); }
std::string number(std::optional<float> value, const char* suffix = "") {
    if (!value) return "?";
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.1f%s", static_cast<double>(*value), suffix);
    return buffer;
}
lv_obj_t* label(lv_obj_t* p, const char* text, int x, int y, int width,
                const lv_font_t* font = &lv_font_montserrat_14, unsigned rgb = ink) {
    auto* obj = lv_label_create(p);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color(rgb), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
void style(lv_obj_t* obj, unsigned rgb = panel) {
    lv_obj_set_style_bg_color(obj, color(rgb), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, color(border), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
const char* kind_name(Kind kind) {
    switch (kind) {
    case Kind::cup: return "Cup";
    case Kind::portafilter: return "Portafilter";
    case Kind::pour_over_set: return "Pour-over set";
    }
    return "?";
}
}
lv_obj_t* View::button(lv_obj_t* p, const char* text, int x, int y, int w, int h,
                      Action action, int value, bool primary) {
    auto* obj = lv_btn_create(p);
    style(obj, primary ? mint : panel);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    auto* caption = label(obj, text, 0, 0, w - 12, &lv_font_montserrat_14, primary ? bg : ink);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    LV_ASSERT(binding_count_ < bindings_.size());
    auto& binding = bindings_[binding_count_++];
    binding = {this, action, value};
    lv_obj_add_event_cb(obj, clicked, LV_EVENT_CLICKED, &binding);
    return obj;
}
void View::clicked(lv_event_t* e) {
    const auto binding = *static_cast<Binding*>(lv_event_get_user_data(e));
    binding.view->action(binding.action, binding.value);
}
void View::dose_changed(lv_event_t* event) {
    auto& self = *static_cast<View*>(lv_event_get_user_data(event));
    const auto selected = lv_roller_get_selected(lv_event_get_target(event));
    self.controller_.set_dose(static_cast<float>(selected + config::dose_min_tenths) / 10);
    self.refresh_values();
}
void View::schedule_draw() {
    if (!pending_draw_) { pending_draw_ = true; lv_async_call(deferred_draw, this); }
}
void View::deferred_draw(void* p) {
    auto& self = *static_cast<View*>(p);
    self.pending_draw_ = false;
    self.draw();
}
void View::init() { draw(); }
void View::open(Dialog dialog) {
    if (controller_.session().phase == Phase::brewing || controller_.session().phase == Phase::result) return;
    dialog_ = dialog;
    if (dialog == Dialog::registration) {
        registration_step_ = 0;
        registration_stable_ = true;
        registration_kind_ = Kind::cup;
        registration_name_ = "My cup";
        registration_error_.clear();
    }
    schedule_draw();
}
void View::reset() { controller_.reset(); dialog_ = Dialog::none; schedule_draw(); }
void View::update() {
    if (rendered_phase_ != controller_.session().phase) {
        dialog_ = Dialog::none; // Cancel unconfirmed edits when extraction starts.
        schedule_draw();
    }
    if (!pending_draw_) refresh_values();
}
void View::draw() {
    auto* old = screen_;
    screen_ = lv_obj_create(nullptr);
    style(screen_, bg);
    lv_obj_set_style_radius(screen_, 0, 0);
    lv_obj_set_style_border_width(screen_, 0, 0);
    binding_count_ = 0;
    weight_ = time_ = flow_ = status_ = object_text_ = dose_text_ = target_text_ = chart_ = nullptr;
    roller_ = name_input_ = kind_input_ = chart_max_ = chart_time_ = current_point_ = object_weight_ = nullptr;
    series_ = target_series_ = nullptr;
    rendered_phase_ = controller_.session().phase;
    const auto phase = rendered_phase_;
    if (phase == Phase::ready) {
        button(screen_, controller_.session().mode == Mode::espresso ? "Espresso" : "Pour-over",
               16, 8, 156, 44, Action::toggle_mode);
    } else label(screen_, phase == Phase::brewing ? "Extracting" : phase == Phase::result ? "Brew complete" : "Reset",
                 20, 19, 230, &lv_font_montserrat_22);
    if (phase == Phase::ready) label(screen_, "SMART SCALE", 188, 22, 114, &lv_font_montserrat_14, muted);
    label(screen_, "DEMO   82% " LV_SYMBOL_BATTERY_3, 328, 24, 145, &lv_font_montserrat_14, mint);
    if (phase == Phase::brewing || phase == Phase::result) draw_brew(screen_, phase == Phase::result);
    else if (phase == Phase::remove_load) {
        label(screen_, LV_SYMBOL_REFRESH, 214, 82, 60, &lv_font_montserrat_48, amber);
        label(screen_, "Remove objects from the scale", 32, 154, 420, &lv_font_montserrat_22);
        label(screen_, "The current load will not become the empty zero.\nSession cleared. Equipment and demo records retained.",
              32, 192, 420, &lv_font_montserrat_14, muted);
        button(screen_, "Simulate removal", 140, 264, 200, 44, Action::remove, 0, true);
    } else draw_ready(screen_);
    if (dialog_ != Dialog::none) draw_dialog(screen_);
    lv_scr_load(screen_);
    if (old) lv_obj_del(old);
    refresh_values();
}
void View::draw_ready(lv_obj_t* root) {
    label(root, "CURRENT NET WEIGHT", 24, 86, 244, &lv_font_montserrat_14, muted);
    weight_ = label(root, "0.0 g", 22, 116, 250, &lv_font_montserrat_48);
    status_ = label(root, "Ready when you are", 24, 184, 244, &lv_font_montserrat_18, mint);
    label(root, "Place an object. Then brew.", 24, 222, 250, &lv_font_montserrat_14, muted);
    auto* object = button(root, "", 280, 66, 184, 84, Action::objects);
    object_text_ = label(object, "", 12, 10, 160, &lv_font_montserrat_18);
    lv_label_set_long_mode(object_text_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(object_text_, 23);
    object_weight_ = label(object, "", 12, 40, 160, &lv_font_montserrat_28);
    auto* dose = button(root, "", 280, 158, 184, 84, Action::dose);
    dose_text_ = label(dose, "", 12, 10, 160, &lv_font_montserrat_18);
    button(root, controller_.demo_running() ? "Demo running" : "Run demo", 16, 266, 140, 44, Action::run, 0, true);
    button(root, "Scenarios", 164, 266, 110, 44, Action::demo);
    button(root, "Reset", 364, 266, 100, 44, Action::reset);
}
void View::draw_brew(lv_obj_t* root, bool result) {
    // Draw a compact lock without relying on a font glyph absent from LVGL 8.
    auto* shackle = lv_obj_create(root); style(shackle, bg);
    lv_obj_set_pos(shackle, 24, 61); lv_obj_set_size(shackle, 10, 10);
    lv_obj_set_style_radius(shackle, 5, 0); lv_obj_set_style_border_color(shackle, color(muted), 0);
    auto* lock = lv_obj_create(root); style(lock, muted);
    lv_obj_set_pos(lock, 22, 67); lv_obj_set_size(lock, 14, 10); lv_obj_set_style_radius(lock, 2, 0);
    object_text_ = label(root, "", 44, 62, 420, &lv_font_montserrat_14, muted);
    lv_label_set_long_mode(object_text_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(object_text_, 18);
    chart_ = lv_chart_create(root);
    style(chart_, bg);
    lv_obj_set_pos(chart_, 32, 112);
    lv_obj_set_size(chart_, 220, 108);
    lv_obj_set_style_radius(chart_, 0, 0);
    lv_obj_set_style_line_color(chart_, color(border), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart_, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart_, 0, LV_PART_INDICATOR);
    lv_chart_set_type(chart_, LV_CHART_TYPE_SCATTER);
    lv_chart_set_point_count(chart_, 120);
    lv_chart_set_div_line_count(chart_, 3, 4);
    target_series_ = lv_chart_add_series(chart_, color(amber), LV_CHART_AXIS_PRIMARY_Y);
    series_ = lv_chart_add_series(chart_, color(mint), LV_CHART_AXIS_PRIMARY_Y);
    current_point_ = lv_obj_create(root); style(current_point_, mint);
    lv_obj_set_size(current_point_, 7, 7); lv_obj_set_style_radius(current_point_, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(current_point_, LV_OBJ_FLAG_HIDDEN);
    chart_max_ = label(root, "", 32, 97, 62, &lv_font_montserrat_14, muted);
    chart_time_ = label(root, "", 98, 224, 80, &lv_font_montserrat_14, muted);
    label(root, "g", 12, 96, 16, &lv_font_montserrat_14, muted);
    label(root, "0", 18, 217, 20, &lv_font_montserrat_14, muted);
    label(root, "time (s)", 186, 224, 68, &lv_font_montserrat_14, muted);
    weight_ = label(root, "", 278, 104, 190, &lv_font_montserrat_48);
    time_ = label(root, "", 280, 162, 100, &lv_font_montserrat_22);
    flow_ = label(root, "", 280, 192, 180, &lv_font_montserrat_18, mint);
    status_ = label(root, "", 32, 84, 430, &lv_font_montserrat_14, mint);
    target_text_ = label(root, "", 20, 244, 440, &lv_font_montserrat_14, muted);
    if (result) {
        label(root, "Demo record: RAM only", 20, 282, 240, &lv_font_montserrat_14, mint);
        button(root, "Next brew", 322, 270, 142, 44, Action::next, 0, true);
    } else {
        button(root, controller_.session().paused ? "Resume flow" : "Pause flow", 16, 270, 140, 44, Action::pause);
        button(root, "Reset", 164, 270, 96, 44, Action::reset);
        button(root, "Finish", 322, 270, 142, 44, Action::finish, 0, true);
    }
}
void View::refresh_values() {
    const auto& s = controller_.session();
    if (weight_) lv_obj_set_style_text_font(weight_, s.phase != Phase::ready && s.net_g >= 100.0F ?
        &lv_font_montserrat_32 : &lv_font_montserrat_48, 0);
    if (weight_) lv_label_set_text(weight_, (s.phase == Phase::ready || (s.baseline_known && !s.uncertain))
        ? number(s.net_g, " g").c_str() : "? g");
    if (s.phase == Phase::ready) {
        if (status_) lv_label_set_text(status_, s.baseline_known ? "Auto tare complete" : "Waiting for a baseline");
        const auto& item = s.active_object;
        if (object_text_) {
            lv_label_set_text(object_text_, item ? item->name.c_str() : "Object");
            lv_label_set_text(object_weight_, number(item ? item->empty_g : std::nullopt, " g").c_str());
        }
        if (dose_text_) lv_label_set_text(dose_text_, ("Coffee dose\n" + number(s.dose_g, " g")).c_str());
        return;
    }
    if (s.phase != Phase::brewing && s.phase != Phase::result) return;
    if (object_text_) lv_label_set_text(object_text_,
        ((s.vessel ? s.vessel->name : "Object ?") + "  /  Locked during extraction").c_str());
    char clock[32];
    const auto secs = static_cast<unsigned>(s.seconds);
    std::snprintf(clock, sizeof(clock), "%02u:%02u", secs / 60, secs % 60);
    lv_label_set_text(time_, clock);
    lv_label_set_text(flow_, s.phase == Phase::result ? ("Ratio " + (s.ratio() ? "1:" + number(s.ratio()) : "?")).c_str()
        : s.uncertain ? "Flow unavailable" : number(s.flow_g_s, " g/s").c_str());
    std::string status;
    if (s.uncertain) status = "Baseline uncertain - weight unavailable";
    else if (s.target_g && s.net_g >= *s.target_g) status = "Target reached  +" + number(s.net_g - *s.target_g, " g");
    else if (s.flow_g_s == 0 && s.phase == Phase::brewing) status = "Flow paused - timer continues";
    else status = s.mode == Mode::pour_over && s.vessel && s.vessel->kind == Kind::pour_over_set ? "Set mass increase / water added" : "Beverage collected in cup";
    lv_label_set_text(status_, status.c_str());
    const auto target_text = "Dose " + number(s.dose_g, " g") + "    Target " + number(s.target_g, " g") +
        "    " + (s.phase == Phase::result ? "Delta " : "Left ") +
        number(s.target_g && !s.uncertain ? std::optional<float>{s.phase == Phase::result ? s.net_g - *s.target_g : std::max(0.0F, *s.target_g - s.net_g)} : std::nullopt, " g");
    lv_label_set_text(target_text_, target_text.c_str());
    const float max_t = std::max(30.0F, s.seconds);
    const float max_g = std::max(s.mode == Mode::espresso ? 60.0F : 350.0F,
                               std::max(s.net_g, s.target_g.value_or(0)) * 1.15F);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_X, 0, static_cast<int>(max_t * 10));
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, static_cast<int>(max_g * 10));
    char axis[24];
    std::snprintf(axis, sizeof(axis), "%.0f g", static_cast<double>(max_g));
    lv_label_set_text(chart_max_, axis);
    std::snprintf(axis, sizeof(axis), "0 - %.0f", static_cast<double>(max_t));
    lv_label_set_text(chart_time_, axis);
    if (s.sample_count && !s.uncertain) {
        lv_obj_clear_flag(current_point_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(current_point_, 32 + static_cast<int>(218 * s.seconds / max_t) - 3,
                       112 + 106 - static_cast<int>(106 * s.net_g / max_g) - 3);
    } else lv_obj_add_flag(current_point_, LV_OBJ_FLAG_HIDDEN);
    for (std::size_t i = 0; i < 120; ++i) {
        const auto index = s.sample_count > 120 ? i * (s.sample_count - 1) / 119 : i;
        if (index < s.sample_count && !s.uncertain) {
            const auto& p = s.samples[index];
            series_->x_points[i] = static_cast<lv_coord_t>(p.seconds * 10);
            series_->y_points[i] = static_cast<lv_coord_t>(p.grams * 10);
        } else { series_->x_points[i] = LV_CHART_POINT_NONE; series_->y_points[i] = LV_CHART_POINT_NONE; }
        target_series_->x_points[i] = LV_CHART_POINT_NONE;
        target_series_->y_points[i] = LV_CHART_POINT_NONE;
    }
    if (s.target_g) {
        target_series_->x_points[0] = 0;
        target_series_->x_points[1] = static_cast<lv_coord_t>(max_t * 10);
        target_series_->y_points[0] = target_series_->y_points[1] = static_cast<lv_coord_t>(*s.target_g * 10);
    }
    lv_chart_refresh(chart_);
}
void View::draw_dialog(lv_obj_t* root) {
    auto* shade = lv_obj_create(root);
    style(shade, 0x000000);
    lv_obj_set_size(shade, 480, 320);
    lv_obj_set_style_bg_opa(shade, LV_OPA_70, 0);
    auto* box = lv_obj_create(shade);
    style(box);
    lv_obj_set_size(box, dialog_ == Dialog::registration ? 452 : 304, 304);
    lv_obj_center(box);
    const int width = dialog_ == Dialog::registration ? 452 : 304;
    button(box, LV_SYMBOL_CLOSE, width - 52, 4, 44, 44, Action::close);
    if (dialog_ == Dialog::objects) {
        auto* list = lv_obj_create(box);
        style(list);
        lv_obj_set_pos(list, 12, 52);
        lv_obj_set_size(list, 280, 186);
        lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
        for (std::size_t i = 0; i < controller_.equipment_count(); ++i)
            button(list, controller_.equipment()[i].name.c_str(), 0, static_cast<int>(i) * 52, 270, 46,
                   Action::choose_object, static_cast<int>(i));
        button(box, "+ Register equipment", 12, 246, 280, 46, Action::registration, 0, true);
    } else if (dialog_ == Dialog::dose) {
        roller_ = lv_roller_create(box);
        std::string values;
        for (int i = config::dose_min_tenths; i <= config::dose_max_tenths; ++i) {
            if (!values.empty()) values += '\n';
            values += number(static_cast<float>(i) / 10, " g");
        }
        lv_roller_set_options(roller_, values.c_str(), LV_ROLLER_MODE_NORMAL);
        lv_obj_set_style_text_font(roller_, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_font(roller_, &lv_font_montserrat_22, LV_PART_SELECTED);
        lv_roller_set_visible_row_count(roller_, 5);
        lv_obj_set_size(roller_, 220, 176);
        lv_obj_set_pos(roller_, 20, 64);
        lv_obj_set_style_bg_color(roller_, color(bg), 0);
        lv_obj_set_style_text_color(roller_, color(muted), 0);
        lv_obj_set_style_bg_color(roller_, color(mint), LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller_, color(bg), LV_PART_SELECTED);
        const auto selected = static_cast<int>(std::lround(controller_.session().dose_g.value_or(18.0F) * 10)) - 1;
        lv_roller_set_selected(roller_, static_cast<uint16_t>(std::clamp(selected, 0, 599)), LV_ANIM_OFF);
        lv_obj_add_event_cb(roller_, dose_changed, LV_EVENT_VALUE_CHANGED, this);
        label(box, LV_SYMBOL_UP, 258, 112, 28, &lv_font_montserrat_22, muted);
        label(box, LV_SYMBOL_DOWN, 258, 188, 28, &lv_font_montserrat_22, muted);
    } else if (dialog_ == Dialog::demo) {
        label(box, "Dummy scenarios", 16, 18, 230, &lv_font_montserrat_22);
        const char* names[] = {"Cup + 18.2 g dose", "Unknown object / no baseline", "Portafilter > beans > cup", "V60 / bloom + pours"};
        for (int i = 0; i < 4; ++i) button(box, names[i], 12, 62 + i * 54, 280, 46, Action::choose_scenario, i);
    } else draw_registration(box);
}
void View::draw_registration(lv_obj_t* box) {
    const auto title = "Register equipment  " + std::to_string(registration_step_ + 1) + "/4";
    label(box, title.c_str(), 16, 14, 370, &lv_font_montserrat_18);
    if (registration_step_ == 0) {
        label(box, "Choose equipment type", 18, 64, 390, &lv_font_montserrat_22);
        kind_input_ = lv_dropdown_create(box);
        lv_dropdown_set_options(kind_input_, "Cup\nPortafilter\nPour-over set");
        lv_dropdown_set_selected(kind_input_, static_cast<unsigned>(registration_kind_));
        lv_obj_set_pos(kind_input_, 18, 110); lv_obj_set_size(kind_input_, 414, 50);
    } else if (registration_step_ == 1) {
        label(box, kind_name(registration_kind_), 18, 64, 400, &lv_font_montserrat_28, mint);
        const char* desc = registration_kind_ == Kind::cup ? "Cup only: measures beverage collected." :
            registration_kind_ == Kind::portafilter ? "Clean and empty, with the basket installed.\nA different basket needs a new profile." :
            "Dripper + server + filter on the scale.\nUse the actual post-rinse tare for brewing.";
        label(box, desc, 18, 110, 408, &lv_font_montserrat_18);
        label(box, "Configuration is stored with the profile.", 18, 210, 410, &lv_font_montserrat_14, muted);
    } else if (registration_step_ == 2) {
        label(box, "Empty reference weight", 18, 60, 400, &lv_font_montserrat_22);
        label(box, "156.8 g", 18, 102, 280, &lv_font_montserrat_48);
        label(box, "Simulated reading - no load cell attached", 18, 165, 416, &lv_font_montserrat_14, muted);
        button(box, registration_stable_ ? "Stable reading" : "Unstable - wait", 18, 196, 250, 46, Action::stable, 0, registration_stable_);
    } else {
        name_input_ = lv_textarea_create(box);
        lv_textarea_set_one_line(name_input_, true);
        lv_textarea_set_max_length(name_input_, 24);
        lv_textarea_set_accepted_chars(name_input_, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
        lv_textarea_set_text(name_input_, registration_name_.c_str());
        lv_obj_set_pos(name_input_, 16, 52); lv_obj_set_size(name_input_, 418, 42);
        button(box, "Back", 16, 103, 90, 40, Action::register_back);
        button(box, "Save (RAM)", 290, 103, 144, 40, Action::register_save, 0, true);
        auto* kb = lv_keyboard_create(box);
        lv_obj_set_align(kb, LV_ALIGN_TOP_LEFT);
        lv_obj_set_size(kb, 444, 144); lv_obj_set_pos(kb, 3, 155);
        lv_keyboard_set_textarea(kb, name_input_);
        if (!registration_error_.empty()) label(box, registration_error_.c_str(), 112, 112, 176, &lv_font_montserrat_14, amber);
        return;
    }
    button(box, "Back", 16, 254, 90, 40, Action::register_back);
    auto* next = button(box, "Continue", 290, 254, 144, 40, Action::register_next, 0, true);
    if (registration_step_ == 2 && !registration_stable_) lv_obj_add_state(next, LV_STATE_DISABLED);
}
void View::action(Action a, int value) {
    switch (a) {
    case Action::objects: open(Dialog::objects); return;
    case Action::dose: open(Dialog::dose); return;
    case Action::toggle_mode:
        controller_.set_mode(controller_.session().mode == Mode::espresso ? Mode::pour_over : Mode::espresso);
        break;
    case Action::demo: open(Dialog::demo); return;
    case Action::registration: open(Dialog::registration); return;
    case Action::close: dialog_ = Dialog::none; break;
    case Action::choose_object: controller_.select_equipment(static_cast<std::size_t>(value)); dialog_ = Dialog::none; break;
    case Action::choose_scenario: controller_.load_scenario(static_cast<Scenario>(value)); dialog_ = Dialog::none; break;
    case Action::run: controller_.run_demo(); break;
    case Action::pause: controller_.pause_demo(); break;
    case Action::finish: controller_.finish(); break;
    case Action::next: controller_.next(); break;
    case Action::reset: reset(); return;
    case Action::remove: controller_.remove_object(); break;
    case Action::stable: registration_stable_ = !registration_stable_; break;
    case Action::register_next:
        if (registration_step_ == 0 && kind_input_) {
            registration_kind_ = static_cast<Kind>(lv_dropdown_get_selected(kind_input_));
            registration_name_ = std::string("My ") + kind_name(registration_kind_);
        }
        if (registration_step_ != 2 || registration_stable_) ++registration_step_;
        break;
    case Action::register_back:
        if (name_input_) registration_name_ = lv_textarea_get_text(name_input_);
        if (registration_step_ > 0) --registration_step_; else dialog_ = Dialog::objects;
        break;
    case Action::register_save: {
        if (!name_input_) return;
        registration_name_ = lv_textarea_get_text(name_input_);
        const char* config = registration_kind_ == Kind::cup ? "Cup only" : registration_kind_ == Kind::portafilter ?
            "Clean, with basket" : "Dripper + server + filter";
        if (controller_.register_equipment({0, registration_kind_, registration_name_, config, 156.8F, std::nullopt}, registration_stable_))
            dialog_ = Dialog::objects;
        else registration_error_ = "Name empty / list full";
        break;
    }
    }
    schedule_draw();
}
}
