#include "lcd_sleep.hxx"
#include "config.hxx"
#include <cassert>
#include <vector>
using namespace smart_scale;
int main() {
    static_assert(lcd_qspi_command(0x10) == 0x02001000);
    static_assert(lcd_qspi_command(0x11) == 0x02001100);
    static_assert(config::wake_button_pin_valid(-1, 44, 43));
    static_assert(config::wake_button_pin_valid(38, 44, 43));
    static_assert(!config::wake_button_pin_valid(38, 38, 43));
    static_assert(!config::wake_button_pin_valid(42, 44, 43));
    static_assert(!config::wake_button_pin_valid(0, 44, 43));
    // Inject a failure at every command, including uncertain Sleep In delivery.
    for (int failure = -1; failure < 4; ++failure) {
        LcdSleep panel;
        std::vector<int> events;
        int index = 0;
        auto send = [&](uint8_t cmd) { events.push_back(cmd); return index++ != failure; };
        auto wait = [&](unsigned ms) { events.push_back(-static_cast<int>(ms)); };
        const bool entered = panel.enter(send, wait);
        assert(entered == (failure != 0 && failure != 1));
        assert(panel.needs_restore());
        assert(!panel.enter(send, wait)); // No double sleep before rollback.
        const bool restored = panel.restore(send, wait);
        if (!restored) {
            assert(panel.needs_restore());
            assert(panel.restore([](uint8_t) { return true; }, wait));
        }
        assert(!panel.needs_restore());
        const auto size = events.size();
        assert(panel.restore(send, wait));
        assert(events.size() == size);
        if (failure == -1) assert((events == std::vector<int>{0x28, 0x10, -150, 0x11, -150, 0x29}));
        if (failure == 1) assert((events == std::vector<int>{0x28, 0x10, -150, 0x11, -150, 0x29}));
    }
    // A late IRQ / rejected CPU sleep uses the same successful restore path.
    LcdSleep panel;
    auto ok = [](uint8_t) { return true; };
    auto wait = [](unsigned) {};
    for (int i = 0; i < 100; ++i) {
        assert(panel.enter(ok, wait));
        assert(panel.restore(ok, wait));
    }
}
