#pragma once
#include <cstddef>
#include <cstdint>

// Waveshare ESP32-S3-Touch-LCD-3.49 V2, PCB Rev1.1 only.
// Pin map and panel protocol: vendor ESP-IDF/09_LVGL_V8_Test.
namespace smart_scale::board349 {
inline constexpr int width = 640, height = 172;
inline constexpr int native_width = 172, native_height = 640;
inline constexpr int strip_rows = 64;
inline constexpr std::size_t frame_pixels = width * height;
inline constexpr std::size_t strip_pixels = native_width * strip_rows;
inline constexpr int i2c_sda = 47, i2c_scl = 48;
inline constexpr int touch_sda = 17, touch_scl = 18;
inline constexpr int lcd_cs = 9, lcd_clock = 10;
inline constexpr int lcd_d0 = 11, lcd_d1 = 12, lcd_d2 = 13, lcd_d3 = 14;
inline constexpr int lcd_backlight = 42;
inline constexpr int expander_interrupt = 8;
inline constexpr std::uint32_t backlight_enable = 1U << 1;
inline constexpr std::uint32_t lcd_reset = 1U << 5;
inline constexpr std::uint32_t system_enable = 1U << 6;
inline constexpr int hx711_dout = 44, hx711_sck = 43;
inline constexpr std::uint8_t touch_address = 0x3b;
inline constexpr std::uint8_t touch_command[] = {0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 0x0e, 0, 0, 0};
inline constexpr std::size_t touch_packet_bytes = 32;

// Rotate the landscape frame clockwise into the controller's native raster.
// Each strip continues the same RAMWR stream; arbitrary dirty rectangles are
// not supported by the vendor's QSPI RAMWRC implementation.
constexpr std::size_t source_index(int native_x, int native_y) {
    return static_cast<std::size_t>(height - 1 - native_x) * width + native_y;
}
template <class Pixel>
void pack_strip(const Pixel* frame, Pixel* strip, int first_row) {
    for (int y = 0; y < strip_rows; ++y)
        for (int x = 0; x < native_width; ++x)
            strip[y * native_width + x] = frame[source_index(x, first_row + y)];
}
struct TouchPoint { int x{}, y{}; };
inline bool decode_touch(const std::uint8_t* packet, std::size_t size, TouchPoint& point) {
    // Treat release, multi-touch and malformed/out-of-range data as released.
    // A failed bus transaction must never re-use a prior pressed state.
    if (size != touch_packet_bytes || !packet || packet[1] != 1) return false;
    const int x = ((packet[2] & 0x0f) << 8) | packet[3];
    const int y = ((packet[4] & 0x0f) << 8) | packet[5];
    if (x >= width || y >= height) return false;
    point = {width - 1 - x, height - 1 - y};
    return true;
}
static_assert(native_height % strip_rows == 0);
static_assert(width == native_height && height == native_width);
static_assert(hx711_dout != touch_sda && hx711_dout != touch_scl);
static_assert(hx711_sck != touch_sda && hx711_sck != touch_scl);
}
