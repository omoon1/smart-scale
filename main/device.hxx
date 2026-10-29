#pragma once
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_io_expander.h"
#include "lvgl.h"

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
private:
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
