#include "device.hxx"
#include "config.hxx"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch_ft6336.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace smart_scale::device::display {
Device& device() { static Device instance; return instance; }
void Device::init() {
    if (initialized_) return;
    // Hide undefined panel RAM before rail setup, reset delays and LVGL startup.
    // Preload the output latch before enabling the pin to avoid a high pulse.
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(config::lcd_backlight), 0));
    ESP_ERROR_CHECK(gpio_set_direction(static_cast<gpio_num_t>(config::lcd_backlight), GPIO_MODE_OUTPUT));
    i2c_master_bus_config_t i2c{};
    i2c.i2c_port = I2C_NUM_0;
    i2c.sda_io_num = static_cast<gpio_num_t>(config::i2c_sda);
    i2c.scl_io_num = static_cast<gpio_num_t>(config::i2c_scl);
    i2c.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c.glitch_ignore_cnt = 7;
    i2c.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c, &bus_));

    // DCDC1 is the board's existing 3.3 V rail. Match the vendor's PWM fix;
    // leave other rail voltages and battery charging policy untouched.
    auto& pmu = pmu_;
    i2c_device_config_t pmu_config{};
    pmu_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    pmu_config.device_address = 0x34;
    pmu_config.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_, &pmu_config, &pmu));
    const std::uint8_t reg = 0x81;
    std::uint8_t pwm{};
    ESP_ERROR_CHECK(i2c_master_transmit_receive(pmu, &reg, 1, &pwm, 1, 100));
    const std::uint8_t write[] = {reg, static_cast<std::uint8_t>(pwm | (1U << 2))};
    ESP_ERROR_CHECK(i2c_master_transmit(pmu, write, sizeof(write), 100));


    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(bus_, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &expander_));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(expander_, IO_EXPANDER_PIN_NUM_1, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander_, IO_EXPANDER_PIN_NUM_1, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander_, IO_EXPANDER_PIN_NUM_1, 1));
    vTaskDelay(pdMS_TO_TICKS(100));

    constexpr unsigned pixels = config::screen_width * 20;
    spi_bus_config_t spi{};
    spi.mosi_io_num = config::lcd_mosi;
    spi.miso_io_num = -1;
    spi.sclk_io_num = config::lcd_clock;
    spi.quadwp_io_num = -1;
    spi.quadhd_io_num = -1;
    spi.max_transfer_sz = pixels * sizeof(lv_color_t); // Bytes, not pixels.
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io{};
    io.cs_gpio_num = -1;
    io.dc_gpio_num = config::lcd_dc;
    io.spi_mode = 0;
    io.pclk_hz = 40000000; // Conservative first bring-up rate.
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &panel_io_));
    esp_lcd_panel_dev_config_t panel{};
    panel.reset_gpio_num = -1;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(panel_io_, &panel, &panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    esp_lcd_panel_io_i2c_config_t touch_io{};
    touch_io.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT6336_ADDRESS;
    touch_io.control_phase_bytes = 1;
    touch_io.lcd_cmd_bits = 8;
    touch_io.flags.disable_control_phase = 1;
    touch_io.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus_, &touch_io, &touch_io_));
    esp_lcd_touch_config_t touch{};
    touch.x_max = 320;
    touch.y_max = 480;
    touch.rst_gpio_num = GPIO_NUM_NC;
    touch.int_gpio_num = GPIO_NUM_NC;
    touch.flags.swap_xy = true;
    touch.flags.mirror_y = true;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft6336(touch_io_, &touch, &touch_));

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    lvgl_port_display_cfg_t display{};
    display.io_handle = panel_io_;
    display.panel_handle = panel_;
    display.buffer_size = pixels;
    display.double_buffer = true;
    display.hres = config::screen_width;
    display.vres = config::screen_height;
    display.rotation.swap_xy = true;
    display.rotation.mirror_x = true;
    display.rotation.mirror_y = true;
    display.flags.buff_dma = true;
    display.flags.sw_rotate = true;
    display_ = lvgl_port_add_disp(&display);
    ESP_ERROR_CHECK(display_ ? ESP_OK : ESP_ERR_NO_MEM);
    lvgl_port_touch_cfg_t input{};
    input.disp = display_;
    input.handle = touch_;
    input_ = lvgl_port_add_touch(&input);
    ESP_ERROR_CHECK(input_ ? ESP_OK : ESP_ERR_NO_MEM);
    if (lvgl_port_lock(0)) {
        touch_read_ = input_->driver->read_cb;
        input_->driver->read_cb = read_touch;
        lvgl_port_unlock();
    }

    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_1;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t light{};
    light.gpio_num = config::lcd_backlight;
    light.speed_mode = LEDC_LOW_SPEED_MODE;
    light.channel = LEDC_CHANNEL_0;
    light.timer_sel = LEDC_TIMER_1;
    light.duty = 0; // Remain dark until show() completes the first full refresh.
    ESP_ERROR_CHECK(ledc_channel_config(&light));
    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << config::reset_button;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&button));
    initialized_ = true;
    ESP_LOGI("smart-scale", "Waveshare ST7796/FT6336: 480x320 landscape");
}
esp_err_t Device::show() {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    // These are partial draw buffers, not full framebuffers. Repaint every pixel
    // of panel RAM with the actual UI while the backlight remains off.
    lv_obj_invalidate(lv_disp_get_scr_act(display_));
    lv_refr_now(display_);
    // LVGL may return with the final SPI DMA transfer still in flight. The
    // port's transfer-complete callback clears flushing without taking its lock.
    auto* draw_buf = lv_disp_get_draw_buf(display_);
    const auto deadline = esp_timer_get_time() + 1000000;
    while (draw_buf->flushing) {
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    auto error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 818);
    if (error != ESP_OK) return error;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void Device::read_touch(lv_indev_drv_t* driver, lv_indev_data_t* data) {
    auto& self = device();
    self.touch_read_(driver, data);
    self.touch_down_ = data->state == LV_INDEV_STATE_PRESSED;
    // Holding a finger is user activity; physical release is still required
    // before delivering a second gesture after wake.
    if (self.touch_down_) self.touch_activity_ = true;
    if (!self.touch_gate_.filter(self.touch_down_, self.power_state_ == PowerState::screen_off))
        data->state = LV_INDEV_STATE_RELEASED;
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
        const unsigned duty = state == PowerState::active ? 818 : state == PowerState::dimmed ? 245 : 0;
        error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        if (error == ESP_OK) error = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        if (error == ESP_OK && state == PowerState::screen_off) lv_timer_pause(display_->refr_timer);
    }
    if (error == ESP_OK) power_state_ = state;
    return error;
}
BatterySnapshot Device::battery() {
    auto read = [this](std::uint8_t reg, std::uint8_t& value) {
        return i2c_master_transmit_receive(pmu_, &reg, 1, &value, 1, 20) == ESP_OK;
    };
    std::uint8_t id{}, status0{}, status1{}, percent{};
    if (!read(0x03, id) || id != 0x4a || !read(0x00, status0) || !read(0x01, status1)) return {};
    const bool soc_valid = read(0xa4, percent);
    auto value = decode_axp2101(status0, status1, soc_valid ? percent : 255);
    return value;
}
bool Device::sleep_supported() const {
#ifdef CONFIG_SMART_SCALE_TOUCH_WAKE_VERIFIED
    constexpr int pin = CONFIG_SMART_SCALE_TOUCH_WAKE_GPIO;
    // Only exposed GPIOs outside memory/USB and this adapter's existing pins.
    return GPIO_IS_VALID_GPIO(pin) && pin != config::reset_button && pin != config::lcd_mosi &&
        pin != config::lcd_clock && pin != config::lcd_dc && pin != config::lcd_backlight &&
        pin != config::i2c_sda && pin != config::i2c_scl && pin != config::hx711_dout &&
        pin != config::hx711_sck && pin != 19 && pin != 20 && !(pin >= 26 && pin <= 37);
#else
    return false;
#endif
}
esp_err_t Device::light_sleep(uint64_t& started_us, uint64_t& ended_us) {
    started_us = ended_us = 0;
#ifdef CONFIG_SMART_SCALE_TOUCH_WAKE_VERIFIED
    if (!sleep_supported() || touch_down_ || touch_activity_) return ESP_ERR_INVALID_STATE;
    const auto pin = static_cast<gpio_num_t>(CONFIG_SMART_SCALE_TOUCH_WAKE_GPIO);
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << pin; cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    auto error = gpio_config(&cfg);
    if (error != ESP_OK) return error;
    if (!gpio_get_level(pin)) return ESP_ERR_INVALID_STATE;
    error = gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
    if (error != ESP_OK) return error;
    error = esp_sleep_enable_gpio_wakeup();
    if (error != ESP_OK) { gpio_wakeup_disable(pin); return error; }
    // LVGL lock prevents touch I2C/rendering while the main task sleeps.
    // Timer is stopped to avoid advancing animations during the pause.
    error = lvgl_port_stop();
    if (error == ESP_OK) {
        if (gpio_get_level(pin)) {
            started_us = esp_timer_get_time();
            error = esp_light_sleep_start();
            ended_us = esp_timer_get_time();
        }
        else error = ESP_ERR_INVALID_STATE;
        lvgl_port_resume();
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    gpio_wakeup_disable(pin);
    // Suppress the waking press, even if the panel becomes bright before poll.
    touch_gate_.filter(true, true);
    return error;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool Device::reset_pressed() {
    // Called at 100 ms: two consecutive low samples, once per press.
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

