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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace smart_scale::device::display {
Device& device() { static Device instance; return instance; }
void Device::init() {
    if (initialized_) return;
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
    i2c_master_dev_handle_t pmu{};
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
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(pmu));

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
    ESP_ERROR_CHECK(lvgl_port_add_touch(&input) ? ESP_OK : ESP_ERR_NO_MEM);

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
    light.duty = 818;
    ESP_ERROR_CHECK(ledc_channel_config(&light));
    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << config::reset_button;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&button));
    initialized_ = true;
    ESP_LOGI("smart-scale", "Waveshare ST7796/FT6336: 480x320 landscape, dummy data");
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
