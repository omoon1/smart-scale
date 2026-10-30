# Battery and idle power management

This change targets the current `main` **ESP32-S3-Touch-LCD-3.5 / ST7796 / FT6336 / AXP2101** adapter. It does not port the application to the 3.49-inch Rev1.1/V2 board. That board needs its own battery adapter and verified touch-wake mapping; its charging circuitry must not be accessed using AXP2101 registers.

## Behavior

| Idle time | State | Behavior |
| --- | --- | --- |
| Under 30 s | Active | Existing normal backlight duty 818/1023 |
| 30 s | Dimmed | Duty 409/1023, half of normal |
| 60 s | Screen off | Backlight zero, display refresh timer paused; touch and weighing continue |
| 360 s | Sleep eligible | Five continuous minutes screen-off; light sleep only with verified touch IRQ configuration |

Touch, the BOOT/application button, or a weight change of at least 0.5 g sustained for 300 ms resets idle. The weight anchor is retained so slow cumulative changes count; invalid readings and ordinary sensor noise do not. Brewing (including flow pauses) and all open dialogs keep the display bright. This is deliberately stronger than only blocking sleep during calibration and registration. Releasing an inhibitor starts a fresh idle interval. USB uses the same policy.

The first touch or application-button press on a dark screen only wakes it. Touch suppression lasts through release and occurs in the input adapter before any widget sees the gesture, including dropdowns and long presses. Weight changes can wake screen-off, but cannot wake sleep because HX711 is powered down.

## Battery header

The header replaces HX711 LIVE and the fake demo 82% with a battery icon and actual PMU-reported percentage/status. Demo mode says `DEMO battery --`.

The existing AXP2101 is read every five seconds under the shared I2C/LVGL ownership lock: chip ID 0x03 must be 0x4A; status registers 0x00/0x01 distinguish battery presence, valid external power, charging, and charge completion; SOC is 0xA4. No charger, rail, fuel-gauge model, or charge-current settings are changed. Percentage is the PMU estimate, not a calibrated runtime claim. Battery removal and invalid SOC suppress the percentage; communication failure immediately displays unknown rather than keeping stale information. A failed SOC read does not erase successfully read charging status. The next wake forces a sample.

Register semantics were checked against [XPowersLib AXP2101](https://github.com/lewisxhe/XPowersLib/blob/master/src/XPowersAXP2101.hpp) and its [register constants](https://github.com/lewisxhe/XPowersLib/blob/master/src/REG/AXP2101Constants.h). Charging requires status2 bits 7:5 = 1; charge done uses bits 2:0 = 4. External power is distinct from charging.

## Sleep configuration and limitations

Default: `CONFIG_SMART_SCALE_TOUCH_WAKE_VERIFIED=n`. Main has no verified touch interrupt pin. At six minutes, the firmware logs the missing capability once per idle interval and remains screen-off. It does not enter sleep that cannot be exited by touch.

After verifying the exact board wiring and active-low IRQ behavior with touch powered, use `make menuconfig` → Smart scale power. Enable verified touch wake and select the actual GPIO. The numerical default is a placeholder, **not a board mapping**. Runtime validation rejects conflicts with this adapter's display, HX711, I2C, BOOT, USB, and memory pins. Electrical validation of the selected exposed pin is still required.

The enabled path:

1. Hold application/LVGL ownership; reject pending or held touch and active operations.
2. Ask the acquisition task to power HX711 down; wait at most 500 ms for acknowledgement. Reset the queued readings only after acknowledgement.
3. Configure low-level GPIO wake; reject an already-low line. Stop the LVGL tick/handler and recheck the line before `esp_light_sleep_start()`.
4. Resume LVGL, remove temporary wake configuration, suppress the waking gesture, restart HX711, and clear transient measurement filters without resetting calibration, tare, container, or dose.
5. Resume session interpretation only after fresh stable samples. Exclude sleep time from session timing and redraw a complete frame before enabling the backlight.

Abort paths cancel acquisition suspension and restore active operation. A failed sleep attempt is logged and receives a fresh idle interval. The panel and touch controller stay powered: no unverified sleep command or shared rail is switched. Other onboard peripheral rails retain their existing settings. Savings therefore require battery-side measurement; this is not full board shutdown or a claim of chip-only standby current. No BLE/Wi-Fi service is active on this baseline; future radio/OTA integration must add explicit suspension/inhibition before using this path.

See [ESP-IDF ESP32-S3 sleep modes](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/sleep_modes.html). Light sleep preserves internal state; deep sleep and checkpoint restoration are not implemented.

## Validation

Automated: `make test` exercises 30/60/360-second boundaries, inhibitor release, long uptime, noise, slow cumulative changes, invalid samples, wake-gesture consumption, PMU states, and preparation-session preservation after measurement resume. Existing brew, calibration, diagnostics, filtering, and recognition suites remain required. `make preview` compiles and renders the shared LVGL UI.

Before enabling verified sleep on hardware:

- Confirm real battery percentage/status on battery, USB, charging, full, absent, and PMU failure cases.
- Verify 30-second dim and 60-second screen-off; confirm no sleep during brewing or dialogs.
- Touch each underlying control while dark: no tare, dose edit, selection, dropdown, or reset may occur. A second gesture must work.
- Verify six-minute sleep and at least 100 touch-wake cycles with a loaded platform. No false auto-brew, lost tare, corrupted state, or flash of undefined panel RAM is acceptable.
- Test an already-low IRQ, held/wet touch, HX711 absence, wake during sleep entry, and failed suspend. Failures must leave a usable device.
- Measure battery-side active/dim/off/sleep current with USB disconnected. Record exact board revision, firmware, battery, wake latency, and first-stable-sample latency. Proposed UI targets: 250 ms from screen-off and 1 s from light sleep; these are unmeasured targets.

Physical battery, display, and sleep tests cannot be substituted by host tests. Do not enable this configuration for the 3.49 V2 merely because it exposes TP_INT.
