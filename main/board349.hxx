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
inline constexpr std::size_t touch_packet_bytes = 14;

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
enum class TouchState { released, pressed, invalid, no_data };
struct TouchReport {
    TouchState state{TouchState::invalid};
    unsigned contacts{};
    TouchPoint point{};
    const char* reason{"size"};
};
inline TouchReport decode_touch_report(const std::uint8_t* packet, std::size_t size) {
    if (!packet || size != touch_packet_bytes) return {};
    // Board policy: a uniform full-length response means zero contacts.
    bool repeated = true;
    for (std::size_t i = 1; i < size; ++i) repeated = repeated && packet[i] == packet[0];
    if (repeated) return {TouchState::released, 0, {}, "repeated_idle"};
    if (packet[1] > 2) return {TouchState::invalid, 0, {}, "count"};
    if (packet[1] == 0) return {TouchState::released, 0, {}, "release"};
    TouchPoint first{};
    for (unsigned i = 0; i < packet[1]; ++i) {
        const auto offset = 2 + 6 * i;
        const int x = ((packet[offset] & 0x0f) << 8) | packet[offset+1];
        const int y = ((packet[offset+2] & 0x0f) << 8) | packet[offset+3];
        if (x >= width || y >= height) return {TouchState::invalid, 0, {}, "coordinates"};
        if (i == 0) first = {width - 1 - x, height - 1 - y};
    }
    return {TouchState::pressed, packet[1], first, "contact"};
}
// Board-observed idle pattern, not a generic invalid-packet fallback. Require
// fresh repeated successful reads. Sleep additionally requires a quiet shared IRQ.
class TouchIdleQualifier {
public:
    enum class Use { sleep, ui };
    explicit TouchIdleQualifier(Use use = Use::sleep) : use_(use) {}
    TouchReport update(TouchReport report, bool io_ok, bool irq_high, std::uint64_t now_us) {
        if (!io_ok || report.state != TouchState::no_data || (use_ == Use::sleep && !irq_high)) {
            reset();
            return io_ok ? report : TouchReport{};
        }
        if (!samples_ || now_us < last_us_ || now_us - last_us_ > (use_ == Use::sleep ? 100000U : 1000000U)) {
            since_us_ = now_us; samples_ = 0;
        }
        last_us_ = now_us;
        if (samples_ < 3) ++samples_;
        if (samples_ >= 3 && now_us - since_us_ >= 300000)
            return {TouchState::released, 0, {}, "repeated_idle_qualified"};
        return report;
    }
    void reset() { samples_ = 0; since_us_ = last_us_ = 0; }
private:
    Use use_;
    unsigned samples_{};
    std::uint64_t since_us_{}, last_us_{};
};
inline bool decode_touch(const std::uint8_t* packet, std::size_t size, TouchPoint& point) {
    const auto report = decode_touch_report(packet, size);
    if (report.state != TouchState::pressed || report.contacts != 1) return false;
    point = report.point;
    return true;
}
static_assert(touch_command[7] == touch_packet_bytes);
static_assert(native_height % strip_rows == 0);
static_assert(width == native_height && height == native_width);
static_assert(hx711_dout != touch_sda && hx711_dout != touch_scl);
static_assert(hx711_sck != touch_sda && hx711_sck != touch_scl);
}
