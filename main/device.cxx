#include "device.hxx"
#include "diagnostic_logger.hxx"
#include "config.hxx"
#include "board349.hxx"
#include "expander_wake.hxx"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "sdkconfig.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/task.h"

namespace smart_scale::device::display {
namespace {
namespace board = board349;
constexpr const char* tag = "display";
auto& log() { return smart_scale::device::DiagnosticLogger::instance(); }
// Match the V2 example; do not apply another panel's long power/gamma table.
const axs15231b_lcd_init_cmd_t init_commands[] = {
    {0x11, nullptr, 0, 100}, // Sleep out.
    {0x29, nullptr, 0, 100}, // Display on; backlight remains gated off.
};
}
Device& device() { static Device instance; return instance; }
void Device::init() {
    if (initialized_) return;
    ESP_LOGI(tag, "Waveshare 3.49 V2/Rev1.1: AXS15231B QSPI, 640x172 landscape");
    // Keep the panel dark through reset and the first complete DMA refresh.
    const auto backlight = static_cast<gpio_num_t>(board::lcd_backlight);
    ESP_ERROR_CHECK(gpio_reset_pin(backlight));
    ESP_ERROR_CHECK(gpio_set_level(backlight, 1));
    // gpio_config routes the pad to the GPIO output, with input enabled for
    // pad-level diagnostics. PWM attaches later, while EXIO1 is still off.
    gpio_config_t light{};
    light.pin_bit_mask = 1ULL << board::lcd_backlight;
    light.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&light));
    i2c_master_bus_config_t i2c{};
    i2c.i2c_port = I2C_NUM_0;
    i2c.sda_io_num = static_cast<gpio_num_t>(board::i2c_sda);
    i2c.scl_io_num = static_cast<gpio_num_t>(board::i2c_scl);
    i2c.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c.glitch_ignore_cnt = 7;
    i2c.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c, &bus_));
    // V2 has a TCA9554 at 0x20, not the old board's AXP2101 PMU at 0x34.
    const auto probe = i2c_master_probe(bus_, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, 100);
    if (probe != ESP_OK) ESP_LOGE(tag, "Rev1.1 expander missing: SDA=47 SCL=48 addr=0x20: %s", esp_err_to_name(probe));
    ESP_ERROR_CHECK(probe);
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(bus_, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &expander_));
    // The expander API requires output mode before setting levels. Hold the
    // active-low brightness control HIGH until EXIO1 gates the backlight off.
    ESP_ERROR_CHECK(esp_io_expander_set_dir(expander_, board::backlight_enable | board::lcd_reset | board::system_enable,
                                          IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander_, board::backlight_enable, 0));
    // Waveshare V2 uses duty=0 for maximum brightness. EXIO1 now keeps the
    // panel dark while GPIO42 stays LOW through initialization and operation.
    ESP_ERROR_CHECK(gpio_set_level(backlight, 0));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander_, board::lcd_reset | board::system_enable, 1));

    i2c.i2c_port = I2C_NUM_1;
    i2c.sda_io_num = static_cast<gpio_num_t>(board::touch_sda);
    i2c.scl_io_num = static_cast<gpio_num_t>(board::touch_scl);
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c, &touch_bus_));
    i2c_device_config_t touch_config{};
    touch_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    touch_config.device_address = board::touch_address;
    touch_config.scl_speed_hz = 300000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(touch_bus_, &touch_config, &touch_));

    transfer_done_ = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(transfer_done_ ? ESP_OK : ESP_ERR_NO_MEM);
    dma_strip_ = static_cast<lv_color_t*>(heap_caps_malloc(board::strip_pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    auto* frame = static_cast<lv_color_t*>(heap_caps_calloc(board::frame_pixels, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_ERROR_CHECK(dma_strip_ && frame ? ESP_OK : ESP_ERR_NO_MEM);
    static_assert(sizeof(lv_color_t) == 2, "AXS15231B requires RGB565");

    spi_bus_config_t spi{};
    spi.data0_io_num = board::lcd_d0;
    spi.data1_io_num = board::lcd_d1;
    spi.data2_io_num = board::lcd_d2;
    spi.data3_io_num = board::lcd_d3;
    spi.sclk_io_num = board::lcd_clock;
    spi.max_transfer_sz = board::strip_pixels * sizeof(lv_color_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io{};
    io.cs_gpio_num = board::lcd_cs;
    io.dc_gpio_num = -1;
    io.spi_mode = 3;
    io.pclk_hz = 40000000;
    io.trans_queue_depth = 1; // One owned DMA strip; wait before rewriting it.
    io.on_color_trans_done = transfer_done;
    io.user_ctx = this;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &panel_io_));
    axs15231b_vendor_config_t vendor{};
    vendor.flags.use_qspi_interface = 1;
    vendor.init_cmds = init_commands;
    vendor.init_cmds_size = sizeof(init_commands) / sizeof(init_commands[0]);
    esp_lcd_panel_dev_config_t panel{};
    panel.reset_gpio_num = -1; // Reset is EXIO5, not a CPU GPIO on Rev1.1.
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    panel.vendor_config = &vendor;
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io_, &panel, &panel_));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander_, board::lcd_reset, 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander_, board::lcd_reset, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    // Use the port's task/tick/mutex, but a board-specific full-frame flush.
    // The generic partial-area rotation path cannot drive this QSPI RAM stream.
    ESP_ERROR_CHECK(lvgl_port_lock(0) ? ESP_OK : ESP_ERR_TIMEOUT);
    lv_disp_draw_buf_init(&draw_buffer_, frame, nullptr, board::frame_pixels);
    lv_disp_drv_init(&display_driver_);
    display_driver_.hor_res = board::width;
    display_driver_.ver_res = board::height;
    display_driver_.draw_buf = &draw_buffer_;
    display_driver_.full_refresh = 1;
    display_driver_.flush_cb = flush;
    display_driver_.user_data = this;
    display_ = lv_disp_drv_register(&display_driver_);
    ESP_ERROR_CHECK(display_ ? ESP_OK : ESP_ERR_NO_MEM);
    lv_indev_drv_init(&touch_driver_);
    touch_driver_.type = LV_INDEV_TYPE_POINTER;
    touch_driver_.disp = display_;
    touch_driver_.read_cb = read_touch;
    touch_driver_.user_data = this;
    ESP_ERROR_CHECK(lv_indev_drv_register(&touch_driver_) ? ESP_OK : ESP_ERR_NO_MEM);
    lvgl_port_unlock();

    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << config::reset_button;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&button));
    // V2: BAT_ADC = GPIO4 / ADC1 channel 3, 200k/100k divider.
    adc_oneshot_unit_init_cfg_t adc_config{};
    adc_config.unit_id = ADC_UNIT_1;
    auto adc_error = adc_oneshot_new_unit(&adc_config, &adc_);
    adc_oneshot_chan_cfg_t channel{};
    channel.atten = ADC_ATTEN_DB_12; channel.bitwidth = ADC_BITWIDTH_12;
    if (adc_error == ESP_OK) adc_error = adc_oneshot_config_channel(adc_, ADC_CHANNEL_3, &channel);
    adc_cali_curve_fitting_config_t calibration{};
    calibration.unit_id = ADC_UNIT_1; calibration.chan = ADC_CHANNEL_3;
    calibration.atten = ADC_ATTEN_DB_12; calibration.bitwidth = ADC_BITWIDTH_12;
    if (adc_error == ESP_OK) adc_error = adc_cali_create_scheme_curve_fitting(&calibration, &adc_calibration_);
    if (adc_error != ESP_OK) ESP_LOGW(tag, "Battery ADC unavailable: %s", esp_err_to_name(adc_error));

    // Active-low brightness: zero duty is full brightness, not off.
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.timer_num = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_10_BIT; timer.freq_hz = 20000; timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t pwm{};
    pwm.gpio_num = board::lcd_backlight; pwm.speed_mode = LEDC_LOW_SPEED_MODE;
    pwm.channel = LEDC_CHANNEL_0; pwm.timer_sel = LEDC_TIMER_0; pwm.duty = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&pwm));
    ESP_ERROR_CHECK(gpio_input_enable(backlight)); // retain actual pad readback after PWM routing
    ESP_ERROR_CHECK(esp_io_expander_set_dir(expander_, 1U << 0, IO_EXPANDER_INPUT));
    i2c_device_config_t expander_config{};
    expander_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    expander_config.device_address = 0x20; expander_config.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_, &expander_config, &expander_input_));
    gpio_config_t wake{};
    wake.pin_bit_mask = 1ULL << board::expander_interrupt;
    wake.mode = GPIO_MODE_INPUT; wake.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&wake));
    initialized_ = true;
    ESP_LOGI(tag, "expander SDA=47 SCL=48; touch SDA=17 SCL=18; HX711 DOUT=%d SCK=%d", config::hx711_dout, config::hx711_sck);
}
bool Device::transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* context) {
    auto& self = *static_cast<Device*>(context);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(self.transfer_done_, &woken);
    return woken == pdTRUE;
}
void Device::flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* pixels) {
    auto& self = *static_cast<Device*>(driver->user_data);
    // A partial refresh would corrupt the continuation stream. Fail visibly
    // instead of reading beyond the supplied pixel buffer after a config change.
    ESP_ERROR_CHECK(area->x1 == 0 && area->y1 == 0 && area->x2 == board::width - 1 && area->y2 == board::height - 1
                    ? ESP_OK : ESP_ERR_INVALID_SIZE);
    for (int y = 0; y < board::native_height; y += board::strip_rows) {
        board::pack_strip(pixels, self.dma_strip_, y);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(self.panel_, 0, y, board::native_width, y + board::strip_rows, self.dma_strip_));
        // Never reuse a buffer until DMA has finished, including the last strip.
        // A bounded wait also prevents show() or the UI task hanging on bus loss.
        ESP_ERROR_CHECK(xSemaphoreTake(self.transfer_done_, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT);
    }
    lv_disp_flush_ready(driver);
}
void Device::read_touch(lv_indev_drv_t* driver, lv_indev_data_t* data) {
    auto& self = *static_cast<Device*>(driver->user_data);
    ++self.touch_polls_;
    self.last_touch_poll_us_ = esp_timer_get_time();
    const bool previous_down = self.touch_down_;
    const bool previous_gate = self.touch_gate_.suppressed();
    data->state = LV_INDEV_STATE_RELEASED;
    data->point = self.last_touch_;
    std::uint8_t packet[board::touch_packet_bytes]{};
    board::TouchReport report;
    const auto result = self.read_touch_packet(packet, report);
    if (self.touch_resume_pending_) {
        log().event("touch", "event=post_resume_read cycle=%u result=%s latency_us=%llu raw_contact_byte=%u",
            self.wake_cycle_, esp_err_to_name(result),
            static_cast<unsigned long long>(esp_timer_get_time() - self.touch_resume_us_),
            static_cast<unsigned>(packet[1]));
        self.touch_resume_pending_ = false;
    }
    if (result != ESP_OK) {
        ++self.touch_errors_;
        if (!self.touch_failed_) log().event("touch", "event=i2c_error result=%s state=%d gate=%d errors=%llu",
            esp_err_to_name(result), static_cast<int>(self.power_state_), previous_gate,
            static_cast<unsigned long long>(self.touch_errors_));
        self.touch_failed_ = true;
        self.touch_down_ = false;
        return; // Release widgets without clearing the wake gesture gate.
    }
    self.last_touch_ok_us_ = self.last_touch_poll_us_;
    if (self.touch_failed_) log().event("touch", "event=i2c_recovered polls=%llu errors=%llu",
        static_cast<unsigned long long>(self.touch_polls_), static_cast<unsigned long long>(self.touch_errors_));
    self.touch_failed_ = false;
    if (report.state == board::TouchState::pressed && report.contacts == 1) {
        self.last_touch_ = {static_cast<lv_coord_t>(report.point.x), static_cast<lv_coord_t>(report.point.y)};
        data->point = self.last_touch_;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    self.touch_down_ = report.state == board::TouchState::pressed;
    if (report.state == board::TouchState::invalid) ++self.touch_invalid_;
    if (self.touch_down_) self.touch_activity_ = true;
    // Invalid data never releases the wake gate. Valid multi-touch blocks sleep
    // but is not delivered to single-touch widgets.
    if (report.state == board::TouchState::released || report.state == board::TouchState::pressed) {
        if (!self.touch_gate_.filter(self.touch_down_, self.power_state_ == PowerState::screen_off))
            data->state = LV_INDEV_STATE_RELEASED;
    }
    if (!previous_gate && self.touch_gate_.suppressed()) self.gate_since_us_ = self.last_touch_poll_us_;
    if (!self.touch_gate_.suppressed()) self.gate_since_us_ = 0;
    const bool delivered = data->state == LV_INDEV_STATE_PRESSED;
    if (previous_gate != self.touch_gate_.suppressed() ||
        ((previous_down != self.touch_down_ || delivered != self.last_delivered_) &&
         self.last_touch_poll_us_ - self.touch_log_us_ >= 250000)) {
        log().event("touch", "event=input contacts=%u raw_down=%d delivered=%d gate=%d x=%d y=%d state=%d gpio8=%d",
            report.contacts, self.touch_down_, delivered, self.touch_gate_.suppressed(),
            static_cast<int>(data->point.x), static_cast<int>(data->point.y), static_cast<int>(self.power_state_),
            gpio_get_level(static_cast<gpio_num_t>(board::expander_interrupt)));
        self.touch_log_us_ = self.last_touch_poll_us_;
    }
    self.last_delivered_ = delivered;
}
void Device::suppress_wake_touch() {
    touch_gate_.filter(true, true);
    gate_since_us_ = esp_timer_get_time();
}
void Device::log_touch_health() {
    const auto now = static_cast<uint64_t>(esp_timer_get_time());
    // Never read TCA9554 inputs just for logging: that acknowledges its IRQ.
    log().event("touch", "event=health polls=%llu errors=%llu invalid=%llu poll_age_us=%llu ok_age_us=%llu down=%d gate=%d gpio8=%d state=%d gate_age_us=%llu",
        static_cast<unsigned long long>(touch_polls_), static_cast<unsigned long long>(touch_errors_),
        static_cast<unsigned long long>(touch_invalid_), static_cast<unsigned long long>(last_touch_poll_us_ ? now-last_touch_poll_us_ : 0),
        static_cast<unsigned long long>(last_touch_ok_us_ ? now-last_touch_ok_us_ : 0),
        touch_down_, touch_gate_.suppressed(), gpio_get_level(static_cast<gpio_num_t>(board::expander_interrupt)), static_cast<int>(power_state_),
        static_cast<unsigned long long>(gate_since_us_ ? now - gate_since_us_ : 0));
}
esp_err_t Device::show() {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    // Caller holds the same LVGL port lock used by the UI task. flush() waits
    // for every DMA completion, so no transfer remains in flight on return.
    log().event("power", "event=frame_begin state=%d", static_cast<int>(power_state_));
    lv_obj_invalidate(lv_disp_get_scr_act(display_));
    lv_refr_now(display_);
    log().event("power", "event=frame_end");
    // Restore active-low full brightness behind EXIO1 before releasing the gate.
    // GPIO42 HIGH would darken this panel.
    auto error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    if (error == ESP_OK) error = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (error == ESP_OK) error = esp_io_expander_set_level(expander_, board::backlight_enable, 1);
    if (error != ESP_OK) {
        ESP_LOGE(tag, "Backlight EXIO1 enable failed: %s", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(tag, "Backlight enabled: EXIO1 HIGH, GPIO42 LOW; %s; pad=%d (expected 0)",
             esp_err_to_name(error), gpio_get_level(static_cast<gpio_num_t>(board::lcd_backlight)));

    // The expander driver's direction/output getters return cached values.
    // Use a temporary read-only handle to inspect the actual TCA9554 registers.
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000;
    config.scl_speed_hz = 400000;
    i2c_master_dev_handle_t diagnostic{};
    error = i2c_master_bus_add_device(bus_, &config, &diagnostic);
    if (error != ESP_OK) {
        ESP_LOGE(tag, "Backlight register diagnostic setup failed: %s", esp_err_to_name(error));
        return error;
    }
    const uint8_t registers[] = {0x03, 0x01, 0x00}; // Direction, output latch, input.
    uint8_t values[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        error = i2c_master_transmit_receive(diagnostic, &registers[i], 1, &values[i], 1, 100);
        if (error != ESP_OK) {
            ESP_LOGE(tag, "TCA9554 register 0x%02x read failed: %s", registers[i], esp_err_to_name(error));
            break;
        }
    }
    const auto cleanup = i2c_master_bus_rm_device(diagnostic);
    if (error == ESP_OK) {
        ESP_LOGI(tag, "TCA9554 hardware readback: config=0x%02x output=0x%02x input=0x%02x; EXIO1 dir=%s latch=%d pad=%d",
                 values[0], values[1], values[2], (values[0] & board::backlight_enable) ? "INPUT" : "OUTPUT",
                 !!(values[1] & board::backlight_enable), !!(values[2] & board::backlight_enable));
        if ((values[0] & board::backlight_enable) || !(values[1] & board::backlight_enable) ||
            !(values[2] & board::backlight_enable))
            ESP_LOGW(tag, "Backlight EXIO1 readback mismatch: expected OUTPUT, latch=1, pad=1");
        error = cleanup;
    }
    return error;
}

bool Device::take_touch_activity() {
    const bool result = touch_activity_; touch_activity_ = false; return result;
}
esp_err_t Device::power_state(PowerState state) {
    if (state == power_state_) return ESP_OK;
    esp_err_t error;
    if (state == PowerState::active && power_state_ == PowerState::screen_off) {
        lv_timer_resume(display_->refr_timer);
        error = show(); // full frame before backlight
    } else {
        const unsigned duty = state == PowerState::active ? 0 : state == PowerState::dimmed ? 716 : 1023;
        error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        if (error == ESP_OK) error = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        if (error == ESP_OK && state == PowerState::screen_off)
            error = esp_io_expander_set_level(expander_, board::backlight_enable, 0);
        if (error == ESP_OK && state == PowerState::screen_off) lv_timer_pause(display_->refr_timer);
    }
    log().event("power", "event=backlight from=%d to=%d duty=%u result=%s",
        static_cast<int>(power_state_), static_cast<int>(state),
        state == PowerState::active ? 0U : state == PowerState::dimmed ? 716U : 1023U, esp_err_to_name(error));
    if (error == ESP_OK) power_state_ = state;
    return error;
}
BatterySnapshot Device::battery() {
    int total = 0;
    esp_err_t error = adc_ && adc_calibration_ ? ESP_OK : ESP_ERR_INVALID_STATE;
    for (int i = 0; error == ESP_OK && i < 16; ++i) {
        int raw{}, mv{};
        error = adc_oneshot_read(adc_, ADC_CHANNEL_3, &raw);
        if (error == ESP_OK) error = adc_cali_raw_to_voltage(adc_calibration_, raw, &mv);
        total += mv;
    }
    const int mv = error == ESP_OK ? total * 3 / 16 : -1;
    const auto battery = error == ESP_OK ? decode_voltage_battery(mv) : BatterySnapshot{};
    const auto now = static_cast<uint64_t>(esp_timer_get_time());
    const bool validity_changed = decode_voltage_battery(battery_last_mv_).valid != battery.valid;
    if (!battery_log_us_ || error != battery_last_error_ || validity_changed ||
        std::abs(mv - battery_last_mv_) >= 30 || now - battery_log_us_ >= 60000000) {
        log().event("battery", "event=sample result=%s mv=%d valid=%d estimated_pct=%d source=unknown charging=unknown state=%d",
            esp_err_to_name(error), mv, battery.valid, battery.percent ? static_cast<int>(*battery.percent) : -1,
            static_cast<int>(power_state_));
        battery_log_us_ = now; battery_last_mv_ = mv; battery_last_error_ = error;
    }
    return battery;
}
bool Device::sleep_supported() const {
#ifdef CONFIG_SMART_SCALE_TOUCH_WAKE_VERIFIED
    return expander_input_ != nullptr;
#else
    return false;
#endif
}
esp_err_t Device::read_touch_packet(uint8_t* packet, board::TouchReport& report) {
    // Match the vendor driver: STOP after command, then a separate read;
    // request and receive exactly two six-byte records plus the two-byte header.
    auto error = i2c_master_transmit(touch_, board::touch_command, sizeof(board::touch_command), 20);
    if (error == ESP_OK) error = i2c_master_receive(touch_, packet, board::touch_packet_bytes, 20);
    const auto now = static_cast<uint64_t>(esp_timer_get_time());
    const auto raw = board::decode_touch_report(packet, board::touch_packet_bytes);
    report = touch_idle_.update(raw, error == ESP_OK,
        gpio_get_level(static_cast<gpio_num_t>(board::expander_interrupt)) != 0, now);
    if (error != ESP_OK) return error;
    const auto state = static_cast<int>(report.state);
    const bool changed = state != packet_log_state_ || report.contacts != packet_log_count_;
    if (!packet_log_us_ || ((changed || report.state == board::TouchState::invalid) && now - packet_log_us_ >= 1000000) ||
        now - packet_log_us_ >= 30000000) {
        char hex[board::touch_packet_bytes * 3 + 1]{};
        for (unsigned i = 0; i < board::touch_packet_bytes; ++i)
            std::snprintf(hex + i * 3, 4, "%02x ", static_cast<unsigned>(packet[i]));
        log().event("touch", "event=packet state=%d reason=%s raw_state=%d raw_contact_byte=%u contacts=%u hex=%s",
            state, report.reason, static_cast<int>(raw.state), static_cast<unsigned>(packet[1]), report.contacts, hex);
        packet_log_us_ = now; packet_log_state_ = state; packet_log_count_ = report.contacts;
    }
    return ESP_OK;
}
esp_err_t Device::touch_pressed(bool& pressed) {
    uint8_t packet[board::touch_packet_bytes]{};
    pressed = false;
    board::TouchReport report;
    const auto error = read_touch_packet(packet, report);
    if (error != ESP_OK) return error;
    if (report.state == board::TouchState::invalid || report.state == board::TouchState::no_data)
        return ESP_ERR_INVALID_RESPONSE;
    pressed = report.state == board::TouchState::pressed;
    return ESP_OK;
}
esp_err_t Device::light_sleep(uint64_t& started_us, uint64_t& ended_us, bool& user_wake, unsigned cycle) {
    started_us = ended_us = 0;
    user_wake = false;
    log().event("power", "event=sleep_prepare cycle=%u supported=%d down=%d activity=%d gate=%d gpio8=%d",
        cycle, sleep_supported(), touch_down_, touch_activity_, touch_gate_.suppressed(),
        gpio_get_level(static_cast<gpio_num_t>(board::expander_interrupt)));
#ifdef CONFIG_SMART_SCALE_TOUCH_WAKE_VERIFIED
    if (!sleep_supported() || touch_down_ || touch_activity_) return ESP_ERR_INVALID_STATE;
    constexpr auto pin = static_cast<gpio_num_t>(board::expander_interrupt);
    auto error = lvgl_port_stop();
    log().event("power", "event=lvgl_stop cycle=%u result=%s", cycle, esp_err_to_name(error));
    if (error != ESP_OK) return error;
    bool gpio_armed = false, wake_enabled = false;
    uint32_t baseline{};
    auto read_port = [this, cycle](uint32_t& value) {
        const uint8_t reg = 0; uint8_t inputs{};
        const auto result = i2c_master_transmit_receive(expander_input_, &reg, 1, &inputs, 1, 20);
        value = inputs;
        log().event("power", "event=expander_read cycle=%u result=%s inputs=0x%02x valid=%d gpio8=%d",
            cycle, esp_err_to_name(result), static_cast<unsigned>(inputs), result == ESP_OK,
            gpio_get_level(static_cast<gpio_num_t>(board::expander_interrupt)));
        return result == ESP_OK;
    };
    auto read_touch = [this, cycle](bool& pressed) {
        const auto result = touch_pressed(pressed);
        log().event("touch", "event=sleep_probe cycle=%u result=%s pressed=%d", cycle, esp_err_to_name(result), pressed);
        return result == ESP_OK;
    };
    // V2 has EXIO0 touch and no PMU IRQ. Never treat EXIO5 (LCD reset) as PMU.
    const auto preparation = prepare_expander_wake(read_port, read_touch,
        [pin] { return gpio_get_level(pin) != 0; }, baseline, 1U, 0U);
    log().event("power", "event=prepared cycle=%u preparation=%d baseline=0x%02x gpio8=%d", cycle,
        static_cast<int>(preparation), static_cast<unsigned>(baseline), gpio_get_level(pin));
    if (preparation != WakePreparation::ready) {
        user_wake = preparation == WakePreparation::touch;
        error = preparation == WakePreparation::io_error ? ESP_FAIL : ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK) {
        error = gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
        gpio_armed = error == ESP_OK;
    }
    if (error == ESP_OK) {
        error = esp_sleep_enable_gpio_wakeup();
        wake_enabled = error == ESP_OK;
    }
    log().event("power", "event=sleep_arm cycle=%u result=%s gpio_armed=%d wake_enabled=%d wake_gpio=%d",
        cycle, esp_err_to_name(error), gpio_armed, wake_enabled, static_cast<int>(pin));
    // No expander read after this check: it could acknowledge a newly arrived touch.
    if (error == ESP_OK && !gpio_get_level(pin)) {
        error = ESP_ERR_INVALID_STATE;
        log().event("power", "event=sleep_abort cycle=%u reason=irq_low_before_entry", cycle);
    }
    if (error == ESP_OK) {
        const auto started = esp_timer_get_time();
        error = esp_light_sleep_start();
        const auto returned = esp_timer_get_time();
        log().event("power", "event=sleep_return cycle=%u result=%s elapsed_us=%llu wake_cause=%d gpio8=%d",
            cycle, esp_err_to_name(error), static_cast<unsigned long long>(returned-started),
            error == ESP_OK ? static_cast<int>(esp_sleep_get_wakeup_cause()) : -1, gpio_get_level(pin));
        if (error == ESP_OK) {
            started_us = started; ended_us = returned;
            uint32_t inputs{}; bool pressed{};
            const bool port_ok = read_port(inputs);
            const bool touch_ok = read_touch(pressed);
            const auto reason = classify_expander_wake(baseline, inputs, pressed, port_ok && touch_ok, 1U);
            user_wake = reason != ExpanderWake::shared;
            log().event("power", "event=wake_classified cycle=%u reason=%d before=0x%02x after=0x%02x touch=%d",
                cycle, static_cast<int>(reason), static_cast<unsigned>(baseline), static_cast<unsigned>(inputs), pressed);
        }
    }
    const auto source_cleanup = wake_enabled ? esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO) : ESP_OK;
    const auto gpio_cleanup = gpio_armed ? gpio_wakeup_disable(pin) : ESP_OK;
    log().event("power", "event=wake_cleanup cycle=%u source=%s gpio=%s", cycle,
        esp_err_to_name(source_cleanup), esp_err_to_name(gpio_cleanup));
    battery_log_us_ = 0; // Include the first post-attempt voltage sample even if unchanged.
    wake_cycle_ = cycle;
    touch_resume_us_ = esp_timer_get_time();
    touch_resume_pending_ = true;
    touch_idle_.reset(); // Sleep time cannot qualify an idle/release interval.
    const auto resumed = lvgl_port_resume();
    log().event("power", "event=lvgl_resume cycle=%u result=%s user_wake=%d", cycle, esp_err_to_name(resumed), user_wake);
    if (user_wake) suppress_wake_touch();
    log().event("touch", "event=wake_gate cycle=%u suppressed=%d", cycle, touch_gate_.suppressed());
    return error;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool Device::reset_pressed() {
    if (gpio_get_level(static_cast<gpio_num_t>(config::reset_button)) != 0) {
        button_ticks_ = 0;
        button_down_ = false;
    } else if (!button_down_ && ++button_ticks_ >= 2) {
        button_down_ = true;
        return true;
    }
    return false;
}
}
