#include "device.hxx"
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
    data->state = LV_INDEV_STATE_RELEASED;
    data->point = self.last_touch_;
    std::uint8_t packet[board::touch_packet_bytes]{};
    const auto result = i2c_master_transmit_receive(self.touch_, board::touch_command, sizeof(board::touch_command),
                                                   packet, sizeof(packet), 20);
    if (result != ESP_OK) {
        if (!self.touch_failed_) ESP_LOGW(tag, "Touch read failed on SDA=17 SCL=18 addr=0x3b: %s", esp_err_to_name(result));
        self.touch_failed_ = true;
        self.touch_down_ = false;
        return; // Release widgets without clearing the wake gesture gate.
    }
    if (self.touch_failed_) ESP_LOGI(tag, "Touch communication recovered");
    self.touch_failed_ = false;
    board::TouchPoint point;
    if (board::decode_touch(packet, sizeof(packet), point)) {
        self.last_touch_ = {static_cast<lv_coord_t>(point.x), static_cast<lv_coord_t>(point.y)};
        data->point = self.last_touch_;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    self.touch_down_ = data->state == LV_INDEV_STATE_PRESSED;
    if (self.touch_down_) self.touch_activity_ = true;
    // Only an explicit zero-contact packet clears suppression. Malformed or
    // multi-touch reports must not turn a waking gesture into a widget press.
    if (packet[1] == 0 || self.touch_down_) {
        if (!self.touch_gate_.filter(self.touch_down_, self.power_state_ == PowerState::screen_off))
            data->state = LV_INDEV_STATE_RELEASED;
    }
}
esp_err_t Device::show() {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    // Caller holds the same LVGL port lock used by the UI task. flush() waits
    // for every DMA completion, so no transfer remains in flight on return.
    ESP_LOGI(tag, "First frame refresh starting; backlight held off");
    lv_obj_invalidate(lv_disp_get_scr_act(display_));
    lv_refr_now(display_);
    ESP_LOGI(tag, "First frame refresh complete; forcing backlight ON");
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
    if (error == ESP_OK) power_state_ = state;
    return error;
}
BatterySnapshot Device::battery() {
    if (!adc_ || !adc_calibration_) return {};
    int total = 0;
    for (int i = 0; i < 16; ++i) {
        int raw{}, mv{};
        if (adc_oneshot_read(adc_, ADC_CHANNEL_3, &raw) != ESP_OK ||
            adc_cali_raw_to_voltage(adc_calibration_, raw, &mv) != ESP_OK) return {};
        total += mv;
    }
    return decode_voltage_battery(total * 3 / 16);
}
bool Device::sleep_supported() const {
#ifdef CONFIG_SMART_SCALE_TOUCH_WAKE_VERIFIED
    return expander_input_ != nullptr;
#else
    return false;
#endif
}
esp_err_t Device::touch_pressed(bool& pressed) {
    uint8_t packet[board::touch_packet_bytes]{};
    const auto error = i2c_master_transmit_receive(touch_, board::touch_command,
        sizeof(board::touch_command), packet, sizeof(packet), 20);
    // Any reported contact blocks sleep, including multi-touch/invalid coordinates.
    pressed = error == ESP_OK && packet[1] != 0;
    return error;
}
esp_err_t Device::light_sleep(uint64_t& started_us, uint64_t& ended_us, bool& user_wake) {
    started_us = ended_us = 0;
    user_wake = false;
#ifdef CONFIG_SMART_SCALE_TOUCH_WAKE_VERIFIED
    if (!sleep_supported() || touch_down_ || touch_activity_) return ESP_ERR_INVALID_STATE;
    constexpr auto pin = static_cast<gpio_num_t>(board::expander_interrupt);
    auto error = lvgl_port_stop();
    if (error != ESP_OK) return error;
    bool gpio_armed = false, wake_enabled = false;
    uint32_t baseline{};
    auto read_port = [this](uint32_t& value) {
        const uint8_t reg = 0; uint8_t inputs{};
        const auto result = i2c_master_transmit_receive(expander_input_, &reg, 1, &inputs, 1, 20);
        value = inputs;
        return result == ESP_OK;
    };
    auto read_touch = [this](bool& pressed) { return touch_pressed(pressed) == ESP_OK; };
    // V2 has EXIO0 touch and no PMU IRQ. Never treat EXIO5 (LCD reset) as PMU.
    const auto preparation = prepare_expander_wake(read_port, read_touch,
        [pin] { return gpio_get_level(pin) != 0; }, baseline, 1U, 0U);
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
    if (error == ESP_OK) ESP_LOGI("power", "arming light sleep: wake_gpio=%d", static_cast<int>(pin));
    // No expander read after this check: it could acknowledge a newly arrived touch.
    if (error == ESP_OK && !gpio_get_level(pin)) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK) {
        const auto started = esp_timer_get_time();
        error = esp_light_sleep_start();
        if (error == ESP_OK) {
            started_us = started; ended_us = esp_timer_get_time();
            uint32_t inputs{}; bool pressed{};
            const bool port_ok = read_port(inputs);
            const bool touch_ok = read_touch(pressed);
            const auto reason = classify_expander_wake(baseline, inputs, pressed, port_ok && touch_ok, 1U);
            user_wake = reason != ExpanderWake::shared;
            ESP_LOGI("power", "expander wake: reason=%d before=0x%02x after=0x%02x touch=%d",
                static_cast<int>(reason), static_cast<unsigned>(baseline), static_cast<unsigned>(inputs), pressed);
        }
    }
    if (wake_enabled) esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (gpio_armed) gpio_wakeup_disable(pin);
    const auto resumed = lvgl_port_resume();
    if (resumed != ESP_OK) ESP_LOGE("power", "LVGL resume failed: %s", esp_err_to_name(resumed));
    if (user_wake) touch_gate_.filter(true, true);
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
