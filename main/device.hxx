#pragma once
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_io_expander.h"
#include "lvgl.h"
#include "power.hxx"
#include "battery.hxx"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
    BatterySnapshot battery(); // LVGL lock required; calibrated ADC voltage estimate
    bool sleep_supported() const;
    esp_err_t light_sleep(uint64_t& started_us, uint64_t& ended_us); // acquisition stopped; LVGL lock held

private:
    static bool transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*);
    static void flush(lv_disp_drv_t*, const lv_area_t*, lv_color_t*);
    static void read_touch(lv_indev_drv_t*, lv_indev_data_t*);
    i2c_master_bus_handle_t bus_{}, touch_bus_{};
    i2c_master_dev_handle_t touch_{};
    SemaphoreHandle_t transfer_done_{};
    lv_color_t* dma_strip_{};
    lv_disp_draw_buf_t draw_buffer_{};
    lv_disp_drv_t display_driver_{};
    lv_indev_drv_t touch_driver_{};
    lv_point_t last_touch_{};
    bool touch_failed_{};
    WakeTouchGate touch_gate_;
    PowerState power_state_{PowerState::active};
    bool touch_activity_{}, touch_down_{};
    adc_oneshot_unit_handle_t adc_{};
    adc_cali_handle_t adc_calibration_{};
    i2c_master_dev_handle_t expander_input_{};
    bool wake_line_idle();
    esp_io_expander_handle_t expander_{};
    esp_lcd_panel_io_handle_t panel_io_{};
    esp_lcd_panel_handle_t panel_{};
    lv_disp_t* display_{};
    bool initialized_{}, button_down_{};
    unsigned button_ticks_{};
};
Device& device();
}
