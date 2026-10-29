#include "ui.hxx"
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
constexpr int width = 480, height = 320;
std::array<std::uint32_t, width * height> framebuffer{};
std::array<lv_color_t, width * 40> buffer{};
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
    for (auto pixel : framebuffer) {
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
lv_obj_t* icon_button(lv_obj_t* parent, const char* text) {
    if (lv_obj_check_type(parent, &lv_btn_class) && has_label(parent, text)) return parent;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i)
        if (auto* result = icon_button(lv_obj_get_child(parent, static_cast<int>(i)), text)) return result;
    return nullptr;
}

}
int main(int argc, char** argv) {
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
    const bool live_test = argc > 1 && std::string(argv[1]) == "--live-screenshots";
    smart_scale::Controller controller(live_test);
    smart_scale::ui::View view(controller);
    view.init();
    if (live_test) {
        std::filesystem::create_directories("build-host/screens");
        screenshot(view, "16-hx711-disconnected");
        click(view, 268, 286);
        screenshot(view, "17-hx711-calibrate");
        std::uint64_t now = 0;
        auto feed = [&](int raw) {
            for (int i = 0; i < 60; ++i) { controller.sensor_sample(raw, now += 100); controller.tick(0.1F); pump(view); }
        };
        const int masses[] = {20, 50, 100};
        for (int i = 0; i < 3; ++i) {
            feed(-78000);
            if (i == 0) require(has_label(lv_scr_act(), "-78000 counts"), "uncalibrated dialog shows filtered counts");
            click(view, 240, 154); // Capture empty zero in centered dialog.
            feed(-78000 + masses[i] * 866);
            click(view, 146 + i * 94, 206);
            require(controller.scale().ready(), "touch calibration succeeds");
            require(std::abs(controller.scale().gross() - masses[i]) < 0.01F, "20/50/100 g buttons use exact mass");
        }
        screenshot(view, "18-hx711-calibrated");
        click(view, 366, 32); // Close dialog.
        feed(8600); // 100 g vessel.
        click(view, 172, 286); // Tare.
        require(std::abs(controller.scale().net()) < 0.01F, "touch tare");
        screenshot(view, "19-hx711-live");
        // A tare changes the idle net reading, never the gross calibration or
        // registration readout. Both dialogs use the same timed presentation.
        click(view, 268, 286);
        require(has_label(lv_scr_act(), "100.0 g"), "calibration displays gross despite tare");
        screenshot(view, "22-hx711-calibration-gross");
        controller.sensor_sample(std::nullopt, now += 1100); controller.tick(1.1F); pump(view);
        require(has_label(lv_scr_act(), "? g"), "calibration hides stale values after timeout");
        feed(8600);
        require(has_label(lv_scr_act(), "100.0 g"), "calibration recovers with a fresh display reference");
        click(view, 366, 32);
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
        click(view, 366, 32); // Close equipment list.
        feed(8254); // Actual placement ~99.6 g, registered mass ~100.12 g.
        click(view, 60, 286); // Start brew.
        require(controller.session().phase == smart_scale::Phase::brewing, "touch starts real brew");
        feed(8167); // -0.1 g net is noise, not cup removal.
        require(!controller.session().uncertain && controller.session().net_g < -.09F,
                "negative tenth-gram reading stays in live brew despite registration offset");
        require(!has_label(lv_scr_act(), "Object removed - home in 03:00"),
                "negative net does not show removal error");
        screenshot(view, "26-hx711-negative-net");
        feed(25920);
        screenshot(view, "20-hx711-brewing");
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
        click(view, 366, 32); feed(8600);
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
    const bool smoke = argc > 1 && std::string(argv[1]) == "--interactive-smoke";
    if (argc > 1 && (std::string(argv[1]) == "--interactive" || smoke)) {
#ifdef SMART_SCALE_SDL
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { std::cerr << SDL_GetError(); return 1; }
        auto* window = SDL_CreateWindow("Smart Scale | dummy data | R = Reset", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, 960, 640, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
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
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_r) view.reset();
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
    click(view, 364, 30); // Close object modal.
    click(view, 360, 200); screenshot(view, "03-dose");
    require(controller.session().dose_g && *controller.session().dose_g == 18.2F, "Opening preserves dose");
    auto* dose_screen = lv_scr_act();
    click(view, 240, 156); // 20 g preset.
    require(*controller.session().dose_g == 20.0F, "Preset selects exact dose");
    click(view, 276, 216); // +0.1 g.
    require(*controller.session().dose_g == 20.1F, "Fine adjustment adds one tenth");
    click(view, 344, 216); // +1 g.
    require(*controller.session().dose_g == 21.1F, "Coarse adjustment adds one gram");
    click(view, 132, 216); // -1 g.
    click(view, 200, 216); // -0.1 g.
    require(*controller.session().dose_g == 20.0F, "Decrement buttons preserve precision");
    require(lv_scr_act() == dose_screen, "Adjustments do not rebuild the screen");
    mouse_x = 276; mouse_y = 240; mouse_down = true;
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
    click(view, 344, 216); click(view, 276, 216);
    require(*controller.session().dose_g == 60.0F, "Upper bound clamps");
    controller.set_dose(0.1F); pump(view);
    click(view, 132, 216); click(view, 200, 216);
    require(*controller.session().dose_g == 0.1F, "Lower bound clamps");
    click(view, 240, 156); pump(view, 500); pump(view, 500);
    const auto idle_flushes = flush_count;
    for (int i = 0; i < 60; ++i) pump(view, 100);
    require(flush_count == idle_flushes, "Unchanged dose dialog performs no display transfers");
    click(view, 364, 30);
    require(*controller.session().dose_g == 20.0F, "Closing preserves selected dose");
    click(view, 360, 200);
    require(*controller.session().dose_g == 20.0F, "Reopening preserves dose");
    click(view, 364, 30);
    view.open(ui::Dialog::dose); pump(view);
    mouse_x = 276; mouse_y = 240; mouse_down = true;
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
    click(view, 396, 290);
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
    click(view, 276, 216);
    require(!controller.session().dose_g, "Brewing dismisses dose controls");
    screenshot(view, "07-unknown");
    controller.finish(); controller.load_scenario(Scenario::pour_over); pump(view);
    controller.run_demo();
    for (int i = 0; i < 130; ++i) { controller.tick(0.1F); pump(view, 100); }
    screenshot(view, "08-pour-over");
    controller.finish(); controller.load_scenario(Scenario::known); pump(view);
    view.open(ui::Dialog::registration); screenshot(view, "09-register-type");
    auto* type = find_type(lv_scr_act(), &lv_dropdown_class);
    require(type && std::string(lv_dropdown_get_options(type)) == "Other\nPortafilter\nPour-over set", "Equipment kind choices use Other, Portafilter and Pour-over set");
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
        click(view, 68 + (i % 5) * 84, 88 + (i / 5) * 46);
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
    require(controller.equipment()[3].id == 19, "Selected type and icon survive Back and Save");
    require(controller.equipment()[3].kind == Kind::pour_over_set, "Selected equipment type survives registration");
    screenshot(view, "13-registered");
    view.open(ui::Dialog::registration); pump(view);
    type = find_type(lv_scr_act(), &lv_dropdown_class);
    lv_dropdown_set_selected(type, 2);
    click(view, 370, 284); click(view, 370, 284);
    auto* used = icon_button(lv_scr_act(), ui::equipment_icons[19]);
    require(used && lv_obj_has_state(used, LV_STATE_DISABLED), "Registered kind/icon is disabled");
    auto* available = icon_button(lv_scr_act(), ui::equipment_icons[0]);
    require(available && !lv_obj_has_state(available, LV_STATE_DISABLED), "Unused icon remains selectable");
    lv_event_send(used, LV_EVENT_CLICKED, nullptr); pump(view);
    require(has_label(lv_scr_act(), "Pour-over set 🫘"), "Even injected events cannot select a duplicate");
    screenshot(view, "29-registered-icon-disabled");
    click(view, 65, 282); click(view, 65, 282);
    type = find_type(lv_scr_act(), &lv_dropdown_class);
    lv_dropdown_set_selected(type, 0);
    click(view, 370, 284); click(view, 370, 284);
    available = icon_button(lv_scr_act(), ui::equipment_icons[19]);
    require(available && !lv_obj_has_state(available, LV_STATE_DISABLED), "Same icon is available in another kind");
    view.open(ui::Dialog::objects); pump(view);
    click(view, 344, 84); // First row trash button.
    screenshot(view, "14-remove-equipment");
    require(controller.equipment_count() == 4, "Remove requires confirmation");
    click(view, 160, 270); // Cancel.
    require(controller.equipment_count() == 4, "Cancel preserves equipment");
    click(view, 344, 84); click(view, 310, 270);
    require(controller.equipment_count() == 3, "Confirm removes the intended equipment");
    require(controller.equipment()[0].kind == Kind::portafilter, "List compacts after removal");
    for (int i = 0; i < 3; ++i) { click(view, 344, 84); click(view, 310, 270); }
    require(controller.equipment_count() == 0, "All equipment can be removed");
    screenshot(view, "15-empty-equipment");
    click(view, 240, 276); pump(view);
    require(find_type(lv_scr_act(), &lv_dropdown_class), "Empty list still allows registration");
    std::cout << "UI interaction checks passed\n";
}
