#include "device.hxx"
#include "config.hxx"
#include "board349.hxx"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(board::lcd_backlight), 0));
    ESP_ERROR_CHECK(gpio_set_direction(static_cast<gpio_num_t>(board::lcd_backlight), GPIO_MODE_OUTPUT));
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
    // The expander API requires output mode before setting levels. GPIO42
    // already holds PWM low, so BL_EN cannot illuminate uninitialized RAM.
    ESP_ERROR_CHECK(esp_io_expander_set_dir(expander_, board::backlight_enable | board::lcd_reset | board::system_enable,
                                          IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander_, board::backlight_enable, 0));
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

    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.timer_num = LEDC_TIMER_1;
    timer.freq_hz = 50000;
    timer.clk_cfg = LEDC_USE_RC_FAST_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t light{};
    light.gpio_num = board::lcd_backlight;
    light.speed_mode = LEDC_LOW_SPEED_MODE;
    light.channel = LEDC_CHANNEL_0;
    light.timer_sel = LEDC_TIMER_1;
    light.duty = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&light));

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
    initialized_ = true;
    ESP_LOGI(tag, "expander SDA=47 SCL=48; touch SDA=17 SCL=18; HX711 DOUT=44 SCK=43");
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
        return; // A disconnect must release a held dose button, not reboot.
    }
    if (self.touch_failed_) ESP_LOGI(tag, "Touch communication recovered");
    self.touch_failed_ = false;
    board::TouchPoint point;
    if (board::decode_touch(packet, sizeof(packet), point)) {
        self.last_touch_ = {static_cast<lv_coord_t>(point.x), static_cast<lv_coord_t>(point.y)};
        data->point = self.last_touch_;
        data->state = LV_INDEV_STATE_PRESSED;
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
    // Bring-up diagnostic: bypass PWM and hold GPIO42 HIGH continuously.
    // Keep the dark startup sequence above; only enable after DMA completes.
    auto error = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1);
    if (error != ESP_OK) {
        ESP_LOGE(tag, "Backlight GPIO42 forced HIGH failed: %s", esp_err_to_name(error));
        return error;
    }
    error = esp_io_expander_set_level(expander_, board::backlight_enable, 1);
    ESP_LOGI(tag, "Backlight forced ON (GPIO42 HIGH, EXIO1 enabled): %s", esp_err_to_name(error));
    return error;
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
