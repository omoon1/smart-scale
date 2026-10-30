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

Sleep is disabled by default because board revision cannot be detected reliably in software.
For a **3.5-inch V2 / Rev2.0** board, run `make menuconfig` → **Smart scale power** →
**Waveshare ESP32-S3-Touch-LCD-3.5 V2 (Rev2.0)**, save, then rebuild and flash.
This sets `CONFIG_SMART_SCALE_BOARD_35_V2=y` and enables the expander-aware path.
Existing sdkconfig files remain safe until this explicit board selection is made.
The boot log must say `wake route: 3.5 V2 TP_INT -> TCA9554 P2 -> GPIO4`.

| Board | Touch interrupt | ESP32 wake connection |
| --- | --- | --- |
| Original/V1 schematic | FT6336 TP_INT → TCA9554 P2 / EXIO2 | Expander INT has a pull-up but no MCU connection shown |
| V2 / Rev2.0 | FT6336 TP_INT → TCA9554 P2 / EXIO2 | Expander INT / EXIO_INT → GPIO4, active low |

Sources: [Waveshare resources](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5/Resources-And-Documents),
[original schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5/ESP32-S3-Touch-LCD-3.5-Schematic.pdf),
[Rev2.0 schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5/ESP32-S3-Touch-LCD-3.5_Rev2.0.pdf),
[TI TCA9554 interrupt behavior, section 8.3.2](https://www.ti.com/lit/ds/symlink/tca9554.pdf).
This is a schematic-verified mapping; electrical wake behavior still requires physical testing.
Do not select V2 for V1, 3.49, or 3.5B. V1 needs a separate timer-polling/button-wake
implementation or verified custom wiring; neither is added here.

The custom direct-touch path remains available through
`CONFIG_SMART_SCALE_TOUCH_WAKE_VERIFIED=y` when the V2 option is off.
Its configurable GPIO16 default is only a placeholder, not a stock board mapping.
Runtime validation rejects conflicts with display, HX711, I2C, BOOT, USB, and memory pins.

The V2 sequence:

1. Hold the LVGL lock, reject active/held touch, and ask acquisition to power HX711
   down with acknowledgement (500 ms timeout).
2. Configure GPIO4 as a pulled-up input and stop LVGL. Set only expander P2 to input,
   preserving other directions and all output latches, including LCD reset and power control.
3. Read FT6336 TD_STATUS, read the full expander input port to acknowledge shared
   interrupts and capture a baseline, then read TD_STATUS again. A held touch or low
   P2 defers sleep and wakes the display. An I2C failure or pending shared INT defers sleep.
4. Arm low-level GPIO wake and recheck GPIO4 immediately before sleeping. Do not
   read the expander again between that check and sleep, to avoid clearing a new event.
5. On successful wake, record the actual sleep interval, read/acknowledge the expander
   input port, and read TD_STATUS. Touch points, low P2, or a changed P2 mean touch.
   A known change on other input pins alone is a shared peripheral event and stays dark.
   An unchanged snapshot (possible short pulse) or an I2C failure wakes conservatively.
6. Disable the wake source, resume LVGL and HX711, clear transient measurement filters,
   and preserve calibration, tare, container and dose. Real/ambiguous wakes restart the
   idle interval and consume the waking gesture before controls can see it. Known shared
   events and entry failures retain screen-off and wait 30 seconds before another attempt.
   Regular touch/button handling remains available during that backoff.

Logs identify the selected route, sleep entry, wake source, elapsed sleep time and
expander snapshots. Wake reason codes: 0 touch, 1 shared, 2 ambiguous, 3 I2C error.
Preparation codes: 0 ready, 1 touch, 2 pending IRQ, 3 I2C error.
The history marks only successful sleep-call intervals as Sleep; setup, failed entry,
and retry backoff remain Active. Post-wake I2C errors do not erase a successful sleep interval.

### Corner cases and hardware validation

- A held/wet touch or stuck IRQ must not create an immediate sleep loop. Entry failures
  and shared wakes retry at most once per 30 seconds, keeping the backlight off unless
  touch is detected. Persistent noise can still consume significant power during backoff.
- The expander interrupt is shared, not touch-specific. RTC/PMU/SD events can wake the
  CPU. Do not drive those inputs as outputs to suppress their interrupts. Test charger
  plug/unplug and other connected peripherals with touch absent and present.
- The TCA9554 does not retain a per-source interrupt history. A short touch pulse that
  coincides with a persistent unrelated change may be indistinguishable from that change;
  a second tap may be required. Test quick taps, held touches and wake-entry races.
- All input-port reads acknowledge the shared IRQ. Keep them serialized with the sleep
  sequence; do not introduce an independent expander ISR/task reading the port during entry.
- Verify repeated sleep/wake cycles, no unintended tare/dose changes, I2C-failure recovery,
  orange history after wake, and battery-side current with USB disconnected. Compilation
  and host tests cannot establish real current consumption or interrupt timing.

The panel and touch controller stay powered; no shared rail is switched and no panel
sleep command is sent. Other onboard peripheral rails retain their existing settings.
This is not full board shutdown and does not claim chip-only standby current. No BLE/Wi-Fi
service is active on this baseline; future radio/OTA integration must explicitly coordinate sleep.

See [ESP-IDF ESP32-S3 sleep modes](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/sleep_modes.html). Light sleep preserves internal state; deep sleep and checkpoint restoration are not implemented.

## Validation

Automated: `make test` exercises 30/60/360-second boundaries, inhibitor release, long uptime, noise, slow cumulative changes, invalid samples, wake-gesture consumption, PMU states, and preparation-session preservation after measurement resume. Existing brew, calibration, diagnostics, filtering, and recognition suites remain required. `make preview` compiles and renders the shared LVGL UI.

Before shipping sleep support on hardware:

- Confirm real battery percentage/status on battery, USB, charging, full, absent, and PMU failure cases.
- Verify 30-second dim and 60-second screen-off; confirm no sleep during brewing or editing dialogs, and normal idle transitions in battery history.
- Touch each underlying control while dark: no tare, dose edit, selection, dropdown, or reset may occur. A second gesture must work.
- Verify six-minute sleep and at least 100 touch-wake cycles with a loaded platform. No false auto-brew, lost tare, corrupted state, or flash of undefined panel RAM is acceptable.
- Test an already-low IRQ, held/wet touch, HX711 absence, wake during sleep entry, and failed suspend. Failures must leave a usable device.
- Measure battery-side active/dim/off/sleep current with USB disconnected. Record exact board revision, firmware, battery, wake latency, and first-stable-sample latency. Proposed UI targets: 250 ms from screen-off and 1 s from light sleep; these are unmeasured targets.

Physical battery, display, and sleep tests cannot be substituted by host tests. Do not enable this configuration for the 3.49 V2 merely because it exposes TP_INT.


## Battery Usage graph

Tap the top-right battery indicator to open **Battery Usage**. The only text in
this panel is the title and graph-related controls, axes, and legend. Range
buttons select **6hr**, **1day**, or **3day**. The vertical axis is battery percent
(0–100%); the horizontal axis is elapsed time ending at Now. No UTC dates, clock
settings, daily table, status prose, or extra summary labels are shown.

Trace colors identify exclusive device states: green Charging (plugged in,
including full battery), blue Active (on battery, including dimmed/screen-off),
and orange Sleep (actual successful light sleep on battery). Header green still
means actual charger activity. Sleep joins are dashed estimates between readings
before and after sleep; no measurements are collected while asleep. Unavailable
battery readings break the trace. Power changes during sleep cannot be timed,
so the interval uses the pre-sleep power source. Before-boot time stays empty.

History uses only monotonic uptime, including light sleep, and never wall time.
It survives light sleep in RAM and starts empty on every firmware boot, including
power-off/on, reset, or watchdog restart. There is no NVS history read/write and
no date/time setup. Legacy history blobs from earlier firmware are ignored;
calibration persistence is unchanged.

The bounded ring holds up to 2,048 endpoints (32 KiB plus small bookkeeping),
allocated in PSRAM. It records five-minute samples plus state and telemetry
validity transitions, and maintains a current live endpoint. Records older than
72 hours are evicted, retaining one anchor for clipping an interval at the window
boundary. If unusually frequent state transitions fill the ring, oldest records
are evicted first; retention can be shorter than three days. Memory is bounded,
and no flash writes or timer wakes are needed for history.

The graph draws clipped line segments directly without creating an LVGL object
for every point. Opening or switching ranges does not alter collection, timers,
weighing, or brewing. The read-only panel permits idle dimming/screen-off/sleep
and automatic brew detection; it remains accessible during a running brew.
The first touch on a dark screen is still consumed by the global wake-touch gate.

Validation covers all three ranges, state colors, clipping, unavailable readings,
72-hour retention, capacity pressure, empty/new boot history, light-sleep joins,
and top-right touch navigation. Host screenshots cover populated and empty graphs.
Physical checks still require battery gauge validation and repeated sleep/wake
and power-cycle tests on the verified board.
