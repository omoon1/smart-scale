# Battery and idle power management

This change targets the current `main` **ESP32-S3-Touch-LCD-3.5 / ST7796 / FT6336 / AXP2101** adapter. It does not port the application to the 3.49-inch Rev1.1/V2 board. That board needs its own battery adapter and verified touch-wake mapping; its charging circuitry must not be accessed using AXP2101 registers.

## Behavior

| Idle time | State | Behavior |
| --- | --- | --- |
| Under 30 s | Active | Existing normal backlight duty 818/1023 |
| 30 s | Dimmed | Duty 245/1023, approximately 30% of normal |
| 60 s | Screen off | Backlight zero, display refresh timer paused; touch and weighing continue |
| 360 s | Sleep eligible | Five continuous minutes screen-off; light sleep only with verified touch IRQ configuration |

Touch, the BOOT/application button, or a weight change of at least 0.5 g sustained for 300 ms resets idle. The weight anchor is retained so slow cumulative changes count; invalid readings and ordinary sensor noise do not. Brewing (including flow pauses) and editing dialogs keep the display bright; the read-only battery history panel does not inhibit idle transitions. This is deliberately stronger than only blocking sleep during calibration and registration. Releasing an inhibitor starts a fresh idle interval. USB uses the same policy.

The first touch or application-button press on a dark screen only wakes it. Touch suppression lasts through release and occurs in the input adapter before any widget sees the gesture, including dropdowns and long presses. Weight changes can wake screen-off, but cannot wake sleep because HX711 is powered down.

## Battery header

The header replaces HX711 LIVE and the fake demo 82% with a battery icon and actual PMU-reported percentage. The icon and percentage are green while charging, gray at 20–100% when not charging, and red below 20% when not charging. Charging takes precedence even below 20%. No Charging or Full text label is displayed. Unknown and absent readings stay gray; USB/No battery labels remain for absent batteries. Demo mode says `DEMO battery --`.

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
- Verify 30-second dim and 60-second screen-off; confirm no sleep during brewing or editing dialogs, and normal idle transitions in battery history.
- Touch each underlying control while dark: no tare, dose edit, selection, dropdown, or reset may occur. A second gesture must work.
- Verify six-minute sleep and at least 100 touch-wake cycles with a loaded platform. No false auto-brew, lost tare, corrupted state, or flash of undefined panel RAM is acceptable.
- Test an already-low IRQ, held/wet touch, HX711 absence, wake during sleep entry, and failed suspend. Failures must leave a usable device.
- Measure battery-side active/dim/off/sleep current with USB disconnected. Record exact board revision, firmware, battery, wake latency, and first-stable-sample latency. Proposed UI targets: 250 ms from screen-off and 1 s from light sleep; these are unmeasured targets.

Physical battery, display, and sleep tests cannot be substituted by host tests. Do not enable this configuration for the 3.49 V2 merely because it exposes TP_INT.


## Seven-day battery usage

Tap the battery indicator, aligned to the top-right with a 120 x 44 px touch target,
from any main-screen phase to open Battery history. The scrollable table shows
today and the previous six UTC dates, with Charging, Active and Sleep in hours:minutes,
and the first/latest battery percentages for each day. Missing records show `--`.
An additional Undated row retains usage that cannot be assigned to a date.
There is no periodic sample chart or remaining-runtime estimate.

Categories are mutually exclusive:

- Charging means **plugged in**, including full battery and sleep on external power.
  The table explains this; header green still means actual charger activity.
- Active means on battery with CPU awake, including dimmed and screen-off.
- Sleep means on battery during a successful light-sleep call only. Failed attempts
  and setup/resume overhead are Active. Disabled sleep leaves Sleep at zero.
- Unknown power source and powered-off intervals are not added to these totals.

History is a read-only dialog: it permits idle dimming/screen-off/sleep and does
not disable automatic brew detection. A brew phase transition closes the panel;
opening it during a running brew leaves brewing intact. Clock editing inhibits
idle transitions like other editing dialogs. The first dark-screen touch remains
consumed by the existing global wake-touch gate.

### Clock and completeness

Main has no network/RTC synchronization. The panel includes **Set UTC date / time**
with year/month/day/hour/minute selectors and calendar-date validation. Firmware
uses UTC explicitly, without DST or local-time assumptions. A cold boot requires
setting the clock again unless another platform clock source has supplied a valid
2024–2099 timestamp. No timestamp is reconstructed from the last checkpoint.
Previously undated usage stays undated after setting the clock.

Elapsed time comes from the monotonic ESP timer, which accounts for light sleep;
brew/session timers are not used. Continuous dated intervals are split at UTC
midnight. Forward gaps expire old daily records. Clock jumps go to the undated
summary; backward adjustments never resurrect expired records or overwrite a
newer ring entry with an out-of-window date. The retained calendar window follows
the latest observed date until a backwards-corrected clock catches up.

`*` marks partial/estimated records. Restored history is conservatively marked
partial because an interrupted interval may not have reached flash. A sleep
interval is estimated because this board does not observe power-source changes
while asleep. It is classified using the pre-sleep source; fresh status is read
after wake. Source polling has up to roughly five seconds of detection latency.
No exact plug/unplug time during sleep is claimed.

### Storage and writes

`BatteryHistory` owns fixed storage: seven 24-byte daily records, one 24-byte
undated record, and an eight-byte header, **200 bytes total**. Each daily record
stores epoch day, three second counters, first/latest/min/max battery percentage,
and quality flags. Percentage 255 means unavailable. Runtime bookkeeping is
separate and is not restored as a running/sleeping state.

`BatteryHistoryStore` writes one versioned NVS blob (`battery_usage/history`).
It loads after the existing NVS initialization, validates size/version/content,
and checkpoints every 15 minutes while awake, on source/day/clock changes, before
sleep, and after wake. Event bursts are coalesced for up to 60 seconds; pre-sleep
checkpoints bypass that window (one-second minimum). Failed writes retry with a
60-second minimum and leave RAM data dirty. The device never wakes just to save.
NVS metadata and garbage collection add physical storage/write overhead beyond
this payload. Unexpected power loss can discard up to 15 minutes of awake usage,
or the uncheckpointed portion of a sleep interval. NVS failure leaves history
functional in RAM and emits a warning; it does not stop weighing or erase calibration.

### Validation

`make test` includes history accounting, midnight split, seven-day expiry, long
intervals, invalid/unknown readings, clock jumps, checkpoint restoration, and
corrupt schema/counter rejection. A fake-NVS adapter test covers blob round trips, event coalescing, pre-sleep saves, and retry after commit failure. `make preview` exercises actual top-right touch
navigation, history/clock/undated screens, and the read-only panel's idle policy.
Physical acceptance still requires NVS power-cycle tests, clock retention checks,
source transitions during sleep, battery gauge accuracy, and repeated sleep/wake
cycles on the verified board.
