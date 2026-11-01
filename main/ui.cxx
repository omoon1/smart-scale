#include "ui.hxx"
#include "equipment_icons.hxx"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "diagnostic_logger.hxx"
#endif
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
// The device has one LVGL display, serialized by its UI task.
unsigned bg, panel, border, ink, muted, mint, amber, finish_bg, finish_ink;
void apply_palette(Theme theme) {
    const auto p = palette(theme);
    bg = p.bg; panel = p.panel; border = p.border; ink = p.ink;
    muted = p.muted; mint = p.accent; amber = p.target;
    finish_bg = theme == Theme::espresso_amber ? 0xC69C72 :
                theme == Theme::porcelain_gray ? 0x65716D : 0x88AA9B;
    finish_ink = theme == Theme::porcelain_gray ? 0xFFFFFF : bg;
}
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
void pulse_coffee(void* obj, int32_t opacity) {
    lv_obj_set_style_border_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(opacity), 0);
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
// Reserve a separate action rail without scaling fonts or vertical touch targets.
void fit_width(lv_obj_t* parent, int numerator, int denominator) {
    lv_obj_update_layout(parent);
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
        auto* child = lv_obj_get_child(parent, i);
        const int x = lv_obj_get_x(child), width = lv_obj_get_width(child);
        fit_width(child, numerator, denominator);
        lv_obj_set_x(child, x * numerator / denominator);
        lv_obj_set_width(child, width * numerator / denominator);
    }
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
    lv_obj_set_style_bg_color(obj, color(panel), LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DISABLED);
    auto* caption = label(obj, text, 0, 0, w - 12, &lv_font_montserrat_14, primary ? bg : ink);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    LV_ASSERT(binding_count_ < bindings_.size());
    auto& binding = bindings_[binding_count_++];
    binding = {this, action, value};
    if (action == Action::dose_adjust) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(obj, dose_pressed, LV_EVENT_SHORT_CLICKED, &binding);
        lv_obj_add_event_cb(obj, dose_pressed, LV_EVENT_LONG_PRESSED, &binding);
        lv_obj_add_event_cb(obj, dose_pressed, LV_EVENT_LONG_PRESSED_REPEAT, &binding);
    } else lv_obj_add_event_cb(obj, clicked, LV_EVENT_CLICKED, &binding);
    return obj;
}
void View::action_rail(lv_obj_t* root, const char* text, Action action) {
    const int width = lv_disp_get_hor_res(nullptr);
    const int rail = width * 18 / 100;
    auto* control = button(root, text, width - rail + 8, 8, rail - 16,
                           lv_disp_get_ver_res(nullptr) - 16, action);
    const bool finish = action == Action::finish;
    lv_obj_set_style_bg_color(control, color(finish ? finish_bg : panel), 0);
    lv_obj_set_style_bg_color(control, color(border), LV_STATE_PRESSED);
    auto* caption = lv_obj_get_child(control, 0);
    lv_obj_set_style_text_color(caption, color(finish ? finish_ink : ink), 0);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_18, 0);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, 18);
    auto* icon = label(control, finish ? LV_SYMBOL_OK : LV_SYMBOL_LEFT, 0, 0, rail - 16,
                       &lv_font_montserrat_28, finish ? finish_ink : ink);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -16);
    // A drag from a neighboring control must not release as a rail click.
    lv_obj_clear_flag(control, LV_OBJ_FLAG_PRESS_LOCK);
}
void View::clicked(lv_event_t* e) {
    const auto binding = *static_cast<Binding*>(lv_event_get_user_data(e));
    binding.view->action(binding.action, binding.value);
}
void View::dose_pressed(lv_event_t* e) {
    const auto binding = *static_cast<Binding*>(lv_event_get_user_data(e));
    // Native input repeat ends on release/press-lost; no background timer survives.
    if (!binding.view->pending_draw_)
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
void View::init() { theme_ = load_theme(); draw(); }
bool View::set_theme(Theme theme) {
    if (!valid_theme(theme)) return false;
    if (theme == theme_) return true;
    if (!save_theme(theme)) return false;
    theme_ = theme;
    schedule_draw();
    return true;
}
void View::open(Dialog dialog) {
    if (dialog != Dialog::battery_history &&
        (controller_.session().phase == Phase::brewing || controller_.session().phase == Phase::result)) return;
    dialog_ = dialog;
    controller_.set_auto_brew_enabled(dialog == Dialog::none || dialog == Dialog::battery_history);
    if (dialog == Dialog::registration) {
        registration_measurement_.reset();
        registration_step_ = 0;
        registration_stable_ = true;
        registration_kind_ = Kind::cup;
        registration_icon_ = 0;
        registration_error_.clear();
    }
    schedule_draw();
}
void View::reset() { controller_.scale().cancel_calibration(); controller_.reset(); dialog_ = Dialog::none; schedule_draw(); }
void View::update() {
    controller_.set_auto_brew_enabled(dialog_ == Dialog::none || dialog_ == Dialog::battery_history);
    if (rendered_phase_ != controller_.session().phase) {
        dialog_ = Dialog::none; // Cancel unconfirmed edits when extraction starts.
        schedule_draw();
    }
    if (!pending_draw_) refresh_values();
}
void View::draw() {
    apply_palette(theme_);
    auto* old = screen_;
    screen_ = lv_obj_create(nullptr);
    style(screen_, bg);
    lv_obj_set_style_radius(screen_, 0, 0);
    lv_obj_set_style_border_width(screen_, 0, 0);
    binding_count_ = 0;
    battery_label_ = nullptr;
    history_plot_ = nullptr; history_revision_ = UINT64_MAX;
    weight_ = weight_unit_ = time_ = flow_ = status_ = object_text_ = dose_text_ = target_text_ = chart_ = nullptr;
    dose_value_ = icon_name_ = kind_input_ = chart_max_ = chart_time_ = current_point_ = object_weight_ = nullptr;
    tare_button_ = weight_hint_ = object_pill_ = coffee_pill_ = nullptr;
    coffee_card_ = coffee_hint_ = coffee_check_ = object_card_ = object_check_ = stage_text_ = nullptr;
    coffee_visual_state_ = -1;
    object_visual_selected_ = false;
    calibration_message_ = calibration_weight_label_ = registration_weight_label_ = registration_status_ = registration_next_ = nullptr;
    icon_buttons_.fill(nullptr);
    series_ = target_series_ = nullptr;
    guide_ticks_.fill(nullptr);
    rendered_phase_ = controller_.session().phase;
    const auto phase = rendered_phase_;
    if (wide()) draw_wide(screen_);
    else {
        if (phase == Phase::ready) {
            button(screen_, controller_.session().mode == Mode::espresso ? "Espresso" : "Pour-over",
                   16, 8, 156, 44, Action::toggle_mode);
        } else label(screen_, phase == Phase::brewing ? "Running" : phase == Phase::result ? "Brew complete" : "Reset",
                     20, 19, 230, &lv_font_montserrat_22);
        if (phase == Phase::brewing) label(screen_, equipment_icons[9], 280, 19, 32, &lv_font_montserrat_22, mint);

        if (phase == Phase::brewing || phase == Phase::result) draw_brew(screen_, phase == Phase::result);
        else if (phase == Phase::remove_load) {
            label(screen_, LV_SYMBOL_REFRESH, 214, 82, 60, &lv_font_montserrat_48, amber);
            label(screen_, "Remove objects from the scale", 32, 154, 420, &lv_font_montserrat_22);
            label(screen_, "The current load will not become the empty zero.\nSession cleared. Equipment and records retained.",
                  32, 192, 420, &lv_font_montserrat_14, muted);
            if (!controller_.live()) button(screen_, "Simulate removal", 140, 264, 200, 44, Action::remove, 0, true);
        } else draw_ready(screen_);
    }
    auto* battery_button = button(screen_, "", wide() ? 518 : 344, wide() ? 0 : 8,
                                  wide() ? 114 : 120, wide() ? 34 : 44, Action::battery_history);
    lv_obj_set_style_border_width(battery_button, 0, 0);
    lv_obj_set_style_bg_opa(battery_button, LV_OPA_TRANSP, 0);
    battery_label_ = lv_obj_get_child(battery_button, 0);
    lv_obj_set_style_text_font(battery_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(battery_label_, LV_TEXT_ALIGN_RIGHT, 0);
    if (phase == Phase::brewing) {
        lv_obj_set_x(battery_button, wide() ? 410 : 274);
        lv_obj_set_width(battery_button, wide() ? 110 : 112);
        lv_obj_set_width(battery_label_, wide() ? 98 : 100);
        if (wide()) { lv_obj_set_x(time_, 270); lv_obj_set_width(time_, 150); }
        action_rail(screen_, "Finish", Action::finish);
    }
    if (dialog_ != Dialog::none) {
        if (wide()) draw_wide_dialog(screen_);
        else draw_dialog(screen_);
    }
    lv_scr_load(screen_);
    if (old) lv_obj_del(old);
    refresh_values();
}
// Layout follows the logical LVGL resolution. Panel rotation belongs to the BSP.
bool View::wide() const {
    return lv_disp_get_hor_res(nullptr) == 640 && lv_disp_get_ver_res(nullptr) == 172;
}
void View::draw_wide(lv_obj_t* root) {
    const auto& s = controller_.session();
    const auto phase = s.phase;
    auto* mode = button(root, s.mode == Mode::espresso ? "Espresso" : "Pour-over",
                        8, 4, 124, 30, Action::toggle_mode);
    auto* mode_text = lv_obj_get_child(mode, 0);
    lv_obj_set_width(mode_text, 90); lv_obj_align(mode_text, LV_ALIGN_RIGHT_MID, -2, 0);
    auto* mode_icon = lv_obj_create(mode); lv_obj_remove_style_all(mode_icon);
    lv_obj_set_pos(mode_icon, 7, 6); lv_obj_set_size(mode_icon, 20, 17);
    lv_obj_clear_flag(mode_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(mode_icon, draw_card_icon, LV_EVENT_DRAW_MAIN, nullptr);
    if (phase != Phase::ready) lv_obj_clear_flag(mode, LV_OBJ_FLAG_CLICKABLE);
    if (phase == Phase::ready) {
        stage_text_ = label(root, "", 160, 9, 352, &lv_font_montserrat_18, ink);
        lv_obj_set_style_text_align(stage_text_, LV_TEXT_ALIGN_CENTER, 0);
        tare_button_ = button(root, "", 8, 39, 304, 125, Action::tare);
        lv_obj_set_style_border_color(tare_button_, color(mint), 0);
        weight_ = label(tare_button_, "0.0", 58, 12, 218, &weight_font_72);
        weight_unit_ = label(tare_button_, "g", 0, 0, 28, &lv_font_montserrat_22, muted);
        weight_hint_ = label(tare_button_, "", 8, 98, 288, &lv_font_montserrat_14, mint);
        lv_obj_set_style_text_align(weight_hint_, LV_TEXT_ALIGN_CENTER, 0);
        object_card_ = button(root, "", 324, 39, 150, 125, Action::objects);
        coffee_card_ = button(root, "", 482, 39, 150, 125, Action::dose);
        for (auto* card : {object_card_, coffee_card_}) {
            lv_obj_set_style_bg_color(card, color(bg), 0);
            auto* icon = lv_obj_create(card);
            lv_obj_remove_style_all(icon);
            lv_obj_set_pos(icon, 55, 12); lv_obj_set_size(icon, 40, 34);
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(icon, draw_card_icon, LV_EVENT_DRAW_MAIN,
                               card == coffee_card_ ? this : nullptr);
        }
        object_text_ = label(object_card_, "Container", 5, 56, 140, &lv_font_montserrat_18);
        lv_label_set_long_mode(object_text_, LV_LABEL_LONG_DOT);
        lv_obj_set_height(object_text_, 23);
        lv_obj_set_style_text_align(object_text_, LV_TEXT_ALIGN_CENTER, 0);
        object_check_ = label(object_card_, "", 127, 8, 18, &lv_font_montserrat_14, mint);
        for (auto* card : {object_card_, coffee_card_}) {
            auto* pill=lv_obj_create(card);style(pill,panel);lv_obj_set_pos(pill,12,87);lv_obj_set_size(pill,126,28);
            lv_obj_set_style_border_width(pill,0,0);lv_obj_clear_flag(pill,LV_OBJ_FLAG_CLICKABLE);
            if(card==object_card_) object_pill_=pill; else coffee_pill_=pill;
        }
        object_weight_ = label(object_card_, "", 8, 92, 134, &lv_font_montserrat_18, mint);
        lv_obj_set_style_text_align(object_weight_, LV_TEXT_ALIGN_CENTER, 0);
        auto* coffee_title = label(coffee_card_, "Coffee", 5, 56, 140, &lv_font_montserrat_18);
        lv_obj_set_style_text_align(coffee_title, LV_TEXT_ALIGN_CENTER, 0);
        coffee_check_ = label(coffee_card_, "", 127, 8, 18, &lv_font_montserrat_14, mint);
        dose_text_ = label(coffee_card_, "", 5, 83, 140, &lv_font_montserrat_18, mint);
        coffee_hint_ = label(coffee_card_, "", 5, 106, 140, &lv_font_montserrat_14, muted);
        lv_obj_set_style_text_align(dose_text_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(coffee_hint_, LV_TEXT_ALIGN_CENTER, 0);
    } else if (phase == Phase::remove_load) {
        label(root, "Remove objects", 165, 8, 340, &lv_font_montserrat_22, mint);
        label(root, "Remove the load to prepare your next brew.", 28, 80, 580, &lv_font_montserrat_18);
        if (!controller_.live()) button(root, "Simulate removal", 424, 124, 208, 44, Action::remove, 0, true);
    } else {
        label(root, phase == Phase::result ? "Complete" : "Running", phase == Phase::brewing ? 145 : 226, 9, 106, &lv_font_montserrat_18, mint);
        time_ = label(root, "", 340, 9, 164, &lv_font_montserrat_18);
        const bool water = s.mode == Mode::pour_over && s.vessel && s.vessel->kind == Kind::pour_over_set;
        label(root, water ? "Water added (g)" : "Beverage (g)", 10, 41, 168, &lv_font_montserrat_14);
        label(root, "Actual", 202, 41, 58, &lv_font_montserrat_14, mint);
        label(root, "Target", 280, 41, 64, &lv_font_montserrat_14, muted);
        for (int i=0;i<4;++i) {
            auto* segment=lv_obj_create(root); style(segment, i ? muted : mint);
            lv_obj_set_pos(segment,i ? 263+(i-1)*5 : 183,48);
            lv_obj_set_size(segment,i ? 3 : 14,2);lv_obj_set_style_border_width(segment,0,0);
        }
        chart_ = lv_obj_create(root); lv_obj_remove_style_all(chart_);
        lv_obj_set_pos(chart_, 6, 62); lv_obj_set_size(chart_, 334, 104);
        lv_obj_clear_flag(chart_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(chart_, draw_brew_plot, LV_EVENT_DRAW_MAIN, this);
        auto* mass = lv_obj_create(root); style(mass);
        lv_obj_set_pos(mass, 350, 39); lv_obj_set_size(mass, phase == Phase::brewing ? 166 : 138, phase == Phase::brewing ? 64 : 72);
        weight_ = label(mass, "", 12, 7, 108, &lv_font_montserrat_32);
        weight_unit_ = label(mass, "g", 0, 0, 20, &lv_font_montserrat_14, muted);
        auto* title = label(mass, water ? "Water added" : "Beverage", 0, phase == Phase::brewing ? 43 : 49, phase == Phase::brewing ? 166 : 138, &lv_font_montserrat_14, muted);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        auto* rate = lv_obj_create(root); style(rate);
        lv_obj_set_pos(rate, phase == Phase::brewing ? 350 : 496, phase == Phase::brewing ? 107 : 39);
        lv_obj_set_size(rate, phase == Phase::brewing ? 166 : 136, phase == Phase::brewing ? 60 : 72);
        flow_ = label(rate, "", 4, 7, phase == Phase::brewing ? 158 : 128, &lv_font_montserrat_32);
        lv_obj_set_style_text_align(flow_, LV_TEXT_ALIGN_CENTER, 0);
        title = label(rate, phase == Phase::result ? "Ratio" : "Flow (g/s)", 0, phase == Phase::brewing ? 39 : 49, phase == Phase::brewing ? 166 : 136, &lv_font_montserrat_14, muted);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        if (phase == Phase::result) {
            label(root, LV_SYMBOL_UP "  Remove container", 354, 130, 278,
                  &lv_font_montserrat_18, mint);
        }
        status_ = label(root, "", 8, 36, 332, &lv_font_montserrat_14, amber);
        lv_obj_set_style_bg_color(status_, color(bg), 0);
        lv_obj_set_style_bg_opa(status_, LV_OPA_COVER, 0);
        lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN);
    }
}
namespace {
void stroke(lv_draw_ctx_t* context, lv_point_t a, lv_point_t b, unsigned rgb, int width=1, bool dashed=false) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.color=color(rgb); d.width=width;
    d.round_start=d.round_end=true;
    if (!dashed) { lv_draw_line(context,&d,&a,&b); return; }
    const float length=std::hypot(b.x-a.x,b.y-a.y);
    auto at=[&](float t){ return lv_point_t{static_cast<lv_coord_t>(a.x+(b.x-a.x)*t/length),static_cast<lv_coord_t>(a.y+(b.y-a.y)*t/length)}; };
    for(float t=0;t<length;t+=8) { auto p=at(t),q=at(std::min(t+4,length)); lv_draw_line(context,&d,&p,&q); }
}
void dot(lv_draw_ctx_t* context,lv_point_t p,int radius,unsigned rgb,bool hollow=false) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=color(hollow?bg:rgb);d.radius=LV_RADIUS_CIRCLE;
    d.border_width=hollow?1:0;d.border_color=color(rgb);
    const lv_area_t a{static_cast<lv_coord_t>(p.x-radius),static_cast<lv_coord_t>(p.y-radius),static_cast<lv_coord_t>(p.x+radius),static_cast<lv_coord_t>(p.y+radius)};
    lv_draw_rect(context,&d,&a);
}
}
void View::draw_card_icon(lv_event_t* event) {
    auto* context=lv_event_get_draw_ctx(event);lv_area_t area;lv_obj_get_coords(lv_event_get_target(event),&area);
    const float zoom=lv_area_get_width(&area)/40.F;
    auto p=[&](int x,int y){return lv_point_t{static_cast<lv_coord_t>(area.x1+x*zoom),static_cast<lv_coord_t>(area.y1+y*zoom)};};
    if (!lv_event_get_user_data(event)) {
        const lv_point_t pts[]={{7,3},{7,24},{12,30},{25,30},{30,24},{30,3},{7,3}};
        for(unsigned i=1;i<std::size(pts);++i)stroke(context,p(pts[i-1].x,pts[i-1].y),p(pts[i].x,pts[i].y),ink,2);
        stroke(context,p(30,8),p(38,8),ink,2);stroke(context,p(38,8),p(38,21),ink,2);stroke(context,p(38,21),p(30,21),ink,2);
    } else {
        lv_point_t previous{};
        for(int i=0;i<=40;++i){const double t=i*6.283185307/40;const double a=10*cos(t),b=15*sin(t);auto next=p(20+.8*a+.6*b,17-.6*a+.8*b);if(i)stroke(context,previous,next,ink,2);previous=next;}
        const lv_point_t pts[]={{28,5},{22,10},{21,17},{18,23},{12,29}};
        for(unsigned i=1;i<std::size(pts);++i)stroke(context,p(pts[i-1].x,pts[i-1].y),p(pts[i].x,pts[i].y),ink,2);
    }
}
void View::draw_brew_plot(lv_event_t* event) {
    const auto& s=static_cast<View*>(lv_event_get_user_data(event))->controller_.session();
    auto* ctx=lv_event_get_draw_ctx(event);lv_area_t area;lv_obj_get_coords(lv_event_get_target(event),&area);
    const auto guide=s.brew_guide();const float max_t=std::max(guide.duration(),s.seconds);
    const float mass=std::max(s.net_g,s.target_g.value_or(s.mode==Mode::espresso?40.F:300.F));
    const float step=mass<=100?10.F:50.F;const float max_g=std::max(step,std::ceil(mass/step)*step);
    const int x=area.x1+25,y=area.y1+5,w=299,h=76;
    auto point=[&](float t,float g){return lv_point_t{static_cast<lv_coord_t>(x+std::clamp(t/max_t,0.F,1.F)*w),static_cast<lv_coord_t>(y+h-std::clamp(g/max_g,0.F,1.F)*h)};};
    auto caption=[&](int xx,int yy,const char* value){lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.color=color(muted);d.font=&lv_font_montserrat_14;const lv_area_t a{static_cast<lv_coord_t>(xx),static_cast<lv_coord_t>(yy),static_cast<lv_coord_t>(xx+44),static_cast<lv_coord_t>(yy+17)};lv_draw_label(ctx,&d,&a,value,nullptr);};
    char text[24];
    for(int i=0;i<3;++i){const float g=max_g*i/2;auto a=point(0,g),b=point(max_t,g);stroke(ctx,a,b,border,1,true);std::snprintf(text,sizeof(text),"%.0f",static_cast<double>(g));caption(area.x1,a.y-7,text);}
    // Uniform reference ticks plus every recipe window (when labels fit).
    for(int i=0;i<4;++i){auto a=point(max_t*i/3,0),b=point(max_t*i/3,max_g);stroke(ctx,a,b,border);}
    int right=-1;
    const size_t count=guide.count>2?guide.count:4;
    for(size_t i=0;i<count;++i){float t=guide.count>2?guide.points[i].seconds:max_t*i/3;auto a=point(t,0);int xx=std::clamp<int>(a.x-11,area.x1,area.x2-40);if(xx<right)continue;std::snprintf(text,sizeof(text),"%.0fs",static_cast<double>(t));caption(xx,y+h+5,text);lv_point_t size{};lv_txt_get_size(&size,text,&lv_font_montserrat_14,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);right=xx+size.x+4;}
    if(s.target_g){for(size_t i=1;i<guide.count;++i){auto a=point(guide.points[i-1].seconds,*s.target_g*guide.points[i-1].fraction),b=point(guide.points[i].seconds,*s.target_g*guide.points[i].fraction);stroke(ctx,a,b,muted,2,true);dot(ctx,b,2,muted,true);}if(max_t>guide.duration())stroke(ctx,point(guide.duration(),*s.target_g),point(max_t,*s.target_g),muted,2,true);}
    if(s.uncertain)return;
    const size_t count_samples=std::min<size_t>(s.sample_count,120);
    lv_point_t previous=point(0,0);
    for(size_t i=0;i<count_samples;++i){size_t index=s.sample_count>120?i*(s.sample_count-1)/119:i;const auto& sample=s.samples[index];auto next=point(sample.seconds,sample.grams);stroke(ctx,previous,next,mint,3);previous=next;}
    if(s.sample_count){auto current=point(s.seconds,s.net_g);stroke(ctx,point(s.seconds,0),point(s.seconds,max_g),mint,1,true);dot(ctx,current,3,mint);}
}
void View::draw_wide_dialog(lv_obj_t* root) {
    // Full-screen sheets make the 172 px height usable without shrinking controls.
    auto* box = lv_obj_create(root); style(box);
    lv_obj_set_pos(box, 0, 0); lv_obj_set_size(box, 640, 172);
    lv_obj_set_style_radius(box, 0, 0); lv_obj_set_style_border_width(box, 0, 0);
    if (dialog_ == Dialog::battery_history) draw_history(box);
    else if (dialog_ == Dialog::dose) {
        label(box, "Coffee", 12, 12, 180, &lv_font_montserrat_22, mint);
        dose_value_ = label(box, "", 12, 65, 184, &lv_font_montserrat_48);
        for (int i = 0; i < 3; ++i)
            button(box, i == 0 ? "18 g" : i == 1 ? "20 g" : "22 g", 208 + i * 128, 54, 120, 44,
                   Action::dose_preset, 180 + i * 20);
        const char* names[] = {"-1 g", "-0.1", "+0.1", "+1 g"};
        const int values[] = {-10, -1, 1, 10};
        for (int i = 0; i < 4; ++i) button(box, names[i], 208 + i * 104, 108, 96, 52, Action::dose_adjust, values[i]);
    } else if (dialog_ == Dialog::objects) {
        auto* list = lv_obj_create(box); style(list, bg);
        lv_obj_set_pos(list, 8, 8); lv_obj_set_size(list, 509, 74);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        if (!controller_.equipment_count()) label(list, "No equipment yet", 12, 24, 480, &lv_font_montserrat_18, muted);
        for (std::size_t i = 0; i < controller_.equipment_count(); ++i) {
            const auto& item = controller_.equipment()[i];
            const int x = (i % 2) * 258, y = (i / 2) * 82;
            auto* row = button(list, item.name.c_str(), x, y, 196, 74, Action::choose_object, i);
            auto* caption = lv_obj_get_child(row, 0);
            lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
            lv_obj_set_height(caption, 44); lv_obj_center(caption);
            button(list, LV_SYMBOL_TRASH, x + 200, y, 49, 74, Action::request_remove, item.id);
        }
        auto* add = button(box, LV_SYMBOL_PLUS " Register equipment", 8, 90, 509, 74, Action::registration);
        lv_obj_set_style_border_color(add, color(mint), 0);
        lv_obj_set_style_text_font(lv_obj_get_child(add, 0), &lv_font_montserrat_22, 0);
        auto* back = button(box, LV_SYMBOL_LEFT " Back", 533, 8, 99, 74, Action::close);
        lv_obj_set_style_text_font(lv_obj_get_child(back, 0), &lv_font_montserrat_18, 0);
        lv_obj_clear_flag(back, LV_OBJ_FLAG_PRESS_LOCK);
        return;
    } else if (dialog_ == Dialog::remove_equipment) {
        label(box, "Remove equipment?", 12, 12, 540, &lv_font_montserrat_22);
        for (std::size_t i = 0; i < controller_.equipment_count(); ++i)
            if (controller_.equipment()[i].id == remove_equipment_id_)
                label(box, controller_.equipment()[i].name.c_str(), 12, 60, 380, &lv_font_montserrat_22, mint);
        label(box, "Saved brews stay unchanged.", 12, 112, 380, &lv_font_montserrat_14, muted);

        button(box, "Remove", 416, 114, 216, 46, Action::confirm_remove, 0, true);
    } else if (dialog_ == Dialog::calibration) {
        label(box, "Calibrate scale", 12, 12, 540, &lv_font_montserrat_22);
        calibration_weight_label_ = label(box, "?", 12, 54, 260, &lv_font_montserrat_28);
        calibration_message_ = label(box, "", 12, 96, 260, &lv_font_montserrat_14, mint);
        label(box, "Empty platform, capture zero; add mass.", 284, 52, 348);
        button(box, "Capture empty zero", 284, 72, 348, 44, Action::capture_zero);
        for (int i = 0; i < 3; ++i) button(box, i == 0 ? "20 g" : i == 1 ? "50 g" : "100 g",
            284 + i * 118, 120, 112, 44, Action::calibrate, i == 0 ? 20 : i == 1 ? 50 : 100);
    } else if (dialog_ == Dialog::demo) {
        label(box, "Dummy scenarios", 12, 12, 540, &lv_font_montserrat_22);
        const char* names[] = {"Cup + 18.2 g dose", "Unknown / no baseline", "Portafilter > beans > cup", "V60 / bloom + pours"};
        for (int i = 0; i < 4; ++i) button(box, names[i], 12 + (i % 2) * 314, 56 + (i / 2) * 56,
                                         306, 48, Action::choose_scenario, i);
    } else { draw_wide_registration(box); return; }
    fit_width(box, 81, 100);
    // Keep the modal backdrop full-width behind the inset action card.
    action_rail(root, "Back",
                dialog_ == Dialog::remove_equipment ? Action::cancel_remove : Action::close);
}
void View::draw_wide_registration(lv_obj_t* box) {
    // Native 640x172 geometry: reserve x=525..639 (18%) for two actions.
    // Do not pass this layout through fit_width: icon hit areas stay 48x52.
    auto* back = button(box, LV_SYMBOL_LEFT " Back", 533, 8, 99, 74, Action::register_back);
    auto* primary = button(box, registration_step_ == 2 ? LV_SYMBOL_OK " Save" : "Continue",
                           533, 90, 99, 74, registration_step_ == 2 ? Action::register_save : Action::register_next, 0, true);
    for (auto* control : {back, primary}) {
        lv_obj_set_style_text_font(lv_obj_get_child(control, 0), &lv_font_montserrat_18, 0);
        lv_obj_clear_flag(control, LV_OBJ_FLAG_PRESS_LOCK);
    }
    if (registration_step_ == 2) {
        icon_name_ = label(box, kind_name(registration_kind_), 8, 10, 509, &lv_font_montserrat_18, muted);
        if (!registration_error_.empty()) set_text(icon_name_, registration_error_.c_str());
        for (unsigned i = 0; i < std::size(equipment_icons); ++i) {
            auto* item = button(box, equipment_icons[i], 8 + (i % 10) * 51, 48 + (i / 10) * 59,
                                48, 52, Action::choose_icon, i);
            auto* caption = lv_obj_get_child(item, 0);
            lv_obj_set_style_text_font(caption, &equipment_icons_32, 0); lv_obj_center(caption);
            lv_obj_set_style_border_color(item, color(i == registration_icon_ ? mint : border), 0);
            lv_obj_set_style_border_width(item, i == registration_icon_ ? 2 : 1, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_PRESS_LOCK);
            icon_buttons_[i] = item;
        }
        return;
    }
    registration_next_ = primary;
    if (registration_step_ == 0) {
        const int icons[] = {1, 4, 3};
        for (int i = 0; i < 3; ++i) {
            auto* card = button(box, "", 8 + i * 172, 8, 165, 156, Action::choose_kind, i);
            auto* glyph = label(card, equipment_icons[icons[i]], 0, 37, 165, &equipment_icons_32, mint);
            lv_obj_set_style_text_align(glyph, LV_TEXT_ALIGN_CENTER, 0);
            auto* name = label(card, kind_name(static_cast<Kind>(i)), 0, 91, 165, &lv_font_montserrat_18);
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
            if (static_cast<Kind>(i) == registration_kind_) {
                lv_obj_set_style_border_color(card, color(mint), 0);
                lv_obj_set_style_border_width(card, 2, 0);
                label(card, LV_SYMBOL_OK, 137, 10, 18, &lv_font_montserrat_14, mint);
            }
        }
    } else {
        auto* card = lv_obj_create(box); style(card);
        lv_obj_set_pos(card, 8, 8); lv_obj_set_size(card, 509, 156);
        const int icon = registration_kind_ == Kind::cup ? 1 : registration_kind_ == Kind::portafilter ? 4 : 3;
        auto* glyph = label(card, equipment_icons[icon], 0, 30, 180, &equipment_icons_32, mint);
        lv_obj_set_style_text_align(glyph, LV_TEXT_ALIGN_CENTER, 0);
        auto* name = label(card, kind_name(registration_kind_), 0, 91, 180, &lv_font_montserrat_18);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        if (controller_.live()) {
            registration_weight_label_ = label(card, "?", 194, 25, 307, &lv_font_montserrat_48);
            registration_status_ = label(card, "", 194, 100, 307, &lv_font_montserrat_14, muted);
        } else {
            label(card, "156.8 g", 194, 25, 307, &lv_font_montserrat_48);
            button(card, registration_stable_ ? "Stable reading" : "Unstable - wait", 194, 98, 300, 44,
                   Action::stable, 0, registration_stable_);
            if (!registration_stable_) lv_obj_add_state(primary, LV_STATE_DISABLED);
        }
    }
}

void View::draw_ready(lv_obj_t* root) {
    tare_button_ = button(root, "", 16, 104, 248, 96, Action::tare);
    weight_ = label(tare_button_, "0.0", 6, 4, 218, &weight_font_72);
    weight_hint_ = label(tare_button_, "", 8, 76, 230, &lv_font_montserrat_14, muted);
    weight_unit_ = label(tare_button_, "g", 0, 0, 28, &lv_font_montserrat_22);
    stage_text_ = label(root, "", 24, 70, 248, &lv_font_montserrat_22, mint);
    target_text_ = label(root, "", 24, 240, 240, &lv_font_montserrat_14, muted);
    auto* object = button(root, "", 280, 66, 184, 84, Action::objects);
    object_card_ = object;
    object_check_ = label(object, "", 154, 10, 22, &lv_font_montserrat_14, mint);
    object_text_ = label(object, "", 12, 10, 138, &lv_font_montserrat_18);
    lv_label_set_long_mode(object_text_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(object_text_, 23);
    object_weight_ = label(object, "", 12, 40, 160, &lv_font_montserrat_28);
    auto* dose = button(root, "", 280, 158, 184, 84, Action::dose);
    coffee_card_ = dose;
    coffee_check_ = label(dose, "", 154, 10, 22, &lv_font_montserrat_14, mint);
    coffee_hint_ = label(dose, "", 12, 61, 160, &lv_font_montserrat_14, muted);
    label(dose, "Coffee", 38, 10, 110, &lv_font_montserrat_18);
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
    if (!controller_.live()) {
        button(root, controller_.demo_running() ? "Demo running" : "Run demo", 16, 266, 140, 44, Action::run, 0, true);
        button(root, "Scenarios", 164, 266, 110, 44, Action::demo);
    }
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
        label(root, LV_SYMBOL_UP "  Remove container", 20, 280, 440,
              &lv_font_montserrat_22, mint);
    } else {
        if (!controller_.live()) button(root, controller_.session().paused ? "Resume flow" : "Pause flow", 16, 270, 140, 44, Action::pause);
        lv_obj_set_width(object_text_, 338);
        lv_obj_set_width(status_, 352);
        lv_obj_set_width(target_text_, 364);
        lv_obj_set_width(chart_, 190);
        lv_obj_set_x(weight_, 238); lv_obj_set_width(weight_, 142);
        lv_obj_set_style_text_font(weight_, &lv_font_montserrat_32, 0);
        lv_obj_set_x(time_, 238);
        lv_obj_set_x(flow_, 238); lv_obj_set_width(flow_, 146);
    }
}
void View::refresh_values() {
    refresh_history();
    if (battery_label_) {
        const char* icon = !battery_.percent ? LV_SYMBOL_BATTERY_EMPTY :
            *battery_.percent >= 90 ? LV_SYMBOL_BATTERY_FULL :
            *battery_.percent >= 60 ? LV_SYMBOL_BATTERY_3 :
            *battery_.percent >= 30 ? LV_SYMBOL_BATTERY_2 : LV_SYMBOL_BATTERY_1;
        const auto text = std::string(icon) + " " + (battery_.valid ? battery_text(battery_) : "--%");
        set_text(battery_label_, text.c_str());
        const auto battery_ink = color(controller_.live() ? battery_color(battery_) : 0xA6A6A6);
        if (lv_obj_get_style_text_color(battery_label_, LV_PART_MAIN).full != battery_ink.full)
            lv_obj_set_style_text_color(battery_label_, battery_ink, 0);
    }
    const auto& s = controller_.session();
    const auto& scale = controller_.scale();
    // Independent caches for net grams, gross grams and uncalibrated counts.
    // Update once per sensor timestamp; redraws never restart confirmation.
    if (display_epoch_ != scale.display_epoch() || display_phase_ != s.phase) {
        weight_display_.reset(); gross_display_.reset(); counts_display_.reset();
        display_epoch_ = scale.display_epoch();
        display_phase_ = s.phase;
    }
    const auto stats = scale.stability_stats();
    const float noise_counts = static_cast<float>(stats.std_counts);
    const float noise_g = scale.calibration() ? noise_counts / std::abs(scale.calibration()->counts_per_gram) : 0;
    const auto gross = gross_display_.update(scale.ready() ? std::optional<float>{scale.gross()} : std::nullopt,
                                             scale.sample_ms(), scale.quiet(), true, noise_g);
    const auto counts = counts_display_.update(scale.filtered_counts() ?
        std::optional<float>{static_cast<float>(*scale.filtered_counts())} : std::nullopt,
        scale.sample_ms(), scale.quiet(), false, noise_counts);
    if (tare_button_) {
        const bool needs_calibration = controller_.live() && !scale.calibration();
        set_text(weight_hint_, needs_calibration ? "Tap to calibrate" : "Tap to tare");
        const bool enabled = controller_.live() && s.phase == Phase::ready &&
            (needs_calibration || (scale.ready() && scale.stable()));
        if (enabled) lv_obj_clear_state(tare_button_, LV_STATE_DISABLED);
        else lv_obj_add_state(tare_button_, LV_STATE_DISABLED);
    }
    if (calibration_message_) set_text(calibration_message_,
        (calibration_message_text_ + "\n" + controller_.scale().status()).c_str());
    if (calibration_weight_label_) {
        if (scale.calibration()) set_text(calibration_weight_label_, gross ? number(gross, " g").c_str() : "? g");
        else {
            char text[48];
            if (counts) std::snprintf(text, sizeof(text), "%.0f counts", static_cast<double>(*counts));
            else std::snprintf(text, sizeof(text), "? counts");
            set_text(calibration_weight_label_, text);
        }
    }
    if (registration_weight_label_) {
        const auto snapshot = registration_measurement_.update(scale);
        const bool stable = snapshot.has_value();
        set_text(registration_weight_label_, number(gross, " g").c_str());
        set_text(registration_status_, stable ? (wide() ? "Ready" : "Stable - ready to continue") :
                 scale.stable() ? "Wait for a fresh stable reading" : scale.status());
        if (stable) lv_obj_clear_state(registration_next_, LV_STATE_DISABLED);
        else lv_obj_add_state(registration_next_, LV_STATE_DISABLED);
    }
    const bool available = (s.phase == Phase::ready || (s.baseline_known && !s.uncertain)) &&
        (!controller_.live() || s.phase == Phase::result || controller_.scale().ready());
    std::optional<float> displayed = available ? std::optional<float>{s.net_g} : std::nullopt;
    if (controller_.live()) {
        if (s.phase != Phase::result)
            displayed = weight_display_.update(displayed, scale.sample_ms(),
                                                scale.quiet(), true, noise_g);
    }
    const auto weight_text = number(displayed);
    // Log actual visible transitions, including unavailable/recovery. Retain the
    // last text across screen redraws to avoid duplicate logs. Never log in the
    // acquisition task or while holding the HX711 clocking critical section.
#ifdef ESP_PLATFORM
    const auto log_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
#else
    const auto log_ms = static_cast<std::uint64_t>(lv_tick_get());
#endif
    if (controller_.live() && weight_ && weight_text != last_logged_weight_ &&
        (last_logged_weight_.empty() || log_ms - last_weight_log_ms_ >= 1000)) {
        last_weight_log_ms_ = log_ms;
        last_logged_weight_ = weight_text;
        const auto& scale = controller_.scale();
        const auto raw = scale.last_raw() ? std::to_string(*scale.last_raw()) : "NA";
        const auto gross = scale.ready() ? number(scale.gross(), " g") : "NA";
        const char* error = s.brew_error == BrewError::object_removed ? "object_removed" :
                            s.brew_error == BrewError::sensor_fault ? "sensor_fault" : "none";
        char line[384];
        std::snprintf(line, sizeof(line),
            "now_ms=%llu seq=%llu display=%s g raw=%s sample_ms=%llu gross=%s net=%.4f g stable=%d ready=%d status=%s brew_error=%s removal_below=%.4f g",
            static_cast<unsigned long long>(log_ms), static_cast<unsigned long long>(scale.received()),
            weight_text.c_str(), raw.c_str(), static_cast<unsigned long long>(scale.sample_ms()),
            gross.c_str(), static_cast<double>(scale.ready() ? scale.net() : NAN),
            scale.stable(), scale.ready(), scale.status(), error,
            static_cast<double>(s.removal_threshold_g.value_or(NAN)));
#ifdef ESP_PLATFORM
        device::DiagnosticLogger::instance().text("weight", line);
#else
        std::printf("weight: %s\n", line);
#endif
    }
    const lv_font_t* weight_font = wide() ? &lv_font_montserrat_32 : &lv_font_montserrat_48;
    if (s.phase != Phase::ready && weight_) {
        lv_point_t text_size{};
        lv_txt_get_size(&text_size, weight_text.c_str(), weight_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (text_size.x > lv_obj_get_width(weight_) - 12) weight_font = wide() ? &lv_font_montserrat_22 : &lv_font_montserrat_32;
    }
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
        if (wide()) {
            const int parent_width = lv_obj_get_width(lv_obj_get_parent(weight_));
            const int unit_width = s.phase == Phase::ready ? 28 : 16;
            lv_obj_set_x(weight_, std::max<int>(3, (parent_width - size.x - unit_width - 6) / 2));
        }
        const int x = lv_obj_get_x(weight_) + size.x + 6;
        const int y = lv_obj_get_y(weight_) + weight_font->line_height - weight_font->base_line
                      - unit_font->line_height + unit_font->base_line;
        if (lv_obj_get_x(weight_unit_) != x || lv_obj_get_y(weight_unit_) != y)
            lv_obj_set_pos(weight_unit_, x, y);
    }
    if (s.phase == Phase::ready) {
        const auto& item = s.active_object;
        if (object_text_) {
            set_text(object_text_, wide() ? (item ? kind_name(item->kind) : "Container") : item ? item->name.c_str() : controller_.live() && !controller_.equipment_count() ? "Register object" : controller_.object_ambiguous() ? "Choose object" : "Container");
            set_text(object_weight_, wide() && !item ? "Select" : number(item ? item->empty_g : std::nullopt, " g").c_str());
        }
        const auto candidate = controller_.coffee_candidate();
        if (dose_text_) set_text(dose_text_, (wide() && !s.dose_g && !candidate ? std::string("Set dose") : number(s.dose_g ? s.dose_g : candidate, " g")).c_str());
        if (object_pill_) {
            if (item) lv_obj_add_flag(object_pill_,LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(object_pill_,LV_OBJ_FLAG_HIDDEN);
            if (s.dose_g || candidate) lv_obj_add_flag(coffee_pill_,LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(coffee_pill_,LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(dose_text_,s.dose_g || candidate ? 83 : 92);
        }
        const int visual = s.dose_g ? 2 : candidate && dialog_ == Dialog::none ? 1 : 0;
        if (coffee_card_ && visual != coffee_visual_state_) {
            coffee_visual_state_ = visual;
            lv_anim_del(coffee_card_, pulse_coffee);
            lv_obj_set_style_border_opa(coffee_card_, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(coffee_card_, color(visual == 1 ? amber : visual == 2 ? mint : border), 0);
            lv_obj_set_style_border_width(coffee_card_, visual ? 2 : 1, 0);
            lv_obj_set_style_text_color(coffee_hint_, color(visual == 1 ? amber : muted), 0);
            if (visual == 1) {
                lv_anim_t pulse;
                lv_anim_init(&pulse);
                lv_anim_set_var(&pulse, coffee_card_);
                lv_anim_set_exec_cb(&pulse, pulse_coffee);
                lv_anim_set_values(&pulse, LV_OPA_30, LV_OPA_COVER);
                lv_anim_set_time(&pulse, 700);
                lv_anim_set_playback_time(&pulse, 700);
                lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
                lv_anim_start(&pulse);
            }
        }
        if (coffee_hint_) set_text(coffee_hint_, s.dose_g ? "Tap to edit" : candidate ? "Tap to save" : wide() ? "" : "Tap to set");
        if (coffee_check_) set_text(coffee_check_, s.dose_g ? LV_SYMBOL_OK : "");
        const bool selected = item && s.loaded;
        if (object_card_ && selected != object_visual_selected_) {
            object_visual_selected_ = selected;
            lv_obj_set_style_border_color(object_card_, color(selected ? mint : border), 0);
            lv_obj_set_style_border_width(object_card_, selected ? 2 : 1, 0);
        }
        if (object_check_) set_text(object_check_, selected ? LV_SYMBOL_OK : "");
        std::string stage;
        if (controller_.live() && !scale.calibration()) {
            stage = LV_SYMBOL_WARNING " Calibrate scale";
        } else if (controller_.live() && !scale.ready()) {
            stage = LV_SYMBOL_WARNING " Check scale";
        } else if (!selected) {
            stage = std::string(equipment_icons[1]) + " Load container";
        } else if (!s.dose_g) {
            stage = std::string(equipment_icons[0]) + (candidate ? " Save coffee" : " Load coffee");
        } else if (item->kind == Kind::portafilter) {
            stage = std::string(equipment_icons[1]) + " Load container";
        } else if (controller_.live() && !s.brew_prepared) {
            stage = LV_SYMBOL_PAUSE " Wait for stable";
        } else {
            stage = LV_SYMBOL_PLAY " Pour to start";
        }
        if (target_text_) set_text(target_text_, s.target_g ? (std::string(wide() ? "Target\n" : "Target ") + number(s.target_g, " g")).c_str() : "");
        if (stage_text_) {
            if (wide()) { const auto pos = stage.find(' '); if (pos != std::string::npos) stage = stage.substr(pos + 1); }
            set_text(stage_text_, stage.c_str());
        }
        return;
    }
    if (s.phase != Phase::brewing && s.phase != Phase::result) return;
    if (object_text_) set_text(object_text_,
        s.vessel ? s.vessel->name.c_str() : "Object ?");
    char clock[32];
    const auto secs = static_cast<unsigned>(s.seconds);
    std::snprintf(clock, sizeof(clock), "%02u:%02u", secs / 60, secs % 60);
    set_text(time_, clock);
    if (wide()) {
        lv_obj_set_style_text_font(flow_, s.uncertain ? &lv_font_montserrat_18 : &lv_font_montserrat_32, 0);
        set_text(flow_, s.uncertain ? "?" : s.phase == Phase::result ?
            (s.ratio() ? "1:" + number(s.ratio()) : "?").c_str() : number(s.flow_g_s).c_str());
    } else {
        set_text(flow_, s.phase == Phase::result ? ("Ratio " + (s.ratio() ? "1:" + number(s.ratio()) : "?")).c_str()
            : s.uncertain ? "Flow unavailable" : number(s.flow_g_s, " g/s").c_str());
    }
    std::string status;
    if (s.uncertain && controller_.live() && s.brew_error != BrewError::none) {
        char alert[96];
        const auto remaining = static_cast<unsigned>(std::ceil(controller_.error_remaining_s()));
        if (s.phase == Phase::result)
            std::snprintf(alert, sizeof(alert), "%s - result incomplete",
                          s.brew_error == BrewError::object_removed ? "Object removed" : "Sensor unavailable");
        else std::snprintf(alert, sizeof(alert), "%s - home in %02u:%02u",
                          s.brew_error == BrewError::object_removed ? "Object removed" : "Sensor unavailable",
                          remaining / 60, remaining % 60);
        status = alert;
    } else if (s.uncertain) status = "Baseline uncertain - weight unavailable";
    else if (s.target_g && s.net_g >= *s.target_g) status = "Target reached  +" + number(s.net_g - *s.target_g, " g");
    else if (s.flow_g_s == 0 && s.phase == Phase::brewing) status = "Flow paused - timer continues";
    else status = s.mode == Mode::pour_over && s.vessel && s.vessel->kind == Kind::pour_over_set ? "Set mass increase / water added" : "Beverage collected in cup";
    set_text(status_, status.c_str());
    if (wide()) {
        if (s.uncertain) lv_obj_clear_flag(status_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(chart_);
        return;
    }
    const auto guide = s.brew_guide();
    const auto target_text = "Dose " + number(s.dose_g, " g") + "    Target " + number(s.target_g, " g") +
        "    " + (s.phase == Phase::result ? "Delta " : "Left ") +
        number(s.target_g && !s.uncertain ? std::optional<float>{s.phase == Phase::result ? s.net_g - *s.target_g : std::max(0.0F, *s.target_g - s.net_g)} : std::nullopt, " g");
    const auto window_text = "Now " + number(s.target_g ? std::optional<float>{*s.target_g * guide.fraction_at(s.seconds)} : std::nullopt, " g") +
        "\nTotal " + number(s.target_g, " g");
    set_text(target_text_, wide() ? window_text.c_str() : target_text.c_str());
    const float max_t = std::max(guide.duration(), s.seconds);
    const float max_g = std::max(s.mode == Mode::espresso ? 60.0F : 350.0F,
                               std::max(s.net_g, s.target_g.value_or(0)) * 1.15F);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_X, 0, 1000);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, static_cast<int>(max_g * 10));
    char axis[24];
    std::snprintf(axis, sizeof(axis), "%.0f g", static_cast<double>(max_g));
    set_text(chart_max_, axis);
    std::snprintf(axis, sizeof(axis), "0 - %.0f", static_cast<double>(max_t));
    if (chart_time_) set_text(chart_time_, axis);
    int previous_right = -1;
    for (std::size_t i = 0; i < guide_ticks_.size() && guide_ticks_[i]; ++i) {
        auto* tick = guide_ticks_[i];
        const int x = i < guide.count ? std::clamp(12 + static_cast<int>(260 * guide.points[i].seconds / max_t) - 10, 12, 236) : 0;
        if (i >= guide.count || x < previous_right) { lv_obj_add_flag(tick, LV_OBJ_FLAG_HIDDEN); continue; }
        std::snprintf(axis, sizeof(axis), "%.0fs", static_cast<double>(guide.points[i].seconds));
        set_text(tick, axis);
        lv_obj_set_x(tick, x);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_HIDDEN);
        lv_point_t tick_size{};
        lv_txt_get_size(&tick_size, axis, &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        previous_right = x + tick_size.x + 4;
    }
    if (s.sample_count && !s.uncertain) {
        lv_obj_clear_flag(current_point_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(current_point_, lv_obj_get_x(chart_) + static_cast<int>((lv_obj_get_width(chart_) - 2) * s.seconds / max_t) - 3,
                       lv_obj_get_y(chart_) + lv_obj_get_height(chart_) - 2 -
                       static_cast<int>((lv_obj_get_height(chart_) - 2) * s.net_g / max_g) - 3);
    } else lv_obj_add_flag(current_point_, LV_OBJ_FLAG_HIDDEN);
    for (std::size_t i = 0; i < 120; ++i) {
        const auto index = s.sample_count > 120 ? i * (s.sample_count - 1) / 119 : i;
        if (index < s.sample_count && !s.uncertain) {
            const auto& p = s.samples[index];
            series_->x_points[i] = static_cast<lv_coord_t>(p.seconds / max_t * 1000);
            series_->y_points[i] = static_cast<lv_coord_t>(p.grams * 10);
        } else { series_->x_points[i] = LV_CHART_POINT_NONE; series_->y_points[i] = LV_CHART_POINT_NONE; }
        target_series_->x_points[i] = LV_CHART_POINT_NONE;
        target_series_->y_points[i] = LV_CHART_POINT_NONE;
    }
    if (s.target_g) {
        for (std::size_t i = 0; i < guide.count; ++i) {
            target_series_->x_points[i] = static_cast<lv_coord_t>(guide.points[i].seconds / max_t * 1000);
            target_series_->y_points[i] = static_cast<lv_coord_t>(*s.target_g * guide.points[i].fraction * 10);
        }
        if (max_t > guide.duration()) {
            target_series_->x_points[guide.count] = 1000;
            target_series_->y_points[guide.count] = static_cast<lv_coord_t>(*s.target_g * 10);
        }
    }
    lv_chart_refresh(chart_);
}
void View::draw_dialog(lv_obj_t* root) {
    auto* shade = lv_obj_create(root);
    style(shade, 0x000000);
    lv_obj_set_size(shade, 480, 320);
    lv_obj_set_style_radius(shade, 0, 0);
    lv_obj_set_style_border_width(shade, 0, 0);
    lv_obj_set_style_bg_opa(shade, LV_OPA_70, 0);
    auto* box = lv_obj_create(shade);
    style(box);
    lv_obj_set_size(box, (dialog_ == Dialog::registration || dialog_ == Dialog::battery_history) ? 452 : 304, 304);
    lv_obj_center(box);
    const int width = (dialog_ == Dialog::registration || dialog_ == Dialog::battery_history) ? 452 : 304;
    if (dialog_ == Dialog::battery_history) draw_history(box);
    else if (dialog_ == Dialog::calibration) {
        label(box, "Calibrate scale", 16, 18, 230, &lv_font_montserrat_22);
        calibration_weight_label_ = label(box, "?", 16, 56, 272, &lv_font_montserrat_28);
        label(box, "1. Empty platform, capture zero.\n2. Add mass, wait, tap its value.", 16, 94, 272);
        button(box, "Capture empty zero", 16, 126, 272, 42, Action::capture_zero);
        button(box, "20 g", 16, 178, 84, 42, Action::calibrate, 20);
        button(box, "50 g", 110, 178, 84, 42, Action::calibrate, 50);
        button(box, "100 g", 204, 178, 84, 42, Action::calibrate, 100);
        calibration_message_ = label(box, "", 16, 232, 272, &lv_font_montserrat_14, mint);
    } else if (dialog_ == Dialog::objects) {
        auto* list = lv_obj_create(box);
        style(list);
        lv_obj_set_pos(list, 12, 52);
        lv_obj_set_size(list, 280, 186);
        lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
        if (!controller_.equipment_count()) label(list, "No equipment yet", 14, 64, 250,
                                                   &lv_font_montserrat_18, muted);
        for (std::size_t i = 0; i < controller_.equipment_count(); ++i) {
            const auto& item = controller_.equipment()[i];
            auto* row = button(list, item.name.c_str(), 0, static_cast<int>(i) * 52, 214, 46,
                              Action::choose_object, static_cast<int>(i));
            lv_label_set_long_mode(lv_obj_get_child(row, 0), LV_LABEL_LONG_DOT);
            lv_obj_set_height(lv_obj_get_child(row, 0), 24);
            lv_obj_center(lv_obj_get_child(row, 0));
            button(list, LV_SYMBOL_TRASH, 224, static_cast<int>(i) * 52, 46, 46,
                   Action::request_remove, static_cast<int>(item.id));
        }
        button(box, "+ Register equipment", 12, 246, 280, 46, Action::registration, 0, true);
    } else if (dialog_ == Dialog::remove_equipment) {
        label(box, "Remove equipment?", 16, 60, 272, &lv_font_montserrat_22);
        for (std::size_t i = 0; i < controller_.equipment_count(); ++i)
            if (controller_.equipment()[i].id == remove_equipment_id_)
                label(box, controller_.equipment()[i].name.c_str(), 16, 108, 272, &lv_font_montserrat_22, mint);
        label(box, "Saved brews stay unchanged.", 16, 174, 272, &lv_font_montserrat_14, muted);

        button(box, "Remove", 160, 238, 128, 50, Action::confirm_remove, 0, true);
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
    if (width > 378) { fit_width(box, 378, width); lv_obj_set_width(box, 378); }
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, (394 - std::min(width, 378)) / 2, 8);
    action_rail(shade, "Back",
                dialog_ == Dialog::remove_equipment ? Action::cancel_remove : Action::close);
}
namespace {
unsigned usage_color(UsageState state) {
    switch (state) {
    case UsageState::charging: return 0x55C878;
    case UsageState::active: return 0x4EA5FF;
    case UsageState::sleep: return 0xFFA652;
    case UsageState::estimated: return 0xA6A6A6;
    default: return muted;
    }
}
}
void View::draw_history(lv_obj_t* box) {
    if (wide()) {
        label(box, "Battery Usage", 12, 12, 225, &lv_font_montserrat_22);
        const unsigned ranges[] = {6, 24, 72};
        const char* names[] = {"6hr", "1day", "3day"};
        for (unsigned i = 0; i < 3; ++i)
            button(box, names[i], 242 + i * 110, 4, 102, 40, Action::history_range, ranges[i], history_hours_ == ranges[i]);
        history_plot_ = lv_obj_create(box); style(history_plot_, bg);
        lv_obj_set_pos(history_plot_, 58, 56); lv_obj_set_size(history_plot_, 562, 76);
        lv_obj_set_style_radius(history_plot_, 0, 0); lv_obj_set_style_border_width(history_plot_, 0, 0);
        lv_obj_clear_flag(history_plot_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(history_plot_, draw_history_plot, LV_EVENT_DRAW_MAIN, this);
        label(box, "100%", 10, 50, 46, &lv_font_montserrat_14, muted);
        label(box, "50%", 20, 88, 36, &lv_font_montserrat_14, muted);
        label(box, "0%", 28, 124, 28, &lv_font_montserrat_14, muted);
        const auto left = "-" + std::to_string(history_hours_) + "h";
        const auto middle = "-" + std::to_string(history_hours_ / 2) + "h";
        label(box, left.c_str(), 58, 136, 60, &lv_font_montserrat_14, muted);
        label(box, middle.c_str(), 320, 136, 60, &lv_font_montserrat_14, muted);
        label(box, "Now", 572, 136, 48, &lv_font_montserrat_14, muted);
        label(box, "Voltage estimate", 58, 154, 180, &lv_font_montserrat_14, usage_color(UsageState::estimated));
        label(box, "Sleep (estimated)", 328, 154, 200, &lv_font_montserrat_14, usage_color(UsageState::sleep));
        refresh_history();
        return;
    }
    label(box, "Battery Usage", 16, 15, 320, &lv_font_montserrat_22);
    const unsigned ranges[] = {6, 24, 72};
    const char* names[] = {"6hr", "1day", "3day"};
    for (unsigned i = 0; i < 3; ++i)
        button(box, names[i], 58 + i * 126, 55, 114, 38, Action::history_range, ranges[i], history_hours_ == ranges[i]);
    history_plot_ = lv_obj_create(box);
    style(history_plot_, bg);
    lv_obj_set_pos(history_plot_, 58, 112); lv_obj_set_size(history_plot_, 374, 132);
    lv_obj_set_style_radius(history_plot_, 0, 0);
    lv_obj_set_style_border_width(history_plot_, 0, 0);
    lv_obj_clear_flag(history_plot_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(history_plot_, draw_history_plot, LV_EVENT_DRAW_MAIN, this);
    label(box, "100%", 12, 106, 44, &lv_font_montserrat_14, muted);
    label(box, "50%", 20, 172, 36, &lv_font_montserrat_14, muted);
    label(box, "0%", 28, 237, 28, &lv_font_montserrat_14, muted);
    const auto left = "-" + std::to_string(history_hours_) + "h";
    const auto middle = "-" + std::to_string(history_hours_ / 2) + "h";
    label(box, left.c_str(), 58, 250, 60, &lv_font_montserrat_14, muted);
    label(box, middle.c_str(), 226, 250, 60, &lv_font_montserrat_14, muted);
    label(box, "Now", 388, 250, 48, &lv_font_montserrat_14, muted);
    label(box, "Charging", 58, 280, 110, &lv_font_montserrat_14, usage_color(UsageState::charging));
    label(box, "Active", 200, 280, 90, &lv_font_montserrat_14, usage_color(UsageState::active));
    label(box, "Sleep", 340, 280, 90, &lv_font_montserrat_14, usage_color(UsageState::sleep));
    refresh_history();
}
void View::refresh_history() {
    if (!history_plot_ || !history_ || history_revision_ == history_->revision()) return;
    history_revision_ = history_->revision();
    lv_obj_invalidate(history_plot_);
}
void View::draw_history_plot(lv_event_t* event) {
    auto& view = *static_cast<View*>(lv_event_get_user_data(event));
    auto* context = lv_event_get_draw_ctx(event);
    lv_area_t area;
    lv_obj_get_content_coords(lv_event_get_target(event), &area);
    const auto w = lv_area_get_width(&area) - 1, h = lv_area_get_height(&area) - 1;
    lv_draw_line_dsc_t grid;
    lv_draw_line_dsc_init(&grid); grid.color = color(border); grid.width = 1;
    for (int i = 0; i <= 2; ++i) {
        const lv_point_t a{area.x1, static_cast<lv_coord_t>(area.y1 + h * i / 2)};
        const lv_point_t b{area.x2, a.y};
        lv_draw_line(context, &grid, &a, &b);
    }
    if (!view.history_) return;
    view.history_->segments(view.history_hours_ * 3600, [&](double x1, double y1, double x2, double y2, UsageState state) {
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line); line.color = color(usage_color(state)); line.width = 3;
        line.round_start = line.round_end = true;
        const lv_point_t a{static_cast<lv_coord_t>(area.x1 + std::lround(x1 * w)),
                           static_cast<lv_coord_t>(area.y2 - std::lround(y1 * h / 100))};
        const lv_point_t b{static_cast<lv_coord_t>(area.x1 + std::lround(x2 * w)),
                           static_cast<lv_coord_t>(area.y2 - std::lround(y2 * h / 100))};
        if (state != UsageState::sleep) lv_draw_line(context, &line, &a, &b);
        else {
            // LVGL 8's native dashes only cover horizontal/vertical lines.
            const double length = std::hypot(b.x - a.x, b.y - a.y);
            auto along = [&](double distance) {
                const auto t = length ? distance / length : 0;
                return lv_point_t{static_cast<lv_coord_t>(a.x + std::lround((b.x - a.x) * t)),
                                  static_cast<lv_coord_t>(a.y + std::lround((b.y - a.y) * t))};
            };
            for (double offset = 0; offset < length; offset += 10) {
                const auto begin = along(offset), end = along(std::min(length, offset + 5));
                lv_draw_line(context, &line, &begin, &end);
            }
        }
    });
    const auto& latest = view.history_->latest();
    if (view.history_->size() && latest.percent <= 100 && latest.state != UsageState::unknown) {
        const auto y = static_cast<lv_coord_t>(area.y2 - latest.percent * h / 100);
        const lv_area_t dot{static_cast<lv_coord_t>(area.x2 - 4), static_cast<lv_coord_t>(y - 2), area.x2, static_cast<lv_coord_t>(y + 2)};
        lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = color(usage_color(latest.state)); dsc.radius = LV_RADIUS_CIRCLE;
        lv_draw_rect(context, &dsc, &dot);
    }
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
        if (controller_.live()) {
            registration_weight_label_ = label(box, "?", 18, 102, 280, &lv_font_montserrat_48);
            registration_status_ = label(box, "", 18, 165, 416, &lv_font_montserrat_14, muted);
            label(box, "Place empty equipment on the platform.", 18, 204, 416);
        } else {
            label(box, "156.8 g", 18, 102, 280, &lv_font_montserrat_48);
            label(box, "Simulated reading - no load cell attached", 18, 165, 416, &lv_font_montserrat_14, muted);
            button(box, registration_stable_ ? "Stable reading" : "Unstable - wait", 18, 196, 250, 46, Action::stable, 0, registration_stable_);
        }
    }
    button(box, "Back", 16, 254, 90, 40, Action::register_back);
    auto* next = button(box, "Continue", 290, 254, 144, 40, Action::register_next, 0, true);
    registration_next_ = next;
    if (registration_step_ == 1 && !registration_stable_) lv_obj_add_state(next, LV_STATE_DISABLED);
}
void View::action(Action a, int value) {
    switch (a) {
    case Action::battery_history: open(Dialog::battery_history); return;
    case Action::history_range:
        if (dialog_ != Dialog::battery_history || pending_draw_) return;
        if (value == 6 || value == 24 || value == 72) { history_hours_ = value; schedule_draw(); }
        return;
    case Action::tare:
        if (!controller_.live() || controller_.session().phase != Phase::ready) return;
        if (!controller_.scale().calibration()) {
            controller_.scale().cancel_calibration();
            calibration_message_text_.clear();
            open(Dialog::calibration);
            return;
        }
        controller_.tare();
        break;
    case Action::calibration:
        controller_.scale().cancel_calibration();
        calibration_message_text_.clear(); open(Dialog::calibration); return;
    case Action::capture_zero:
        calibration_message_text_ = controller_.scale().capture_zero() ? "" : "Wait for a stable reading.";
        break;
    case Action::calibrate:
        calibration_message_text_ = controller_.scale().calibrate(static_cast<float>(value)) ?
            "Calibrated. Remove known mass." : "Capture zero, then stable mass.";
        break;
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
    case Action::request_remove:
        if (pending_draw_ || dialog_ != Dialog::objects || controller_.session().phase != Phase::ready) return;
        remove_equipment_id_ = static_cast<std::uint32_t>(value);
        dialog_ = Dialog::remove_equipment;
        break;
    case Action::confirm_remove:
        if (pending_draw_ || dialog_ != Dialog::remove_equipment || controller_.session().phase != Phase::ready) return;
        controller_.remove_equipment(remove_equipment_id_);
        dialog_ = Dialog::objects;
        break;
    case Action::cancel_remove: dialog_ = Dialog::objects; break;
    case Action::objects: open(Dialog::objects); return;
    case Action::dose:
        if (pending_draw_ || dialog_ != Dialog::none || controller_.session().phase != Phase::ready) return;
        if (controller_.save_coffee()) { refresh_values(); return; }
        open(Dialog::dose); return;
    case Action::toggle_mode:
        controller_.set_mode(controller_.session().mode == Mode::espresso ? Mode::pour_over : Mode::espresso);
        break;
    case Action::demo: open(Dialog::demo); return;
    case Action::registration: open(Dialog::registration); return;
    case Action::close: controller_.scale().cancel_calibration(); dialog_ = Dialog::none; break;
    case Action::choose_object: controller_.select_equipment(static_cast<std::size_t>(value)); dialog_ = Dialog::none; break;
    case Action::choose_scenario: controller_.load_scenario(static_cast<Scenario>(value)); dialog_ = Dialog::none; break;
    case Action::run: if (!controller_.live()) controller_.run_demo(); break;
    case Action::pause: controller_.pause_demo(); break;
    case Action::finish: controller_.finish(); break;
    case Action::next: controller_.next(); break;
    case Action::reset: reset(); return;
    case Action::remove: controller_.remove_object(); break;
    case Action::stable: registration_stable_ = !registration_stable_; break;
    case Action::choose_kind:
        if (dialog_ != Dialog::registration || registration_step_ != 0 || value < 0 || value > 2) return;
        registration_kind_ = static_cast<Kind>(value);
        break;
    case Action::register_next:
        if (controller_.live() && registration_step_ == 1) {
            const auto snapshot = registration_measurement_.update(controller_.scale());
            if (!snapshot) return;
            registration_weight_ = snapshot->grams;
            registration_stable_ = true;
        }
        if (registration_step_ == 0 && kind_input_) {
            registration_kind_ = static_cast<Kind>(lv_dropdown_get_selected(kind_input_));
        }
        if (registration_step_ == 0 || (registration_step_ == 1 && registration_stable_)) ++registration_step_;
        break;
    case Action::register_back:
        registration_measurement_.reset();
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
        set_text(icon_name_, wide() ? kind_name(registration_kind_) : name.c_str());
        return;
    }
    case Action::register_save: {
        if (registration_step_ != 2 || dialog_ != Dialog::registration) return;
        const auto registration_name = std::string(kind_name(registration_kind_)) + " " + equipment_icons[registration_icon_];
        const char* config = registration_kind_ == Kind::cup ? "Container only" : registration_kind_ == Kind::portafilter ?
            "Clean, with basket" : "Dripper + server + filter";
        if (controller_.register_equipment({0, registration_kind_, registration_name, config, controller_.live() ? registration_weight_ : 156.8F, std::nullopt}, registration_stable_))
        {
            if (wide()) {
                controller_.select_equipment(controller_.equipment_count() - 1);
                dialog_ = Dialog::none;
            } else dialog_ = Dialog::objects;
        }
        else registration_error_ = "Equipment list full";
        break;
    }
    }
    controller_.set_auto_brew_enabled(dialog_ == Dialog::none || dialog_ == Dialog::battery_history);
    schedule_draw();
}
}

