#pragma once
#include "session.hxx"
#include "lvgl.h"

namespace smart_scale::ui {
enum class Dialog { none, objects, dose, demo, registration };
class View {
public:
    explicit View(Controller& controller) : controller_(controller) {}
    void init();
    void update();
    void open(Dialog dialog);
    void reset();
private:
    enum class Action { objects, dose, toggle_mode, demo, close,
        choose_object, choose_scenario, run, pause, finish, next,
        reset, remove, registration, register_next, register_back, register_save, stable };
    struct Binding { View* view{}; Action action{}; int value{}; };
    lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int w, int h,
                     Action action, int value = 0, bool primary = false);
    void draw();
    void draw_ready(lv_obj_t* root);
    void draw_brew(lv_obj_t* root, bool result);
    void draw_dialog(lv_obj_t* root);
    void draw_registration(lv_obj_t* parent);
    void action(Action action, int value);
    static void clicked(lv_event_t* event);
    static void dose_changed(lv_event_t* event);
    static void deferred_draw(void* context);
    void schedule_draw();
    void refresh_values();
    Controller& controller_;
    std::array<Binding, 64> bindings_{};
    std::size_t binding_count_{};
    lv_obj_t *screen_{}, *weight_{}, *time_{}, *flow_{}, *status_{}, *object_text_{}, *dose_text_{},
             *target_text_{}, *chart_{}, *roller_{}, *name_input_{}, *kind_input_{},
             *chart_max_{}, *chart_time_{}, *current_point_{}, *object_weight_{};
    lv_chart_series_t *series_{}, *target_series_{};
    Phase rendered_phase_{Phase::ready};
    Dialog dialog_{Dialog::none};
    bool pending_draw_{};
    int registration_step_{};
    Kind registration_kind_{Kind::cup};
    bool registration_stable_{true};
    std::string registration_name_, registration_error_;
};
}
