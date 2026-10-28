# Validation and remaining hardware checks

## Completed checks

- `make install`: installed and reran the ESP-IDF v5.5.2 bootstrap using the repository-local `.tools/` paths. CMake 3.30.5 and Ninja 1.11.1.3 are installed in a local bootstrap venv.
- `make idf ARGS=--version`: reports ESP-IDF v5.5.2.
- `make build`: cross-compiles and links the ESP32-S3 app, bootloader and partition table using GNU C++20 for the ESP-IDF build. The core also compiles with strict standard C++20 on the host.
- `make test`: 470 checks pass, including required candidate boundaries, ambiguity, actual dose preservation, sequential placement, extraction locks, unknown baseline, paused flow, abort/reset, session isolation, snapshot history, input validation, capacity limits, and bounded graph timestamps.
- `make preview`: the same `ui.cxx` is compiled against LVGL 8.4.0 and rendered at 480×320; 13 screenshots are produced. Touch-driven checks exercise object/dose dialogs, draft cancellation, dose commit, start/finish/next, remove-load gating, popup interruption, and equipment registration.
- Native SDL2 variant: compiled against SDL2 headers and the installed SDL2 runtime; a 20-frame display/texture smoke check passes using SDL's dummy video driver. An interactive window requires a desktop display server.
- Visual inspection: ready, objects, dose, extraction, result, remove-load, unknown, pour-over pause, all three registration steps, and registered list inspected. Registration uses a bundled monochrome icon font and no keyboard.
- `bash -n scripts/*.sh`: installer/wrapper/preview scripts pass syntax checks.

The test environment has a process namespace where `psutil.Process()` cannot resolve its own PID. Component Manager 2.5.2's CMake parent lookup needed an **environment-only** `psutil.NoSuchProcess` fallback to `os.getppid()` in the ignored `.tools/` Python package. No toolchain source, application source, or dependency lock was altered to bypass compilation. This workaround is not shipped; normal Linux/macOS installations should not need it.

The unmodified vendor FT6336 driver emits one unused static-function warning. Application sources compile without warnings. CI definitions are included; the checks above describe local validation. GitHub Actions results are tracked separately on the pull request.

## Corner Cases & Failure Modes

Passing host tests does not establish sensor accuracy or the physical ≤2-second detection requirement. There is no attached load cell or physical board in this environment. Follow `hardware.md` to verify cold boot, LCD colors/orientation, touch alignment, BOOT debounce, SPI stability, free DMA memory, readability, and USB/battery behavior.

Power-loss recovery, durable local records, equipment/history persistence, cloud synchronization, recognition learning, and calibration remain outside this UX milestone. The UI explicitly labels simulated data and RAM-only results. No firmware was flashed to a device.

## Dose responsiveness regression checks

`make preview` exercises presets, coarse/fine increments, both dose bounds, close/reopen persistence, and brew-time editor dismissal. It also asserts that adjustments retain the current screen and that an unchanged dose dialog produces zero flush callbacks across ten 100 ms updates after button transitions settle. `make test` covers the controller state machine.

On device, compare touch-to-visible latency and energy per dose-selection task at the same brightness. Verify 0.1 and 60.0 g limits and extraction transitions. Desktop flush counts verify removal of redundant rendering, not physical frame rate or battery-life improvement.

## Equipment icon and layout checks

The host checks Container type options, stable-weight gating, absence of keyboard/text input, all 20 in-place icon selections, Back navigation, and saving the selected type plus icon. Ready and brewing weight units use a separate smaller label aligned by font baseline. Review screenshots include the three-step registration, the selected icon, and larger dose buttons with no title/footer. Device touch accuracy and battery use remain physical validation tasks.

The icon set uses original vector geometry, pre-rendered at 32 px for the picker and 22 px for names. Host checks require both fonts to contain all 20 identifiers. Portafilter uses private-use U+E004; other icons use Unicode identifiers. Profiles are currently RAM-only, so no persistent-name migration is needed.

## Held adjustment and equipment removal

Host tests cover repeated fine adjustment, no extra increment on release, stopping after release/slide-off/brew start, removal confirmation and cancel, list compaction, removal of all profiles, and registration from the empty state. Controller tests cover phase locks, invalid IDs, monotonic IDs after deletion, preserved readings and archived snapshots, and all demo types with an empty profile list. Repeat defaults are LVGL 8.4's 400 ms hold threshold and 100 ms interval. Check touch release/slide-off behavior on the physical FT6336 device before release.
