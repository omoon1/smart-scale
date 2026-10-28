#include "ui.hxx"
#include "equipment_icons.hxx"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

LV_FONT_DECLARE(weight_font_72);
LV_FONT_DECLARE(equipment_icons_22);
LV_FONT_DECLARE(equipment_icons_32);

namespace smart_scale::ui {
namespace {
static_assert(std::size(equipment_icons) == 20);
const lv_font_t* icon_font(const lv_font_t* font) {
    // Keep built-in fonts immutable and supply the same emoji subset everywhere.
    static lv_font_t f14 = lv_font_montserrat_14, f18 = lv_font_montserrat_18,
                     f22 = lv_font_montserrat_22, f28 = lv_font_montserrat_28;
    for (auto* f : {&f14, &f18, &f22, &f28}) f->fallback = &equipment_icons_22;
    if (font == &lv_font_montserrat_14) return &f14;
    if (font == &lv_font_montserrat_18) return &f18;
    if (font == &lv_font_montserrat_22) return &f22;
    if (font == &lv_font_montserrat_28) return &f28;
    return font;
}
constexpr auto bg = 0x0C1415, panel = 0x172224, border = 0x344345;
constexpr auto ink = 0xF2F6F4, muted = 0xA4B8B4, mint = 0x9DEBCB, amber = 0xFFC36B;
lv_color_t color(unsigned rgb) { return lv_color_hex(rgb); }
std::string number(std::optional<float> value, const char* suffix = "") {
    if (!value) return "?";
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.1f%s", static_cast<double>(*value), suffix);
    return buffer;
}
// LVGL 8 invalidates labels even when the supplied text is unchanged.
void set_text(lv_obj_t* obj, const char* text) {
    if (std::strcmp(lv_label_get_text(obj), text) != 0) lv_label_set_text(obj, text);
}
lv_obj_t* label(lv_obj_t* p, const char* text, int x, int y, int width,
                const lv_font_t* font = &lv_font_montserrat_14, unsigned rgb = ink) {
    auto* obj = lv_label_create(p);
    set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, icon_font(font), 0);
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
    case Kind::cup: return "Container";
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
        registration_icon_ = 0;
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
    weight_ = weight_unit_ = time_ = flow_ = status_ = object_text_ = dose_text_ = target_text_ = chart_ = nullptr;
    dose_value_ = icon_name_ = kind_input_ = chart_max_ = chart_time_ = current_point_ = object_weight_ = nullptr;
    icon_buttons_.fill(nullptr);
    series_ = target_series_ = nullptr;
    rendered_phase_ = controller_.session().phase;
    const auto phase = rendered_phase_;
    if (phase == Phase::ready) {
        button(screen_, controller_.session().mode == Mode::espresso ? "Espresso" : "Pour-over",
               16, 8, 156, 44, Action::toggle_mode);
    } else label(screen_, phase == Phase::brewing ? "Running" : phase == Phase::result ? "Brew complete" : "Reset",
                 20, 19, 230, &lv_font_montserrat_22);
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
    weight_ = label(root, "0.0", 22, 108, 218, &weight_font_72);
    weight_unit_ = label(root, "g", 0, 0, 28, &lv_font_montserrat_22);
    label(root, "Place to weigh.", 24, 222, 250, &lv_font_montserrat_14, muted);
    auto* object = button(root, "", 280, 66, 184, 84, Action::objects);
    object_text_ = label(object, "", 12, 10, 160, &lv_font_montserrat_18);
    lv_label_set_long_mode(object_text_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(object_text_, 23);
    object_weight_ = label(object, "", 12, 40, 160, &lv_font_montserrat_28);
    auto* dose = button(root, "", 280, 158, 184, 84, Action::dose);
    label(dose, "Coffee", 38, 10, 134, &lv_font_montserrat_18);
    // Native LVGL bean silhouette and curved seam; no extra font or image asset.
    auto* bean = lv_obj_create(dose);
    style(bean, ink);
    lv_obj_set_pos(bean, 13, 10);
    lv_obj_set_size(bean, 16, 22);
    lv_obj_set_style_radius(bean, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(bean, 0, 0);
    lv_obj_clear_flag(bean, LV_OBJ_FLAG_CLICKABLE);
    static const lv_point_t seam_points[] = {{10, 1}, {8, 5}, {6, 9}, {6, 13}, {8, 17}, {8, 21}};
    auto* seam = lv_line_create(bean);
    lv_line_set_points(seam, seam_points, 6);
    lv_obj_set_style_line_color(seam, color(panel), 0);
    lv_obj_set_style_line_width(seam, 2, 0);
    lv_obj_set_style_line_rounded(seam, true, 0);
    lv_obj_clear_flag(seam, LV_OBJ_FLAG_CLICKABLE);
    dose_text_ = label(dose, "", 12, 34, 160, &lv_font_montserrat_18);
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
    weight_ = label(root, "", 278, 104, 164, &lv_font_montserrat_48);
    weight_unit_ = label(root, "g", 0, 0, 24, &lv_font_montserrat_14);
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
    const auto weight_text = (s.phase == Phase::ready || (s.baseline_known && !s.uncertain))
        ? number(s.net_g) : "?";
    const lv_font_t* weight_font = s.phase != Phase::ready && s.net_g >= 100.0F ?
        &lv_font_montserrat_32 : &lv_font_montserrat_48;
    if (s.phase == Phase::ready) {
        // Pre-rendered large digits avoid runtime scaling; fit longer readings.
        weight_font = &weight_font_72;
        lv_point_t text_size{};
        lv_txt_get_size(&text_size, weight_text.c_str(), weight_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (text_size.x > 218) weight_font = &lv_font_montserrat_48;
        lv_txt_get_size(&text_size, weight_text.c_str(), weight_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (text_size.x > 218) weight_font = &lv_font_montserrat_32;
    }
    if (weight_ && lv_obj_get_style_text_font(weight_, 0) != weight_font)
        lv_obj_set_style_text_font(weight_, weight_font, 0);
    if (dose_value_) set_text(dose_value_, number(s.dose_g, " g").c_str());
    if (weight_) {
        set_text(weight_, weight_text.c_str());
        lv_point_t size{};
        lv_txt_get_size(&size, weight_text.c_str(), weight_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const auto* unit_font = s.phase == Phase::ready ? &lv_font_montserrat_22 : &lv_font_montserrat_14;
        const int x = (s.phase == Phase::ready ? 22 : 278) + size.x + 6;
        const int y = (s.phase == Phase::ready ? 108 : 104) + weight_font->line_height - weight_font->base_line
                      - unit_font->line_height + unit_font->base_line;
        if (lv_obj_get_x(weight_unit_) != x || lv_obj_get_y(weight_unit_) != y)
            lv_obj_set_pos(weight_unit_, x, y);
    }
    if (s.phase == Phase::ready) {
        const auto& item = s.active_object;
        if (object_text_) {
            set_text(object_text_, item ? item->name.c_str() : "Object");
            set_text(object_weight_, number(item ? item->empty_g : std::nullopt, " g").c_str());
        }
        if (dose_text_) set_text(dose_text_, number(s.dose_g, " g").c_str());
        return;
    }
    if (s.phase != Phase::brewing && s.phase != Phase::result) return;
    if (object_text_) set_text(object_text_,
        s.vessel ? s.vessel->name.c_str() : "Object ?");
    char clock[32];
    const auto secs = static_cast<unsigned>(s.seconds);
    std::snprintf(clock, sizeof(clock), "%02u:%02u", secs / 60, secs % 60);
    set_text(time_, clock);
    // Demo volume estimate: assume liquid density of 1.0 g/ml.
    constexpr float demo_density_g_ml = 1.0F;
    const float flow_ml_s = s.flow_g_s / demo_density_g_ml;
    set_text(flow_, s.phase == Phase::result ? ("Ratio " + (s.ratio() ? "1:" + number(s.ratio()) : "?")).c_str()
        : s.uncertain ? "Flow unavailable" : number(flow_ml_s, " ml/s").c_str());
    std::string status;
    if (s.uncertain) status = "Baseline uncertain - weight unavailable";
    else if (s.target_g && s.net_g >= *s.target_g) status = "Target reached  +" + number(s.net_g - *s.target_g, " g");
    else if (s.flow_g_s == 0 && s.phase == Phase::brewing) status = "Flow paused - timer continues";
    else status = s.mode == Mode::pour_over && s.vessel && s.vessel->kind == Kind::pour_over_set ? "Set mass increase / water added" : "Beverage collected in cup";
    set_text(status_, status.c_str());
    const auto target_text = "Dose " + number(s.dose_g, " g") + "    Target " + number(s.target_g, " g") +
        "    " + (s.phase == Phase::result ? "Delta " : "Left ") +
        number(s.target_g && !s.uncertain ? std::optional<float>{s.phase == Phase::result ? s.net_g - *s.target_g : std::max(0.0F, *s.target_g - s.net_g)} : std::nullopt, " g");
    set_text(target_text_, target_text.c_str());
    const float max_t = std::max(30.0F, s.seconds);
    const float max_g = std::max(s.mode == Mode::espresso ? 60.0F : 350.0F,
                               std::max(s.net_g, s.target_g.value_or(0)) * 1.15F);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_X, 0, static_cast<int>(max_t * 10));
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, static_cast<int>(max_g * 10));
    char axis[24];
    std::snprintf(axis, sizeof(axis), "%.0f g", static_cast<double>(max_g));
    set_text(chart_max_, axis);
    std::snprintf(axis, sizeof(axis), "0 - %.0f", static_cast<double>(max_t));
    set_text(chart_time_, axis);
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
        dose_value_ = label(box, "", 16, 48, 272, &lv_font_montserrat_48);
        lv_obj_set_style_text_align(dose_value_, LV_TEXT_ALIGN_CENTER, 0);
        button(box, "18 g", 16, 112, 84, 70, Action::dose_preset, 180);
        button(box, "20 g", 110, 112, 84, 70, Action::dose_preset, 200);
        button(box, "22 g", 204, 112, 84, 70, Action::dose_preset, 220);
        button(box, "-1 g", 16, 198, 60, 88, Action::dose_adjust, -10);
        button(box, "-0.1", 84, 198, 64, 88, Action::dose_adjust, -1);
        button(box, "+0.1", 156, 198, 64, 88, Action::dose_adjust, 1);
        button(box, "+1 g", 228, 198, 60, 88, Action::dose_adjust, 10);
        for (unsigned i = 0; i < lv_obj_get_child_cnt(box); ++i) {
            auto* child = lv_obj_get_child(box, static_cast<int>(i));
            if (lv_obj_check_type(child, &lv_btn_class)) {
                auto* caption = lv_obj_get_child(child, 0);
                lv_obj_set_style_text_font(caption, &lv_font_montserrat_18, 0);
                lv_obj_center(caption);
            }
        }
    } else if (dialog_ == Dialog::demo) {
        label(box, "Dummy scenarios", 16, 18, 230, &lv_font_montserrat_22);
        const char* names[] = {"Cup + 18.2 g dose", "Unknown object / no baseline", "Portafilter > beans > cup", "V60 / bloom + pours"};
        for (int i = 0; i < 4; ++i) button(box, names[i], 12, 62 + i * 54, 280, 46, Action::choose_scenario, i);
    } else draw_registration(box);
}
void View::draw_registration(lv_obj_t* box) {
    if (registration_step_ == 2) {
        const auto name = std::string(kind_name(registration_kind_)) + " " + equipment_icons[registration_icon_];
        icon_name_ = label(box, name.c_str(), 16, 16, 370, &lv_font_montserrat_22, mint);
        for (unsigned i = 0; i < std::size(equipment_icons); ++i) {
            auto* item = button(box, equipment_icons[i], 16 + (i % 5) * 84, 58 + (i / 5) * 46,
                                76, 44, Action::choose_icon, static_cast<int>(i));
            auto* caption = lv_obj_get_child(item, 0);
            lv_obj_set_style_text_font(caption, &equipment_icons_32, 0);
            lv_obj_center(caption);
            lv_obj_set_style_border_color(item, color(i == registration_icon_ ? mint : border), 0);
            lv_obj_set_style_border_width(item, i == registration_icon_ ? 2 : 1, 0);
            icon_buttons_[i] = item;
        }
        button(box, "Back", 16, 254, 90, 40, Action::register_back);
        label(box, registration_error_.empty() ? "Icon 3/3" : registration_error_.c_str(),
              116, 266, 168, &lv_font_montserrat_14, registration_error_.empty() ? muted : amber);
        button(box, "Save", 290, 254, 144, 40, Action::register_save, 0, true);
        return;
    }
    const auto title = "Register equipment  " + std::to_string(registration_step_ + 1) + "/3";
    label(box, title.c_str(), 16, 14, 370, &lv_font_montserrat_18);
    if (registration_step_ == 0) {
        label(box, "Choose equipment type", 18, 64, 390, &lv_font_montserrat_22);
        kind_input_ = lv_dropdown_create(box);
        lv_dropdown_set_options(kind_input_, "Container\nPortafilter\nPour-over set");
        lv_dropdown_set_selected(kind_input_, static_cast<unsigned>(registration_kind_));
        lv_obj_set_pos(kind_input_, 18, 110); lv_obj_set_size(kind_input_, 414, 50);
    } else {
        label(box, "Empty reference weight", 18, 60, 400, &lv_font_montserrat_22);
        label(box, "156.8 g", 18, 102, 280, &lv_font_montserrat_48);
        label(box, "Simulated reading - no load cell attached", 18, 165, 416, &lv_font_montserrat_14, muted);
        button(box, registration_stable_ ? "Stable reading" : "Unstable - wait", 18, 196, 250, 46, Action::stable, 0, registration_stable_);
    }
    button(box, "Back", 16, 254, 90, 40, Action::register_back);
    auto* next = button(box, "Continue", 290, 254, 144, 40, Action::register_next, 0, true);
    if (registration_step_ == 1 && !registration_stable_) lv_obj_add_state(next, LV_STATE_DISABLED);
}
void View::action(Action a, int value) {
    switch (a) {
    case Action::dose_adjust:
    case Action::dose_preset: {
        if (dialog_ != Dialog::dose || controller_.session().phase != Phase::ready) return;
        const int current = static_cast<int>(std::lround(controller_.session().dose_g.value_or(18.0F) * 10));
        const int tenths = std::clamp(a == Action::dose_preset ? value : current + value,
                                     config::dose_min_tenths, config::dose_max_tenths);
        controller_.set_dose(static_cast<float>(tenths) / 10.0F);
        refresh_values();
        return; // Keep the existing dialog and redraw only changed values.
    }
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
        }
        if (registration_step_ == 0 || (registration_step_ == 1 && registration_stable_)) ++registration_step_;
        break;
    case Action::register_back:
        if (registration_step_ > 0) --registration_step_; else dialog_ = Dialog::objects;
        break;
    case Action::choose_icon: {
        if (registration_step_ != 2 || value < 0 || value >= 20) return;
        const auto previous = registration_icon_;
        registration_icon_ = static_cast<unsigned>(value);
        for (const auto index : {previous, registration_icon_}) {
            lv_obj_set_style_border_color(icon_buttons_[index], color(index == registration_icon_ ? mint : border), 0);
            lv_obj_set_style_border_width(icon_buttons_[index], index == registration_icon_ ? 2 : 1, 0);
        }
        const auto name = std::string(kind_name(registration_kind_)) + " " + equipment_icons[registration_icon_];
        set_text(icon_name_, name.c_str());
        return;
    }
    case Action::register_save: {
        if (registration_step_ != 2 || dialog_ != Dialog::registration) return;
        const auto registration_name = std::string(kind_name(registration_kind_)) + " " + equipment_icons[registration_icon_];
        const char* config = registration_kind_ == Kind::cup ? "Container only" : registration_kind_ == Kind::portafilter ?
            "Clean, with basket" : "Dripper + server + filter";
        if (controller_.register_equipment({0, registration_kind_, registration_name, config, 156.8F, std::nullopt}, registration_stable_))
            dialog_ = Dialog::objects;
        else registration_error_ = "Equipment list full";
        break;
    }
    }
    schedule_draw();
}
}
