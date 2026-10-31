# Battery and idle power — 3.49 V2 / Rev1.1

The wide-panel branch integrates main's idle policy, acquisition suspend/resume,
wake gesture gate, battery history and UI. The board adapter is specific to
AXS15231B + TCA9554 + ETA6098; **never access AXP2101 registers at 0x34**.

| Idle | Behavior |
| --- | --- |
| 30 seconds | Dim to about 30% (main policy); active-low GPIO42 duty 716/1023 |
| 60 seconds | Gate backlight off at EXIO1 and pause display refresh; touch/scale continue |
| 360 seconds | Light sleep if explicitly enabled after board validation; otherwise stay screen-off |

Full brightness uses GPIO42 PWM duty 0. Initialization holds EXIO1 low until a
complete frame has reached the panel. Wake redraws before enabling EXIO1.
The first dark-screen touch is consumed until release, including held gestures;
failed reads and malformed/multiple contacts release widgets without clearing
the wake gate. Touch, BOOT or sustained weight change wakes screen-off. Brewing
and editors inhibit idle; battery history does not. Main's tare, calibration,
container and dose preservation on acquisition resume is retained.

## Battery telemetry limitations

GPIO4 / ADC1 channel 3 samples the 200k/100k divider (battery = ADC voltage × 3).
Sixteen calibrated readings are averaged every five seconds. Failed ADC setup,
calibration, reads or implausible voltage yields `Battery --`, not stale SOC.
A generic 1S Li-ion voltage curve produces **approximate** SOC, visibly prefixed
with `~`. It is not a calibrated fuel gauge: load, temperature, cell chemistry,
charger voltage and missing battery can distort the estimate. In particular,
a plausible charger terminal voltage cannot prove a battery is connected.
Do not use this estimate for safety cutoff or remaining-runtime guarantees.

ETA6098 STAT drives the onboard LED; it is not routed to an MCU input in the
Rev1.1 schematic. Firmware therefore does not claim charging, full or USB power
status. Voltage-only history is gray `Estimated`; it is never labeled Charging
or on-battery Active. Actual sleep intervals remain dashed Sleep estimates.
Precise charging/absence detection requires additional hardware telemetry.

## Touch to wake

Screen-off touch wake works by polling and is enabled by default. For CPU light
sleep, `make menuconfig` → **Smart scale power** → **Enable verified 3.49 V2
expander touch wake**, after checking this physical path:

`AXS15231B TP_INT → TCA9554 EXIO0 → active-low INT → ESP32 GPIO8`.

GPIO42 is backlight, not the interrupt on V2. The adapter reads actual TCA9554
input register 0x00 to acknowledge the shared interrupt, rejects held touch or
failed I2C, then checks GPIO8 before sleep. IMU/RTC changes can also wake this
shared line. HX711 acquisition must acknowledge suspension before sleep; on
wake it resumes, clears transient filters and waits for stable samples. Touch
and panel stay powered; EXIO6 system hold stays high. No deep sleep or PWR
button shutdown is implemented. Default light sleep remains disabled until
hardware validation; software tests cannot establish electrical wake behavior.

## Hardware acceptance

- Verify active/dim/off polarity, clean cold boot and wake redraw.
- Touch Tare, Reset, coffee and dropdown areas while dark: first gesture must
  only wake; release and a second gesture must activate the control.
- Validate at least 100 light-sleep touch-wake cycles, held/wet contact, shared
  RTC/IMU interrupt, touch disconnect and wake during sleep entry.
- Compare ADC voltage with a meter on battery/USB/absent battery and record
  estimation error; confirm no fabricated charging state.
- Measure battery-side current in active/dim/off/sleep with USB disconnected.
  No current savings or wake latency are claimed without measurements.

Sources: [V2 schematic](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/blob/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/schematic/ESP32-S3-Touch-LCD-3.49%20V2.pdf),
[V2 ADC example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/tree/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/ESP-IDF/01_ADC_Test),
[V2 pin definitions](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/blob/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/ESP-IDF/07_BATT_PWR_Test/main/user_config.h).

## Battery Usage graph

Tap the top-right battery indicator to open **Battery Usage**. The only text in
this panel is the title and graph-related controls, axes, and legend. Range
buttons select **6hr**, **1day**, or **3day**. The vertical axis is battery percent
(0–100%); the horizontal axis is elapsed time ending at Now. No UTC dates, clock
settings, daily table, status prose, or extra summary labels are shown.

For adapters with power-source telemetry, trace colors identify exclusive device states: green Charging (plugged in,
including full battery), blue Active (on battery, including dimmed/screen-off),
and orange Sleep (actual successful light sleep on battery). Header green still
means actual charger activity. Sleep joins are dashed estimates between readings
before and after sleep; no measurements are collected while asleep. Unavailable
battery readings break the trace. Power changes during sleep cannot be timed,
so the interval uses the pre-sleep power source where known. V2 has no source telemetry; its gray Estimated trace does not assert a power source. Before-boot time stays empty.

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
