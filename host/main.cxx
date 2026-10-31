#include "ui.hxx"
#include "config.hxx"
#include "equipment_icons.hxx"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#ifdef SMART_SCALE_SDL
#include <SDL.h>
#endif
LV_FONT_DECLARE(equipment_icons_22);
LV_FONT_DECLARE(equipment_icons_32);
namespace {
int width = 480, height = 320;
std::array<std::uint32_t, 640 * 320> framebuffer{};
std::array<lv_color_t, 640 * 40> buffer{};
int mouse_x{}, mouse_y{};
bool mouse_down{};
unsigned flush_count{};
void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* pixels) {
    ++flush_count;
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            const auto rgb = lv_color_to32(*pixels++);
            if (x >= 0 && x < width && y >= 0 && y < height) framebuffer[y * width + x] = rgb;
        }
    lv_disp_flush_ready(driver);
}
void input(lv_indev_drv_t*, lv_indev_data_t* data) {
    data->point.x = static_cast<lv_coord_t>(mouse_x);
    data->point.y = static_cast<lv_coord_t>(mouse_y);
    data->state = mouse_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
void pump(smart_scale::ui::View& view, unsigned ms = 40) {
    lv_tick_inc(ms); view.update(); lv_timer_handler(); lv_refr_now(nullptr);
}
void click(smart_scale::ui::View& view, int x, int y) {
    mouse_x = x; mouse_y = y; mouse_down = true; pump(view);
    mouse_down = false; pump(view); pump(view);
}
void screenshot(smart_scale::ui::View& view, const char* name) {
    pump(view); pump(view);
    const auto path = std::filesystem::path("build-host/screens") / (std::string(name) + ".ppm");
    auto* file = std::fopen(path.c_str(), "wb");
    if (!file) std::exit(1);
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; ++i) {
        const auto pixel = framebuffer[i];
        const unsigned char bytes[] = {static_cast<unsigned char>(pixel >> 16),
            static_cast<unsigned char>(pixel >> 8), static_cast<unsigned char>(pixel)};
        std::fwrite(bytes, 1, 3, file);
    }
    std::fclose(file);
    std::cout << path.string() << '\n';
}
void require(bool value, const char* message) {
    if (!value) { std::cerr << "UI test failed: " << message << '\n'; std::exit(1); }
}

lv_obj_t* find_type(lv_obj_t* parent, const lv_obj_class_t* type) {
    if (lv_obj_check_type(parent, type)) return parent;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i)
        if (auto* found = find_type(lv_obj_get_child(parent, static_cast<int>(i)), type)) return found;
    return nullptr;
}
bool has_label(lv_obj_t* parent, const std::string& text) {
    if (lv_obj_check_type(parent, &lv_label_class) && text == lv_label_get_text(parent)) return true;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i)
        if (has_label(lv_obj_get_child(parent, static_cast<int>(i)), text)) return true;
    return false;
}

void run_wide_checks(smart_scale::ui::View& view, smart_scale::Controller& controller) {
    using namespace smart_scale;
    std::filesystem::create_directories("build-host/screens");
    screenshot(view, "wide-01-ready");
    click(view, 520, 70); screenshot(view, "wide-02-dose");
    require(has_label(lv_scr_act(), "+0.1"), "wide coffee card opens dose");
    click(view, 328, 76); click(view, 386, 132);
    require(std::abs(*controller.session().dose_g - 20.1F) < .01F, "wide preset and fine adjustment");
    mouse_x = 386; mouse_y = 132; mouse_down = true;
    for (int i = 0; i < 25; ++i) pump(view, 40);
    const auto held = *controller.session().dose_g;
    mouse_down = false; pump(view);
    require(held > 20.1F && *controller.session().dose_g == held, "wide press hold and release");
    click(view, 610, 26);
    click(view, 330, 70); screenshot(view, "wide-03-objects");
    require(has_label(lv_scr_act(), "+ Register\nequipment"), "wide object card opens list");
    click(view, 454, 100); screenshot(view, "wide-04-register-type");
    auto* type = find_type(lv_scr_act(), &lv_dropdown_class);
    require(type, "wide registration type");
    click(view, 150, 90); screenshot(view, "wide-05-type-options");
    lv_dropdown_close(type); lv_dropdown_set_selected(type, 2);
    click(view, 462, 100); screenshot(view, "wide-06-register-weight");
    click(view, 100, 138); click(view, 462, 100);
    require(has_label(lv_scr_act(), "Unstable - wait"), "wide unstable registration blocked");
    click(view, 100, 138); click(view, 462, 100);
    require(has_label(lv_scr_act(), "Icon 3/3"), "wide stable registration advances");
    for (int i = 0; i < 20; ++i) click(view, (40 + (i % 10) * 62) * 84 / 100, 90 + (i / 10) * 50);
    screenshot(view, "wide-07-icons");
    click(view, 449, 26);
    require(controller.equipment_count() == 4 && controller.equipment()[3].name == "Pour-over set 🦋",
            "all wide icon positions are reachable and last selection saves");
    click(view, 350, 130); screenshot(view, "wide-08-remove-confirm");
    require(controller.equipment_count() == 4, "wide removal requires confirmation");
    click(view, 600, 80); require(controller.equipment_count() == 4, "wide cancel preserves equipment");
    click(view, 350, 130); click(view, 437, 138);
    require(controller.equipment_count() == 3, "wide confirm removes equipment");
    click(view, 610, 26);
    view.open(ui::Dialog::calibration); screenshot(view, "wide-09-calibration");
    click(view, 610, 26);
    view.open(ui::Dialog::demo); screenshot(view, "wide-10-scenarios");
    click(view, 150, 80); controller.run_demo();
    for (int i = 0; i < 190; ++i) { controller.tick(.1F); pump(view, 100); }
    require(controller.session().phase == Phase::brewing, "wide run starts brewing");
    screenshot(view, "wide-11-brewing");
    auto* brew = lv_scr_act(); view.open(ui::Dialog::dose); pump(view);
    require(lv_scr_act() == brew, "wide brew locks editors");
    click(view, 570, 146); require(controller.session().phase == Phase::result, "wide finish");
    screenshot(view, "wide-12-result");
    click(view, 560, 146); screenshot(view, "wide-13-remove-load");
    require(controller.session().phase == Phase::remove_load, "wide next requires removal");
    click(view, 540, 146); require(controller.session().phase == Phase::ready, "wide removal returns home");
    controller.load_scenario(Scenario::pour_over); controller.run_demo();
    for (int i = 0; i < 130; ++i) { controller.tick(.1F); pump(view, 100); }
    screenshot(view, "wide-14-pour-over");
    static BatteryHistory history;
    view.history(history);
    history.update(0, UsageState::estimated, decode_voltage_battery(4000));
    history.update(21600, UsageState::estimated, decode_voltage_battery(3800));
    view.battery(decode_voltage_battery(3800));
    click(view, 480, 18);
    require(has_label(lv_scr_act(), "Battery Usage"), "wide battery accessible during brew");
    require(!view.power_inhibited() || controller.session().phase == Phase::brewing,
            "battery history does not introduce an editor inhibitor");
    screenshot(view, "wide-24-battery-6hr");
    click(view, 400, 24); screenshot(view, "wide-25-battery-1day");
    click(view, 510, 24); screenshot(view, "wide-26-battery-3day");
    click(view, 610, 24);
    require(!has_label(lv_scr_act(), "Battery Usage"), "wide battery close returns to brew");
    std::cout << "Wide UI interaction checks passed\n";
}

void run_wide_live_checks(smart_scale::ui::View& view, smart_scale::Controller& controller) {
    using namespace smart_scale;
    view.battery(decode_voltage_battery(3900));
    std::filesystem::create_directories("build-host/screens");
    screenshot(view, "wide-15-disconnected");
    require(has_label(lv_scr_act(), "Tap to calibrate"), "missing calibration offers card action");
    require(!has_label(lv_scr_act(), "Calibrate") && !has_label(lv_scr_act(), "Reset") &&
            !has_label(lv_scr_act(), "Start brew") && !has_label(lv_scr_act(), "Place a registered container."),
            "wide setup removes separate actions and placement hint");
    click(view, 100, 90);
    require(has_label(lv_scr_act(), "Calibrate scale"), "weight card opens calibration without sensor data");
    click(view, 610, 26);
    require(has_label(lv_scr_act(), "Tap to calibrate"), "cancelled calibration remains accessible");
    click(view, 100, 90); screenshot(view, "wide-16-live-calibration");
    std::uint64_t now{};
    auto feed = [&](int raw) {
        for (int i = 0; i < 60; ++i) { controller.sensor_sample(raw, now += 100); controller.tick(.1F); pump(view, 100); }
    };
    feed(10000); click(view, 450, 92); feed(110000); click(view, 483, 142);
    require(controller.scale().ready() && std::abs(controller.scale().gross() - 100) < .01F,
            "wide touch calibration captures zero and 100 g mass");
    screenshot(view, "wide-17-calibrated"); click(view, 610, 26);
    require(has_label(lv_scr_act(), "Tap to tare") && !has_label(lv_scr_act(), "Tap to calibrate"),
            "successful calibration changes card to tare");
    controller.sensor_sample(std::nullopt, now += 1100); controller.tick(1.1F); pump(view);
    click(view, 100, 90);
    require(!has_label(lv_scr_act(), "Calibrate scale") && has_label(lv_scr_act(), "Tap to tare"),
            "sensor fault with calibration cannot open calibration or tare");
    feed(110000);
    require(controller.register_equipment({0, Kind::pour_over_set, "Pour-over set", "", 500.0F, {}}, true), "wide live profile");
    click(view, 70, 24);
    feed(10000); screenshot(view, "wide-18-load-container");
    feed(510000); screenshot(view, "wide-19-load-coffee");
    feed(514900); click(view, 520, 70);
    require(has_label(lv_scr_act(), "+0.1"), "wide below 5 g opens editor");
    require(!controller.session().dose_g, "wide opening editor does not invent coffee");
    click(view, 610, 26); feed(528000);
    require(has_label(lv_scr_act(), "Tap to save"), "wide measured coffee candidate");
    screenshot(view, "wide-20-save-coffee"); click(view, 520, 70);
    require(controller.session().dose_source == DoseSource::measured, "wide card saves measured coffee");
    feed(528000); screenshot(view, "wide-21-ready-live");
    click(view, 300, 146); // Former Start brew position.
    // The former footer is now occupied by full-height cards.
    require(controller.session().phase == Phase::ready && controller.session().dose_g,
            "removed controls neither start nor reset a prepared brew");
    click(view, 520, 70); click(view, 386, 132); click(view, 610, 26);
    require(std::abs(*controller.session().dose_g - 18.1F) < .02F, "wide saved coffee manual edit");
    feed(528000);
    const auto epoch = controller.scale().display_epoch();
    click(view, 100, 90);
    require(controller.scale().display_epoch() != epoch && std::abs(controller.session().net_g) < .01F,
            "wide weight card tares ready stable scale");
    for (unsigned i = 1; i <= 25 && controller.session().phase == Phase::ready; ++i) {
        controller.sensor_sample(528000 + i * 200, now += 100); controller.tick(.1F); pump(view, 100);
    }
    require(controller.session().phase == Phase::brewing && controller.session().net_g > 0,
            "wide coffee registration permits automatic flow start");
    screenshot(view, "wide-22-auto-brew");
    controller.sensor_sample(std::nullopt, now += 1100); controller.tick(1.1F); pump(view);
    require(controller.session().brew_error == BrewError::sensor_fault, "wide sensor fault recorded");
    const auto remaining = static_cast<unsigned>(std::ceil(controller.error_remaining_s()));
    char alert[96];
    std::snprintf(alert, sizeof(alert), "Sensor unavailable - home in %02u:%02u", remaining / 60, remaining % 60);
    require(has_label(lv_scr_act(), alert), "wide sensor error countdown");
    screenshot(view, "wide-23-sensor-error");
    controller.tick(180.1F); pump(view); pump(view);
    require(controller.session().phase == Phase::ready, "wide error recovery returns home");
    std::cout << "Wide live calibration and coffee checks passed\n";
}

}
int main(int argc, char** argv) {
    const bool interactive = argc > 1 && std::string(argv[1]) == "--interactive";
    const bool rail_test = argc > 1 && (std::string(argv[1]) == "--rail-wide" || std::string(argv[1]) == "--rail-compact");
    const bool rail_wide = argc > 1 && std::string(argv[1]) == "--rail-wide";
    const bool theme_test = argc > 1 && std::string(argv[1]) == "--theme-screenshots";
    const bool smoke = argc > 1 && std::string(argv[1]) == "--interactive-smoke";
    const bool wide_test = argc > 1 && std::string(argv[1]) == "--wide-screenshots";
    const bool wide_live = argc > 1 && std::string(argv[1]) == "--wide-live-screenshots";
    const bool wide_interactive = argc > 1 && std::string(argv[1]) == "--wide-interactive";
    // The default simulator follows the device configuration. Keep the legacy
    // screenshot fixtures at 480x320 for regression coverage.
    if (interactive || smoke || wide_test || wide_interactive || wide_live || theme_test || rail_wide) {
        width = smart_scale::config::screen_width;
        height = smart_scale::config::screen_height;
    }
    require(width * height <= static_cast<int>(framebuffer.size()) &&
            width <= static_cast<int>(buffer.size()), "display fits host buffers");
    lv_init();
    static lv_disp_draw_buf_t draw_buffer;
    lv_disp_draw_buf_init(&draw_buffer, buffer.data(), nullptr, buffer.size());
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = width; display.ver_res = height;
    display.draw_buf = &draw_buffer; display.flush_cb = flush;
    lv_disp_drv_register(&display);
    static lv_indev_drv_t pointer;
    lv_indev_drv_init(&pointer); pointer.type = LV_INDEV_TYPE_POINTER; pointer.read_cb = input;
    lv_indev_drv_register(&pointer);
    const bool coffee_test = argc > 1 && std::string(argv[1]) == "--coffee-screenshots";
    const bool live_test = argc > 1 && std::string(argv[1]) == "--live-screenshots";
    smart_scale::Controller controller(live_test || coffee_test || wide_live);
    if (coffee_test) controller.set_mode(smart_scale::Mode::pour_over);
    smart_scale::ui::View view(controller);
    view.init();
    if (rail_test) {
        using namespace smart_scale;
        std::filesystem::create_directories("build-host/screens");
        const auto count = controller.equipment_count();
        const std::string prefix = rail_wide ? "rail-wide-" : "rail-compact-";
        for (int step = 0; step < 3; ++step) {
            view.open(ui::Dialog::registration); pump(view);
            for (int i = 0; i < step; ++i) click(view, rail_wide ? 470 : 320, rail_wide ? 100 : 284);
            screenshot(view, (prefix + "registration-" + std::to_string(step)).c_str());
            require(has_label(lv_scr_act(), "Cancel"), "registration exposes cancellation");
            click(view, width - 2, step == 0 ? 2 : step == 1 ? height / 2 : height - 2);
            require(!has_label(lv_scr_act(), "Cancel"), "rail corners and center close registration");
            require(controller.equipment_count() == count, "cancel never persists equipment");
        }
        view.open(ui::Dialog::dose); pump(view);
        screenshot(view, (prefix + "dose").c_str());
        const auto dose = controller.session().dose_g;
        click(view, width - 2, height - 2);
        require(controller.session().dose_g == dose && !has_label(lv_scr_act(), "Close"), "dose close preserves value");
        controller.load_scenario(Scenario::pour_over);
        controller.run_demo();
        for (int i = 0; i < 180; ++i) { controller.tick(.1F); pump(view, 100); }
        require(controller.session().phase == Phase::brewing, "brew starts");
        screenshot(view, (prefix + "brewing").c_str());
        click(view, width - 2, 2);
        require(controller.session().phase == Phase::result, "rail corner finishes brew");
        return 0;
    }
    if (theme_test) {
        using namespace smart_scale;
        std::filesystem::create_directories("build-host/screens");
        require(view.theme() == Theme::sage_green, "default Sage Green");
        require(!view.set_theme(static_cast<Theme>(255)), "invalid theme rejected");
        const char* names[] = {"sage-green", "espresso-amber", "porcelain-gray"};
        for (unsigned i = 0; i < 3; ++i) {
            controller.load_scenario(Scenario::pour_over);
            view.open(ui::Dialog::none);
            require(view.set_theme(static_cast<Theme>(i)), "theme changes");
            pump(view);
            screenshot(view, (std::string(names[i]) + "-ready").c_str());
            view.open(ui::Dialog::dose); pump(view);
            screenshot(view, (std::string(names[i]) + "-dose").c_str());
            view.open(ui::Dialog::none); pump(view);
            controller.run_demo(); for(int sample=0;sample<180;++sample){controller.tick(.1F);pump(view,100);}
            const auto before = controller.session();
            require(view.set_theme(static_cast<Theme>((i+1)%3)), "theme can change during brew");
            pump(view);
            require(controller.session().net_g == before.net_g && controller.session().seconds == before.seconds &&
                    controller.session().phase == before.phase, "theme preserves running session");
            require(view.set_theme(static_cast<Theme>(i)), "restore theme"); pump(view);
            require(!controller.tare(), "controller rejects tare during brew");
            const float net = controller.session().net_g;
            click(view, 355, 90);
            require(controller.session().net_g == net, "brew weight tap does not tare");
            require(!has_label(lv_scr_act(), "Tare"), "no brew tare control");
            screenshot(view, (std::string(names[i]) + "-brewing").c_str());
            click(view, 555, 140);
            require(controller.session().phase == Phase::result, "large Finish button");
            screenshot(view, (std::string(names[i]) + "-result").c_str());
            controller.reset(); controller.remove_object(); view.open(ui::Dialog::none); pump(view);
            require(has_label(lv_scr_act(), "Select") && has_label(lv_scr_act(), "Set dose"), "empty cards have clear actions");
            require(!has_label(lv_scr_act(), "Run demo") && !has_label(lv_scr_act(), "Scenarios"), "simulator controls stay off device");
            screenshot(view, (std::string(names[i]) + "-empty").c_str());
        }
        return 0;
    }
    if (wide_live) {
        run_wide_live_checks(view, controller);
        return 0;
    }
    if (wide_test) {
        run_wide_checks(view, controller);
        return 0;
    }
    if (coffee_test) {
        using namespace smart_scale;
        std::filesystem::create_directories("build-host/screens");
        require(controller.scale().restore({0, 1000}), "calibration");
        require(controller.register_equipment({0, Kind::pour_over_set, "Pour-over set", "", 500.0F, {}}, true), "profile");
        std::uint64_t now = 0;
        auto feed = [&](int raw) {
            for (int i = 0; i < 60; ++i) { controller.sensor_sample(raw, now += 100); controller.tick(.1F); pump(view, 100); }
        };
        feed(0); screenshot(view, "29-load-container");
        feed(500000); screenshot(view, "30-load-coffee");
        require(has_label(lv_scr_act(), "Tap to set"), "empty coffee offers manual entry");
        feed(504900); click(view, 360, 200);
        require(has_label(lv_scr_act(), "+0.1"), "below 5 g opens manual editor");
        require(!controller.session().dose_g, "opening manual editor does not save a default");
        click(view, 440, 32);
        feed(518000); screenshot(view, "31-save-coffee");
        require(has_label(lv_scr_act(), "Tap to save"), "candidate offers tap-to-save");
        require(!controller.session().dose_g, "candidate remains unsaved");
        const auto before_pulse = flush_count;
        for (unsigned i = 0; i < 15; ++i) pump(view, 100);
        require(flush_count > before_pulse, "candidate card pulses");
        click(view, 360, 200);
        require(controller.session().dose_source == DoseSource::measured, "click saves measured coffee");
        require(!has_label(lv_scr_act(), "+0.1"), "save does not open manual editor");
        feed(518000); screenshot(view, "32-start-brew");
        require(has_label(lv_scr_act(), LV_SYMBOL_PLAY " Pour to start"), "both registered shows ready guidance");
        const auto after_save = flush_count;
        for (unsigned i = 0; i < 15; ++i) pump(view, 100);
        require(flush_count == after_save, "saved card stops pulsing");
        click(view, 360, 200); screenshot(view, "33-edit-coffee");
        require(has_label(lv_scr_act(), "+0.1"), "saved coffee opens manual editor");
        click(view, 240, 216); click(view, 440, 32);
        require(std::abs(*controller.session().dose_g - 18.1F) < .02F, "manual edit retained");
        feed(518000);
        for (unsigned i = 1; i <= 25 && controller.session().phase == Phase::ready; ++i) {
            controller.sensor_sample(518000 + i * 200, now += 100); controller.tick(.1F); pump(view, 100);
        }
        require(controller.session().phase == Phase::brewing, "flow automatically starts brew");
        require(controller.session().net_g > 1 && controller.session().net_g < 5, "brew excludes coffee");
        screenshot(view, "34-coffee-auto-brew");
        std::cout << "Coffee registration UI checks passed\n";
        return 0;
    }
    if (live_test) {
        std::filesystem::create_directories("build-host/screens");
        screenshot(view, "16-hx711-disconnected");
        view.battery(smart_scale::decode_axp2101(0x28, 0x20, 100));
        screenshot(view, "35-battery-charging");
        require(has_label(lv_scr_act(), LV_SYMBOL_BATTERY_FULL " 100%"), "charging uses color without a label");
        view.battery(smart_scale::decode_axp2101(0x28, 4, 100));
        screenshot(view, "36-battery-full");
        require(has_label(lv_scr_act(), LV_SYMBOL_BATTERY_FULL " 100%"), "full battery retains percentage only");
        smart_scale::BatteryHistory history;
        auto previous_usage = smart_scale::UsageState::unknown;
        for (uint64_t t = 0; t <= 72 * 3600; t += 300) {
            const double hour = (t % (12 * 3600)) / 3600.0;
            const auto state = hour < 2 ? smart_scale::UsageState::charging : hour < 6 ? smart_scale::UsageState::active : smart_scale::UsageState::sleep;
            const unsigned percent = hour < 2 ? 30 + hour * 30 : hour < 6 ? 90 - (hour-2)*12 : 42 - (hour-6)*2;
            if (state == smart_scale::UsageState::sleep && previous_usage == state) continue;
            previous_usage = state;
            history.update(t, state, smart_scale::decode_axp2101(state == smart_scale::UsageState::charging ? 0x28 : 8,
                state == smart_scale::UsageState::charging ? 0x20 : 0, percent));
        }
        view.battery(smart_scale::decode_axp2101(8, 0, 72));
        view.history(history);
        click(view, 440, 30);
        require(has_label(lv_scr_act(), "Battery Usage"), "top-right battery opens usage graph");
        require(!view.power_inhibited(), "history panel permits idle power policy");
        require(view.history_hours()==6 && has_label(lv_scr_act(), "-6h"), "default six-hour range");
        require(!has_label(lv_scr_act(), "Set UTC date / time"), "no clock controls");
        screenshot(view, "37-battery-6hr");
        click(view, 250, 80);
        require(view.history_hours()==24 && has_label(lv_scr_act(), "-24h"), "one-day range");
        screenshot(view, "38-battery-1day");
        unsigned green_pixels=0, blue_pixels=0, orange_pixels=0;
        for (int y=120; y<252; ++y) for (int x=72; x<447; ++x) {
            const auto pixel=framebuffer[y*width+x];
            const int r=(pixel>>16)&255, g=(pixel>>8)&255, b=pixel&255;
            green_pixels += g>r+40 && g>b+20;
            blue_pixels += b>r+40 && b>g+20;
            orange_pixels += r>g+30 && g>b+40;
        }
        require(green_pixels>30 && blue_pixels>30 && orange_pixels>30, "graph renders all three state colors");
        click(view, 380, 80);
        require(view.history_hours()==72 && has_label(lv_scr_act(), "-72h"), "three-day range");
        screenshot(view, "39-battery-3day");
        history.clear(); pump(view);
        screenshot(view, "40-battery-empty");
        click(view, 432, 30);
        view.battery({});
        pump(view);
        require(has_label(lv_scr_act(), "Tap to calibrate"), "legacy calibration prompt");
        require(!has_label(lv_scr_act(), "Start brew") && !has_label(lv_scr_act(), "Reset") &&
                !has_label(lv_scr_act(), "Calibrate"), "legacy setup has no separate actions");
        click(view, 100, 145);
        screenshot(view, "17-hx711-calibrate");
        std::uint64_t now = 0;
        auto feed = [&](int raw) {
            for (int i = 0; i < 60; ++i) { controller.sensor_sample(raw, now += 100); controller.tick(0.1F); pump(view); }
        };
        const int masses[] = {20, 50, 100};
        for (int i = 0; i < 3; ++i) {
            feed(-78000);
            if (i == 0) require(has_label(lv_scr_act(), "-78000 counts"), "uncalibrated dialog shows filtered counts");
            click(view, 204, 154); // Capture empty zero in centered dialog.
            feed(-78000 + masses[i] * 866);
            click(view, 110 + i * 94, 206);
            require(controller.scale().ready(), "touch calibration succeeds");
            require(std::abs(controller.scale().gross() - masses[i]) < 0.01F, "20/50/100 g buttons use exact mass");
        }
        screenshot(view, "18-hx711-calibrated");
        click(view, 440, 32); // Close dialog.
        feed(8600); // 100 g vessel.
        click(view, 100, 145); // Tap weight to tare.
        require(std::abs(controller.scale().net()) < 0.01F, "touch tare");
        screenshot(view, "19-hx711-live");
        // A tare changes the idle net reading, never the gross calibration or
        // registration readout. Both dialogs use the same timed presentation.
        // Exercise calibration rendering directly; it is no longer a setup button.
        view.open(smart_scale::ui::Dialog::calibration); pump(view);
        require(has_label(lv_scr_act(), "100.0 g"), "calibration displays gross despite tare");
        screenshot(view, "22-hx711-calibration-gross");
        controller.sensor_sample(std::nullopt, now += 1100); controller.tick(1.1F); pump(view);
        require(has_label(lv_scr_act(), "? g"), "calibration hides stale values after timeout");
        feed(8600);
        require(has_label(lv_scr_act(), "100.0 g"), "calibration recovers with a fresh display reference");
        click(view, 440, 32);
        view.open(smart_scale::ui::Dialog::registration); pump(view); pump(view);
        click(view, 370, 284);
        require(has_label(lv_scr_act(), "100.0 g"), "registration uses stabilized gross");
        // Synthetic +/-0.2 g sensor noise, with real timestamps, while this
        // dialog is open. Quantized sensor values never feed saved equipment.
        for (unsigned i = 0; i < 150; ++i) {
            controller.sensor_sample(8600 + static_cast<int>(173 * std::sin(i * 0.1 * 6.283185307 / 1.4)), now += 100);
            controller.tick(0.1F); pump(view);
        }
        require(controller.scale().quiet(), "registration keeps common quiet filtering active");
        feed(8707);
        const float reference = controller.scale().gross();
        require(std::abs(reference - 100.0F) > 0.1F, "registration source retains sub-display precision");
        for (unsigned i = 0; i < 12; ++i) {
            controller.sensor_sample(8707 + (i % 2 ? 121 : -121), now += 100);
            controller.tick(0.1F); pump(view);
        }
        require(controller.scale().stable(), "minor registration noise keeps Stable latched");
        require(has_label(lv_scr_act(), "Stable - ready to continue"), "registration status stays actionable during brief noise");
        screenshot(view, "23-hx711-registration-stabilized");
        click(view, 370, 284); // Capture the recent qualified mean, not the label.
        require(has_label(lv_scr_act(), "Icon 3/3"), "Continue consumes snapshot despite minor noise");
        click(view, 370, 284); // Save default icon.
        require(controller.equipment_count() == 1 &&
                std::abs(*controller.equipment()[0].empty_g - reference) < 0.03F &&
                std::abs(*controller.equipment()[0].empty_g - 100.1F) > 0.005F,
                "registered mass is an unrounded qualified mean");
        click(view, 440, 32); // Close equipment list.
        feed(8254); // Actual placement ~99.6 g, registered mass ~100.12 g.
        require(controller.set_dose(18), "coffee required before manual brew");
        click(view, 60, 286); // Former Start brew position must do nothing.
        require(controller.session().phase == smart_scale::Phase::ready, "no manual start control");
        // Model-only fixture for the signed-weight regression below. Live UI starts via flow only.
        require(controller.start_brew(), "signed-weight brew fixture"); pump(view);
        feed(8167); // -0.1 g net is noise, not cup removal.
        require(!controller.session().uncertain && controller.session().net_g < -.09F,
                "negative tenth-gram reading stays in live brew despite registration offset");
        require(!has_label(lv_scr_act(), "Object removed - home in 03:00"),
                "negative net does not show removal error");
        screenshot(view, "26-hx711-negative-net");
        feed(25920);
        screenshot(view, "20-hx711-brewing");
        click(view, 360, 30);
        require(has_label(lv_scr_act(), "Battery Usage"), "history accessible during brew");
        require(controller.session().phase == smart_scale::Phase::brewing && view.power_inhibited(), "history preserves brewing and its sleep inhibitor");
        click(view, 432, 30);
        feed(17260); // 110 g gross: liquid decreased, registered cup remains.
        require(!controller.session().uncertain, "decreasing net weight does not imply removal");
        for (unsigned i = 0; i < 6; ++i) {
            controller.sensor_sample(-60, now += 100); // 90 g < registered 100 g cup.
            controller.tick(.1F); pump(view);
        }
        require(controller.session().brew_error == smart_scale::BrewError::object_removed,
                "gross below registered empty mass detects removal");
        require(has_label(lv_scr_act(), "Object removed - home in 03:00"), "removal error includes return countdown");
        screenshot(view, "25-hx711-object-removed");
        controller.sensor_sample(std::nullopt, now + 1100); controller.tick(1.1F);
        screenshot(view, "21-hx711-timeout");
        require(controller.session().uncertain, "timeout invalidates live brew");
        controller.tick(static_cast<float>(controller.error_remaining_s() - 1)); pump(view);
        require(controller.session().phase == smart_scale::Phase::brewing, "error remains visible before three minutes");
        controller.tick(1.1F); pump(view); pump(view);
        require(controller.session().phase == smart_scale::Phase::ready, "error returns directly to main after three minutes");
        require(has_label(lv_scr_act(), "Espresso"), "main screen is rendered after error timeout");
        screenshot(view, "24-hx711-error-home");
        feed(-78000); feed(8600); // Rearm cup recognition after removal.
        require(has_label(lv_scr_act(), "0.0"), "recognized vessel displays zero after automatic tare");
        screenshot(view, "28-hx711-vessel-zero");
        view.open(smart_scale::ui::Dialog::calibration); pump(view);
        for (unsigned i = 1; i <= 18; ++i) {
            controller.sensor_sample(8600 + 173 * i, now += 100);
            controller.tick(.1F); pump(view);
        }
        require(controller.session().phase == smart_scale::Phase::ready,
                "calibration dialog blocks automatic brew");
        click(view, 440, 32); feed(8600);
        require(!controller.session().brew_prepared, "vessel without coffee dose is not brew prepared");
        require(controller.set_dose(18) && controller.session().brew_prepared,
                "registered vessel and coffee dose enter internal brew preparation");
        for (unsigned i = 1; i <= 20; ++i) {
            controller.sensor_sample(8600 + 173 * i, now += 100);
            controller.tick(.1F); pump(view);
            if (controller.session().phase == smart_scale::Phase::brewing) break;
        }
        pump(view); pump(view);
        require(controller.session().phase == smart_scale::Phase::brewing &&
                controller.session().net_g > 1 && has_label(lv_scr_act(), "Running"),
                "sustained flow opens Running automatically and retains liquid");
        screenshot(view, "27-hx711-auto-brew");
        std::cout << "Live HX711 UI checks passed\n";
        return 0;
    }
    if (interactive || wide_interactive || smoke) {
#ifdef SMART_SCALE_SDL
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { std::cerr << SDL_GetError(); return 1; }
        auto* window = SDL_CreateWindow("Smart Scale | Space: Run demo | S: Scenarios | R: Reset", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, width * 2, height * 2, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        auto* renderer = SDL_CreateRenderer(window, -1, 0);
        if (!window || !renderer) { std::cerr << SDL_GetError(); return 1; }
        SDL_RenderSetLogicalSize(renderer, width, height);
        auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!texture) { std::cerr << SDL_GetError(); return 1; }
        bool running = true;
        unsigned frames = 0;
        auto previous = SDL_GetTicks();
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                if (event.type == SDL_MOUSEMOTION) { mouse_x = event.motion.x; mouse_y = event.motion.y; }
                if (event.type == SDL_MOUSEBUTTONDOWN) mouse_down = true;
                if (event.type == SDL_MOUSEBUTTONUP) mouse_down = false;
                if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_r) view.reset();
                    if (event.key.keysym.sym == SDLK_SPACE) controller.run_demo();
                    if (event.key.keysym.sym == SDLK_s) view.open(smart_scale::ui::Dialog::demo);
                }
            }
            const auto now = SDL_GetTicks(); const auto elapsed = now - previous; previous = now;
            controller.tick(static_cast<float>(elapsed) / 1000); pump(view, elapsed);
            SDL_UpdateTexture(texture, nullptr, framebuffer.data(), width * sizeof(std::uint32_t));
            SDL_RenderClear(renderer); SDL_RenderCopy(renderer, texture, nullptr, nullptr); SDL_RenderPresent(renderer);
            SDL_Delay(5);
            if (smoke && ++frames == 20) running = false;
        }
        SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
        if (smoke) std::cout << "SDL display smoke check passed\n";
#else
        std::cerr << "SDL2 development package required. macOS: brew install sdl2; Ubuntu: sudo apt install libsdl2-dev\n";
        return 2;
#endif
        return 0;
    }
    using namespace smart_scale;
    std::filesystem::create_directories("build-host/screens");
    screenshot(view, "01-ready");
    click(view, 360, 100); screenshot(view, "02-objects");
    click(view, 440, 30); // Close object modal.
    click(view, 360, 200); screenshot(view, "03-dose");
    require(controller.session().dose_g && *controller.session().dose_g == 18.2F, "Opening preserves dose");
    auto* dose_screen = lv_scr_act();
    click(view, 204, 156); // 20 g preset.
    require(*controller.session().dose_g == 20.0F, "Preset selects exact dose");
    click(view, 240, 216); // +0.1 g.
    require(*controller.session().dose_g == 20.1F, "Fine adjustment adds one tenth");
    click(view, 308, 216); // +1 g.
    require(*controller.session().dose_g == 21.1F, "Coarse adjustment adds one gram");
    click(view, 96, 216); // -1 g.
    click(view, 164, 216); // -0.1 g.
    require(*controller.session().dose_g == 20.0F, "Decrement buttons preserve precision");
    require(lv_scr_act() == dose_screen, "Adjustments do not rebuild the screen");
    mouse_x = 240; mouse_y = 240; mouse_down = true;
    for (int i = 0; i < 25; ++i) pump(view, 40);
    const float repeated = *controller.session().dose_g;
    require(repeated > 20.1F, "Holding fine adjustment repeats");
    mouse_down = false; pump(view);
    require(*controller.session().dose_g == repeated, "Release adds no extra click");
    for (int i = 0; i < 20; ++i) pump(view, 40);
    require(*controller.session().dose_g == repeated, "Repeat stops after release");
    mouse_down = true;
    for (int i = 0; i < 20; ++i) pump(view, 40);
    mouse_x = 40; pump(view); // Slide outside the button.
    const float lost = *controller.session().dose_g;
    for (int i = 0; i < 20; ++i) pump(view, 40);
    mouse_down = false; pump(view);
    require(*controller.session().dose_g == lost, "Repeat stops when pointer leaves");
    controller.set_dose(60.0F); pump(view);
    click(view, 308, 216); click(view, 240, 216);
    require(*controller.session().dose_g == 60.0F, "Upper bound clamps");
    controller.set_dose(0.1F); pump(view);
    click(view, 96, 216); click(view, 164, 216);
    require(*controller.session().dose_g == 0.1F, "Lower bound clamps");
    click(view, 204, 156); pump(view, 500); pump(view, 500);
    const auto idle_flushes = flush_count;
    for (int i = 0; i < 60; ++i) pump(view, 100);
    require(flush_count == idle_flushes, "Unchanged dose dialog performs no display transfers");
    click(view, 440, 30);
    require(*controller.session().dose_g == 20.0F, "Closing preserves selected dose");
    click(view, 360, 200);
    require(*controller.session().dose_g == 20.0F, "Reopening preserves dose");
    click(view, 440, 30);
    view.open(ui::Dialog::dose); pump(view);
    mouse_x = 240; mouse_y = 240; mouse_down = true;
    for (int i = 0; i < 20; ++i) pump(view, 40);
    controller.liquid(1, 1); pump(view);
    const float locked_dose = *controller.session().dose_g;
    for (int i = 0; i < 20; ++i) pump(view, 40);
    mouse_down = false; pump(view);
    require(*controller.session().dose_g == locked_dose, "Brew start cancels held adjustment");
    controller.finish();
    controller.load_scenario(Scenario::known); pump(view);
    click(view, 90, 290); // Start dummy liquid stream via UI.
    for (int i = 0; i < 190; ++i) { controller.tick(0.1F); pump(view, 100); }
    require(controller.session().phase == Phase::brewing, "Run demo starts extraction");
    screenshot(view, "04-extracting");
    auto* brew_screen = lv_scr_act();
    view.open(ui::Dialog::dose); pump(view);
    require(lv_scr_act() == brew_screen, "Locked panel cannot open editor");
    click(view, 440, 290);
    require(controller.session().phase == Phase::result, "Finish opens results");
    screenshot(view, "05-result");
    click(view, 396, 290);
    require(controller.session().phase == Phase::remove_load, "Next requires load removal");
    screenshot(view, "06-remove-load");
    click(view, 240, 286);
    require(controller.session().phase == Phase::ready, "Removal returns to ready");
    controller.load_scenario(Scenario::unknown);
    view.open(ui::Dialog::dose); pump(view); // Brew must dismiss unconfirmed edits.
    controller.run_demo();
    for (int i = 0; i < 30; ++i) { controller.tick(0.1F); pump(view, 100); }
    require(controller.session().phase == Phase::brewing && !controller.session().dose_g, "Opening editor does not invent a dose");
    click(view, 240, 216);
    require(!controller.session().dose_g, "Brewing dismisses dose controls");
    screenshot(view, "07-unknown");
    controller.finish(); controller.load_scenario(Scenario::pour_over); pump(view);
    controller.run_demo();
    for (int i = 0; i < 130; ++i) { controller.tick(0.1F); pump(view, 100); }
    screenshot(view, "08-pour-over");
    controller.finish(); controller.load_scenario(Scenario::known); pump(view);
    view.open(ui::Dialog::registration); screenshot(view, "09-register-type");
    auto* type = find_type(lv_scr_act(), &lv_dropdown_class);
    require(type && std::string(lv_dropdown_get_options(type)) == "Container\nPortafilter\nPour-over set", "Container replaces Cup");
    lv_dropdown_set_selected(type, 2);
    click(view, 370, 284); screenshot(view, "10-register-weight");
    click(view, 150, 222); // Mark unstable.
    click(view, 370, 284);
    require(find_type(lv_scr_act(), &lv_dropdown_class) == nullptr, "Weight step stays active");
    require(controller.equipment_count() == 3, "Unstable reading cannot save");
    click(view, 150, 222); // Stable again.
    click(view, 370, 284); screenshot(view, "11-register-icon");
    require(!find_type(lv_scr_act(), &lv_keyboard_class) && !find_type(lv_scr_act(), &lv_textarea_class), "Icon picker has no keyboard or text input");
    auto* icon_screen = lv_scr_act();
    for (int i = 0; i < 20; ++i) {
        click(view, 8 + (54 + (i % 5) * 84) * 392 / 452, 88 + (i / 5) * 46);
        // Both picker and name fonts must cover every bundled identifier.
        uint32_t offset = 0;
        const auto codepoint = _lv_txt_encoded_next(ui::equipment_icons[i], &offset);
        lv_font_glyph_dsc_t glyph{};
        require(lv_font_get_glyph_dsc(&equipment_icons_22, &glyph, codepoint, 0), "Name font contains icon");
        require(lv_font_get_glyph_dsc(&equipment_icons_32, &glyph, codepoint, 0), "Picker font contains icon");
    }
    require(lv_scr_act() == icon_screen, "All 20 icon choices update in place");
    click(view, 65, 282); // Back to weight.
    click(view, 370, 284); // Return to picker; preserve icon.
    screenshot(view, "12-register-icon-selected");
    click(view, 370, 282);
    require(controller.equipment_count() == 4, "Registration saves profile");
    require(controller.equipment()[3].name == "Pour-over set 🦋", "Selected type and icon survive Back and Save");
    require(controller.equipment()[3].kind == Kind::pour_over_set, "Selected equipment type survives registration");
    screenshot(view, "13-registered");
    click(view, 308, 84); // First row trash button.
    screenshot(view, "14-remove-equipment");
    require(controller.equipment_count() == 4, "Remove requires confirmation");
    click(view, 440, 270); // Cancel.
    require(controller.equipment_count() == 4, "Cancel preserves equipment");
    click(view, 308, 84); click(view, 274, 270);
    require(controller.equipment_count() == 3, "Confirm removes the intended equipment");
    require(controller.equipment()[0].kind == Kind::portafilter, "List compacts after removal");
    for (int i = 0; i < 3; ++i) { click(view, 308, 84); click(view, 274, 270); }
    require(controller.equipment_count() == 0, "All equipment can be removed");
    screenshot(view, "15-empty-equipment");
    click(view, 240, 276); pump(view);
    require(find_type(lv_scr_act(), &lv_dropdown_class), "Empty list still allows registration");
    std::cout << "UI interaction checks passed\n";
}
