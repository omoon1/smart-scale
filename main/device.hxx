#pragma once
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_io_expander.h"
#include "lvgl.h"
#include "power.hxx"
#include "board349.hxx"
#include "battery.hxx"
#include "lcd_sleep.hxx"
#include "peripheral_sleep.hxx"
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
    bool wake_button_pressed() const; // Optional active-low wake-only input.
    esp_err_t power_state(PowerState state); // LVGL lock required
    bool take_touch_activity(); // LVGL lock required
    bool touch_down() const { return touch_down_; }
    BatterySnapshot battery(); // LVGL lock required; calibrated ADC voltage estimate
    bool sleep_supported() const;
    void log_touch_health();
    void suppress_wake_touch(); // LVGL lock required
    esp_err_t qualify_touch_for_sleep(bool& pressed); // bounded fresh polling; LVGL lock required
    esp_err_t touch_pressed(bool& pressed); // LVGL lock required; no expander acknowledgement
    esp_err_t light_sleep(uint64_t& started_us, uint64_t& ended_us, bool& user_wake, unsigned cycle = 0); // acquisition stopped; LVGL lock held

private:
    bool lcd_command(uint8_t command);
    esp_err_t restore_lcd();
    esp_err_t prepare_peripherals();
    esp_err_t restore_peripherals();
    bool peripheral_read(uint8_t address, uint8_t reg, uint8_t& value);
    bool peripheral_write(uint8_t address, uint8_t reg, uint8_t value);
    PeripheralSleep peripheral_sleep_;
    i2c_master_dev_handle_t imu_{}, rtc_{};
    bool peripherals_quiet_{};
    LcdSleep lcd_sleep_;
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
    uint64_t touch_polls_{}, touch_errors_{}, touch_invalid_{}, last_touch_poll_us_{}, last_touch_ok_us_{};
    uint64_t touch_log_us_{}, battery_log_us_{};
    int battery_last_mv_{-1}, battery_last_error_{-1};
    bool last_delivered_{}, touch_resume_pending_{};
    unsigned wake_cycle_{};
    uint64_t touch_resume_us_{};
    uint64_t packet_log_us_{}, gate_since_us_{};
    int packet_log_state_{-1};
    unsigned packet_log_count_{};
    board349::TouchIdleQualifier touch_idle_;
    board349::TouchIdleQualifier ui_touch_idle_{board349::TouchIdleQualifier::Use::ui};
    esp_err_t read_touch_packet(uint8_t* packet, board349::TouchReport& report, bool for_ui = false, board349::TouchReport* raw_report = nullptr);
    WakeTouchGate touch_gate_;
    PowerState power_state_{PowerState::active};
    bool touch_activity_{}, touch_down_{};
    adc_oneshot_unit_handle_t adc_{};
    adc_cali_handle_t adc_calibration_{};
    i2c_master_dev_handle_t expander_input_{};
    esp_io_expander_handle_t expander_{};
    esp_lcd_panel_io_handle_t panel_io_{};
    esp_lcd_panel_handle_t panel_{};
    lv_disp_t* display_{};
    bool initialized_{}, button_down_{};
    unsigned button_ticks_{};
};
Device& device();
}

