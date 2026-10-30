#pragma once
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_io_expander.h"
#include "lvgl.h"
#include "power.hxx"
#include "battery.hxx"

namespace smart_scale::device::display {
class Device final {
public:
    Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    void init();
    // Call with the LVGL port lock held, after constructing the initial UI.
    esp_err_t show();
    bool reset_pressed();
    esp_err_t power_state(PowerState state); // LVGL lock required
    bool take_touch_activity(); // LVGL lock required
    bool touch_down() const { return touch_down_; }
    BatterySnapshot battery(); // LVGL lock required; read-only PMU access
    bool sleep_supported() const;
    esp_err_t light_sleep(); // acquisition stopped; LVGL lock held

private:
    static void read_touch(lv_indev_drv_t*, lv_indev_data_t*);
    void (*touch_read_)(lv_indev_drv_t*, lv_indev_data_t*){};
    lv_indev_t* input_{};
    WakeTouchGate touch_gate_;
    PowerState power_state_{PowerState::active};
    bool touch_activity_{}, touch_down_{};
    i2c_master_dev_handle_t pmu_{};
    i2c_master_bus_handle_t bus_{};
    esp_io_expander_handle_t expander_{};
    esp_lcd_panel_io_handle_t panel_io_{}, touch_io_{};
    esp_lcd_panel_handle_t panel_{};
    esp_lcd_touch_handle_t touch_{};
    lv_disp_t* display_{};
    bool initialized_{}, button_down_{};
    unsigned button_ticks_{};
};
Device& device();
}
