#include "board349.hxx"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    namespace b = smart_scale::board349;
    // Unique indices expose transposed axes, reversal, strip seams and overrun.
    std::vector<unsigned> logical(b::frame_pixels), physical(b::frame_pixels);
    for (std::size_t i = 0; i < logical.size(); ++i) logical[i] = i;
    for (int y = 0; y < b::native_height; y += b::strip_rows)
        b::pack_strip(logical.data(), physical.data() + y * b::native_width, y);
    std::vector<bool> seen(b::frame_pixels);
    for (const auto index : physical) { assert(index < seen.size()); assert(!seen[index]); seen[index] = true; }
    assert(physical.front() == 171 * 640);
    assert(physical[171] == 0);
    assert(physical[639 * 172] == 640 * 172 - 1);
    assert(physical.back() == 639);
    // Independent inverse mapping checks every pixel across all ten DMA strips.
    for (int y = 0; y < b::height; ++y)
        for (int x = 0; x < b::width; ++x)
            assert(physical[x * b::native_width + (b::height - 1 - y)] == logical[y * b::width + x]);

    std::array<std::uint8_t, 32> packet{};
    auto sample = [&](int x, int y, unsigned count) {
        packet[1] = count; packet[2] = (x >> 8) & 15; packet[3] = x & 255;
        packet[4] = (y >> 8) & 15; packet[5] = y & 255;
    };
    b::TouchPoint point;
    for (const auto x : {0, 639}) for (const auto y : {0, 171}) {
        sample(x, y, 1);
        assert(b::decode_touch(packet.data(), packet.size(), point));
        assert(point.x == 639 - x && point.y == 171 - y);
    }
    sample(319, 85, 1); assert(b::decode_touch(packet.data(), 32, point));
    assert(point.x == 320 && point.y == 86);
    for (const auto count : {0, 2, 4, 255}) {
        sample(10, 10, count); assert(!b::decode_touch(packet.data(), 32, point));
    }
    sample(640, 0, 1); assert(!b::decode_touch(packet.data(), 32, point));
    sample(0, 172, 1); assert(!b::decode_touch(packet.data(), 32, point));
    sample(4095, 4095, 1); assert(!b::decode_touch(packet.data(), 32, point));
    sample(0, 0, 1); assert(!b::decode_touch(packet.data(), 31, point));
    assert(!b::decode_touch(nullptr, 32, point));
    std::puts("Rev1.1 frame rotation, DMA strip seams, touch bounds and release checks passed");
}
