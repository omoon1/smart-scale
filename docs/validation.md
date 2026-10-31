# Validation and remaining hardware checks

## Current target: 3.49-inch V2 / Rev1.1

The firmware now uses the AXS15231B BSP, 640×172 logical display, separate expander/touch I²C buses and HX711 on IO44/43. Older sections below describe previous 480×320/ST7796 milestones and remain historical.

Validated for this port:
- ESP-IDF 5.5.2 firmware build completed, including bootloader, partition table and application. Application sources compiled without warnings. The transitive `esp_lvgl_port` generic touch adapter emits a deprecation warning with touch-base 1.2.1; the Rev1.1 BSP uses its own input callback.
- `make test` passed, including an exhaustive frame-rotation/strip-seam check and touch corner, multi-touch, malformed-packet and out-of-range tests.
- `make preview-wide` and `make preview` passed: wide and legacy UI workflows remain intact.
- `git diff --check` passed.

The development container required a local component-manager process-ID fallback because its `/proc` process lookup failed. That environment-only workaround is not part of the firmware or repository changes.

Physical display timing, cold-boot backlight, touch orientation and HX711 sampling while the display refreshes remain to be checked on Rev1.1 hardware. See [bring-up procedure](hardware.md).


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


## HX711 integration checks

- `make test`: existing 503 session checks and new signed ADC/calibration/tare/live-session suite pass.
- `make preview`: original UI interaction checks and live injected-raw touch calibration, tare, Start, and timeout checks pass. Screens 16–21 show disconnected, calibration, live, brewing, and fault states; visually inspected at 480×320.
- `make build`: ESP-IDF v5.5.2 / ESP32-S3 firmware compiles and links, including GPIO and NVS adapters.
- Local build environment note: the installed component manager 2.5.2 attempted to inspect sandbox PIDs through psutil and failed before configuration. Only its ignored local Python environment was adjusted to use the Linux parent PID fallback. No dependency workaround is committed; CI uses the existing Espressif container.
- Hardware acceptance remains open: electrical signal timing, load-cell polarity, repeatability/linearity, physical disconnection behavior, and calibration persistence on actual flash. See [HX711 acceptance steps](hx711.md).


## Display, recognition, and calibration follow-up

- Updated `make test`: 502 session checks plus scale/live tests pass. The legacy filled-portafilter inference tests were replaced with identity-only ±40 g boundary/ambiguity tests, because ±40 g cannot establish a dose.
- Added regression checks for ±0.1 g flicker at zero and at 100 g, accumulated small changes, negative readings, invalid-reading reset, tare epochs, and unchanged domain weight beneath a zeroed display.
- Added live recognition checks for both inclusive boundaries, unmatched and ambiguous placements, manual override without tare, identity lock during filling/brewing, fresh placement after removal, and dose measurement only after explicit tare.
- Actual LVGL touch tests calibrate with each of 20 g, 50 g, and 100 g and verify the resulting measurement, followed by tare/start/fault checks. `make preview` and the ESP-IDF firmware build pass.


## Acquisition, filtering, and stability follow-up

- `make test`: 502 session checks, existing live/display/recognition tests, and new timestamp/filter/stability tests pass. Fixtures now allow real filter settling rather than assuming ten samples are sufficient.
- New tests cover minimum stability duration at ~80 SPS, isolated spike rejection, duplicate/backward timestamps, sustained noise, slow drift rejection, real positive/negative steps at 10/~80 SPS, saturation/timeout resets, stale calibration cancellation, sample-timestamp flow with batched UI updates, and explicit acquisition-gap invalidation.
- `make preview` passes all existing and live touchscreen tests, including 20/50/100 g calibration. `make build` compiles and links the acquisition task/queue and domain changes with ESP-IDF v5.5.2.
- On-device acceptance still required: verify 10/80 SPS sample cadence, induce an extended UI/NVS stall to exercise the real FreeRTOS queue overflow path, verify timeout/reconnect, measure settling latency, and check repeatability/drift with known masses. Host tests exercise the controller's gap handling, not actual FreeRTOS scheduling or electrical timing.


## Bounded diagnostics and stability confirmation

Historical scale diagnostic mode: weight-change logging was capped at one line/second, plus one `hx711_diag` summary per five seconds. Current 3.49 V2 firmware disables `hx711_diag` and `scale_trace`; see [current power/touch logging](power-management.md#diagnostic-logs). Summaries report stability failure bits and measured limits, calibration, sample cadence/age, batching, overflow, and fault counts. Stability entry now requires 500 ms of continuous passing checks; exit on failure remains immediate, with no relaxation of the measurement thresholds. Native tests cover pending confirmation, entry, immediate exit, drift flags, and invalidation. Firmware and LVGL checks pass; hardware diagnosis awaits logs captured under a fixed load.


## Drift-window correction from device diagnostics

Supplied diagnostics showed `sd=0.0565–0.0760 g` below the 0.10 g limit, but short-window slopes of -0.0934 and +0.0758 g/s exceeded 0.05 g/s (`fail=4`). Sporadic `fail=0 hold_ms=0` indicates checks had just passed without sustained confirmation. Overflow/fault counters were zero. These sparse logs identify a stability-gate failure but do not establish the physical source of variation.

Drift now uses 3.2 s of history (minimum 3.0 s), separate from the unchanged 1.2 s noise/settling window. Limits and immediate failure exit are unchanged; initial stability takes approximately 3.5–4 s. A 320-sample buffer supports the longer window at 80 SPS. Diagnostics add drift-window count and duration without adding log lines.

`make test`, `make preview`, and the ESP-IDF firmware build pass. Synthetic ±0.08 g oscillation tests at 10, ~11, and ~80 SPS remain stable after warmup; the same noise with sustained 0.10 g/s drift is rejected. Sudden steps still fail short-window checks. This is a synthetic regression scenario informed by the log's noise scale, not a replay of unavailable raw samples. Physical verification remains necessary.


## Object recognition rearming

Fixed two reproducible paths: registering equipment while an already-unknown object remains on the platform now retries matching, and a normal brief lift now rearms matching without waiting for the full drift window. Removal requires 300 ms of near-zero median samples; a single low sample does not reset identity. Manual identity selection is permitted while settling, without weakening tare/calibration/start checks. Empty profile lists explicitly display Register object; profiles remain RAM-only. The existing diagnostic summary includes profile count and recognition state.

Native tests cover registration in place, transient lows, a 600 ms lift, replacement with a different profile, ambiguity resolved by deletion, and explicit selection while tare remains blocked. Existing session/filter suites, LVGL previews, and ESP-IDF build pass. Physical confirmation is still required; no claim is made that the user's exact cause is established without their `profiles`/`object` diagnostics.


## Dark display during boot

The display driver drives the active-high backlight GPIO low before I2C, panel reset, and LVGL initialization. PWM starts at zero duty. After `View::init()`, `Device::show()` runs with the LVGL lock held, invalidates the whole screen, forces a complete refresh, and waits for the final SPI transfer to complete before restoring the existing 818/1023 brightness. There is no fixed boot delay. If the final transfer does not complete within one second after refresh returns, startup fails with the backlight still off.

Validation for this change: `make test` and `git diff --check` pass. Local firmware compilation is unavailable because the ESP-IDF toolchain is not installed; the existing firmware CI job must confirm the target build. Native tests do not exercise GPIO, PWM, or SPI DMA.

On-device acceptance:

- Repeat cold power-on, EN reset, and software restart; the panel should remain dark until the complete initial UI appears, without noise or partially painted areas.
- Confirm touch, BOOT-button reset, and normal screen updates still work at the original brightness.
- Check the interval before `app_main()` separately: firmware cannot guarantee backlight-off during ROM/bootloader execution. If noise remains there, verify the board's backlight enable bias and consider a hardware default-off circuit.

## Main integration: battery, idle and touch wake

The wide branch includes main commit `7578c45` and retains the 3.49 V2 BSP.
Run `make test`, `make preview-wide`, and `make preview`. Wide interaction tests
cover opening battery history while brewing, 6h/1d/3d range controls and close.
Voltage tests reject implausible ADC input and ensure estimated SOC never claims
charging/full/external power. Configuration compilation checks accept all 30
ordered distinct pairs from 38/39/40/41/43/44 and reject duplicates or GPIO42.

Review the hardware checklist in [power-management.md](power-management.md)
before enabling `CONFIG_SMART_SCALE_TOUCH_WAKE_VERIFIED`. Firmware compilation
must cover both its default-disabled and enabled paths. Physically measure
backlight polarity, wake behavior, battery voltage and power draw on Rev1.1.

Integration validation completed with ESP-IDF 5.5.2: full bootloader, partition
and application builds pass with defaults (DOUT44/SCK43, light sleep disabled)
and with light sleep enabled plus DOUT40/SCK41. The generic managed LVGL touch
adapter emits its existing deprecated API warning; application sources have no
compiler warnings. The container needed a local-only Python environment repair
and component-manager PID fallback; neither workaround is part of the repository.
Physical validation is still pending.

### Main dd72309 re-integration

Native tests and wide UI interactions pass after integrating PR #15. Added
3.49 EXIO0 tests cover held touch, touch arriving during acknowledgement,
non-touch shared IRQs, ambiguous pulses and read errors without assuming a PMU
on EXIO5. Firmware builds pass with ESP-IDF 5.5.2 for default wake-disabled
DOUT44/SCK43 and wake-enabled DOUT40/SCK41. CI now uses those two 3.49 modes
instead of the upstream 3.5/custom-direct mappings. Physical wake tests remain
pending; the new common policy permits sleep at 60 seconds total idle.
