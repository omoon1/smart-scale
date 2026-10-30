#pragma once
#include "session.hxx"
#include "weight_display.hxx"
#include "registration_measurement.hxx"
#include "lvgl.h"
#include "battery.hxx"
#include "battery_history.hxx"
#include <ctime>

namespace smart_scale::ui {
enum class Dialog { none, calibration, objects, dose, demo, registration, remove_equipment, battery_history, battery_clock };
class View {
public:
    explicit View(Controller& controller) : controller_(controller) {}
    void init();
    void update();
    void battery(const BatterySnapshot& value) { battery_ = value; }
    void history(const BatteryHistoryData& value, bool clock_valid = true) { history_ = value; history_clock_valid_ = clock_valid; }
    std::optional<std::time_t> take_clock_request() { auto value = clock_request_; clock_request_.reset(); return value; }
    bool power_inhibited() const { return (dialog_ != Dialog::none && dialog_ != Dialog::battery_history) || controller_.session().phase == Phase::brewing; }
    void open(Dialog dialog);
    void reset();
private:
    enum class Action { tare, calibration, capture_zero, calibrate, objects, dose, toggle_mode, demo, close,
        request_remove, confirm_remove, cancel_remove, dose_adjust, dose_preset, choose_object, choose_scenario, run, pause, finish, next,
        battery_history, battery_clock, save_clock, reset, remove, registration, register_next, register_back, register_save, choose_icon, stable };
    struct Binding { View* view{}; Action action{}; int value{}; };
    lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int w, int h,
                     Action action, int value = 0, bool primary = false);
    void draw();
    void draw_ready(lv_obj_t* root);
    void draw_brew(lv_obj_t* root, bool result);
    void draw_dialog(lv_obj_t* root);
    void draw_history(lv_obj_t* parent);
    void refresh_history();
    void draw_clock(lv_obj_t* parent);
    void draw_registration(lv_obj_t* parent);
    void action(Action action, int value);
    static void clicked(lv_event_t* event);
    static void dose_pressed(lv_event_t* event);
    static void deferred_draw(void* context);
    void schedule_draw();
    void refresh_values();
    Controller& controller_;
    BatterySnapshot battery_;
    BatteryHistoryData history_{};
    bool history_clock_valid_{};
    std::array<std::array<lv_obj_t*, 5>, 8> history_rows_{};
    std::array<lv_obj_t*, 5> clock_fields_{};
    lv_obj_t* history_status_{};
    lv_obj_t* clock_error_{};
    std::optional<std::time_t> clock_request_;
    lv_obj_t* battery_label_{};
    WeightDisplay weight_display_;
    WeightDisplay gross_display_;
    WeightDisplay counts_display_{1.0F, 150.0F, 0.0F};
    std::string last_logged_weight_;
    std::uint64_t last_weight_log_ms_{};
    std::uint32_t display_epoch_{};
    Phase display_phase_{Phase::ready};
    std::array<Binding, 64> bindings_{};
    std::size_t binding_count_{};
    lv_obj_t *screen_{}, *weight_{}, *weight_unit_{}, *time_{}, *flow_{}, *status_{}, *object_text_{}, *dose_text_{},
             *target_text_{}, *chart_{}, *dose_value_{}, *icon_name_{}, *kind_input_{},
             *chart_max_{}, *chart_time_{}, *current_point_{}, *object_weight_{};
    lv_chart_series_t *series_{}, *target_series_{};
    Phase rendered_phase_{Phase::ready};
    Dialog dialog_{Dialog::none};
    bool pending_draw_{};
    std::uint32_t remove_equipment_id_{};
    int registration_step_{};
    Kind registration_kind_{Kind::cup};
    bool registration_stable_{true};
    RegistrationMeasurement registration_measurement_;
    unsigned registration_icon_{};
    std::array<lv_obj_t*, 20> icon_buttons_{};
    std::string registration_error_;
    float registration_weight_{156.8F};
    lv_obj_t *start_button_{}, *tare_button_{};
    lv_obj_t *coffee_card_{}, *coffee_hint_{}, *coffee_check_{}, *object_card_{}, *object_check_{}, *stage_text_{};
    int coffee_visual_state_{-1};
    bool object_visual_selected_{};
    lv_obj_t *sensor_status_{}, *calibration_message_{}, *calibration_weight_label_{}, *registration_weight_label_{}, *registration_status_{}, *registration_next_{};
    std::string calibration_message_text_;
};
}

