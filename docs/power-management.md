# Battery and idle power — 3.49 V2 / Rev1.1

The wide-panel branch integrates main's idle policy, acquisition suspend/resume,
wake gesture gate, battery history and UI. The board adapter is specific to
AXS15231B + TCA9554 + ETA6098; **never access AXP2101 registers at 0x34**.

| Idle | Behavior |
| --- | --- |
| 30 seconds | Dim to about 30% (main policy); active-low GPIO42 duty 716/1023 |
| 60 seconds | Gate backlight off at EXIO1 and pause display refresh; touch/scale continue |
| 60 seconds | Light sleep if explicitly enabled after board validation; otherwise stay screen-off |

These deadlines are total time since the last valid activity, independent of
battery/external/unknown power status. Plugging or unplugging USB does not reset
idle or wake the screen. Battery telemetry is used only for display/history.

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

## Main update dd72309 (PR #15)

The common policy now makes sleep eligible at **60 seconds total idle**, rather
than 360 seconds. With the verified-wake option disabled the board stays screen-off.
The former external-power exception (10-minute screen-off, no sleep) has been
removed. All adapters now dim at 30 seconds and become sleep-eligible at 60
seconds, regardless of source telemetry. Power-source changes do not reset idle.
Native tests cover battery, charging, full, unknown and failed telemetry, source
transitions across the deadline, touch restart, inhibition release and retry delay.
No AXP2101 charger register accesses or 3.5 V2 pin assignments are used.

Sleep preparation reads touch before and after acknowledging the physical
expander input port. A held contact, I2C error or pending GPIO8 interrupt defers
sleep. After logging, GPIO8 is checked again immediately before sleep without
another acknowledgement. Wake classification uses EXIO0: a known non-touch
change stays dark and retries after 30 seconds; touch, ambiguous pulses and
post-wake read errors wake conservatively. Entry failures also retry after 30
seconds without illuminating the screen. Sleep timestamps only describe an
actually successful sleep. The host helper retains its upstream 3.5 defaults
for regression tests; the 3.49 adapter explicitly supplies touch mask 0x01 and
pending-PMU mask 0x00.


## Diagnostic logs

The 3.49 V2 firmware emits queued `power`, `touch`, and `battery` events for
intermittent touch failures after sleep. `scale_trace` capture/dumps and the
five-second `hx711_diag` stream are disabled; ordinary `scale_event` transitions
and startup scale configuration remain available. The verified wake configuration gate remains in place.

Capture the complete serial output from boot through a failure, including the
startup `power event=config` (`sleep_enabled`, firmware/IDF version, reset reason).
Keep the unfiltered capture so errors, resets and display diagnostics survive.
Annotate when you touched the panel, whether the backlight came on, whether a
second tap worked, and whether USB or battery powered the test.

Each sleep attempt has a `cycle` number. Follow these markers in order:

1. `suspend_begin` / `suspend_end`: HX711 task suspension result and latency.
2. `sleep_prepare`, `lvgl_stop`, `expander_read`, `sleep_probe`, `prepared`:
   touch state, wake gesture suppression, I2C results, expander inputs and GPIO8.
3. `sleep_arm`, optional `sleep_abort`, `sleep_return`: GPIO wake setup,
   pending-IRQ rejection, actual sleep call result, duration and wake cause.
4. `wake_classified`, `wake_cleanup`, `lvgl_resume`, `wake_gate`: wake decision,
   cleanup results, LVGL resume result and suppression state.
5. `measurement_resume`, `sleep_result`, `display_restore`,
   `post_resume_read`, `measurement_stable`: measurement restart, screen restore,
   first LVGL touch transaction after resume and eventual stable measurement.
   `post_resume_read` also reports I2C errors; it does not imply a successful read.

`touch event=input` reports raw contact detection versus delivered LVGL presses,
coordinates and the wake gate. Ordinary input transitions are rate limited to at most
four lines/second, so rapid press/release edges can be omitted. Wake gate changes
are always logged. `i2c_error` and
`i2c_recovered` mark communication transitions. The 30-second awake health logs
report touch poll/error/invalid-packet counters, age of the last poll/success,
power state, idle time, inhibition, measurement readiness and logger drops.
A missing `post_resume_read` after successful `lvgl_resume` warrants checking
whether the LVGL task resumes polling. A persistent `raw_down=1 delivered=0
gate=1` suggests the wake gesture has not received a valid zero-contact release.
These are diagnostic clues, not proof of the underlying hardware/software fault.

`battery event=sample` records calibrated millivolts, validity, estimated percent
and ADC/conversion result. It emits on first sample, error/validity changes,
the first sample after a sleep attempt that stopped LVGL,
a change of at least 30 mV from the last logged sample, or every 60 seconds
while awake. Sampling remains every five seconds and restarts after sleep.
`source=unknown charging=unknown` is intentional: this board's voltage-only ADC
cannot identify charger state or USB power. No periodic logs or battery samples
are produced while the CPU sleeps, and no timer wake has been added.

Numeric fields:

| Field | Values |
| --- | --- |
| `state`, `from`, `to` | 0 active, 1 dimmed, 2 screen off |
| `preparation` | 0 ready, 1 touch pending, 2 pending IRQ, 3 I/O error |
| `reason` in `wake_classified` | 0 touch, 1 shared non-touch, 2 ambiguous, 3 I/O error |
| `wake_cause`, `reset_reason` | ESP-IDF enum values; wake cause -1 means no successful sleep |
| `estimated_pct` | -1 means unavailable |

`t_us` is the producer's ESP timer timestamp in microseconds; the ESP log prefix
reflects when the queued line is printed. Pending lines may flush only after
wake, so the last printed line alone cannot prove where execution stopped.
Formatting/queue insertion occurs in the application/LVGL path; serial output
runs in the logger task. `log_dropped` is cumulative and `queue_hwm` is the queue
high-water mark (capacity 32). Logging can still affect timing. Diagnostic logs
add no TCA9554 input reads: those could acknowledge and clear a wake interrupt.


### Invalid touch response / false screen restoration fix

A capture showed 20 cancelled sleep attempts (`slept=0`), each classified as a
user touch, with `raw_contact_byte=165` (0xA5). All UI touch polls were invalid.
This did not demonstrate HX711 noise waking a sleeping CPU.

The transport now sends the command with STOP and performs a separate I2C read,
matching the vendor driver. The request and receive sizes both equal 14 bytes
(two six-byte contact records plus the two-byte header), instead of requesting
14 and reading 32. The vendor source documents `max_contacts * 6 + 2` (8 bytes
for one contact, 14 for two) and separate write/read transactions:
[Waveshare V2 driver, pinned reference](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/blob/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/Arduino/examples/09_LVGL_V8_Test/src/axs15231b/esp_lcd_axs15231b.c).
Byte offsets and coordinate orientation have not been guessed or shifted.
Whether the transport mismatch caused the observed 0xA5 requires device testing.

UI reads and sleep probes share a decoder: zero contacts is release, one/two
in-range contacts is pressed, and invalid count/coordinates/length is invalid.
Both contacts are checked. Multi-touch prevents sleep but is not delivered to
single-touch widgets. Invalid packets cannot clear the wake gesture gate or be
reported as a pre-sleep user touch; I2C success alone is not valid input.

Before HX711 suspension, `sleep_precheck` reads touch without acknowledging the
expander. An invalid response or I2C error produces `sleep_deferred`, retains the
dark screen and measurement state, and retries after 30 seconds. A valid touch
restarts the idle timer, restores the screen on the next policy update, and
consumes that gesture through release. Neither precheck rejection suspends or
resets acquisition. Final touch/IRQ checks still run after acquisition suspension
to handle a touch arriving after the precheck. If acquisition was interrupted,
measurement reset/warm-up is retained, including on a final preparation failure.
The existing conservative screen-restoration fallback after an actual CPU wake
with ambiguous/error classification is unchanged.

`touch event=packet` includes decoder state (0 release, 1 press, 2 invalid),
reason, raw contact byte, validated contact count and the full response in hex.
It logs state/contact changes and persistent invalid responses at most once per
second, plus a 30-second heartbeat. `post_resume_read` labels the unvalidated
byte `raw_contact_byte`; `gate_age_us` in touch health exposes prolonged
suppression. Capture idle, press, release and repeated sleep/wake on the device.
Native tests cover the repeated 0xA5 preparation-error/backoff sequence,
release/press/multi-touch, second-contact bounds, and matching request length.


### Sleep blocked by all-A5 idle responses and stuck wake gate

Later device captures contain valid contact packets between all-A5 responses.
The old decoder classified every all-A5 response as invalid. This blocked all
sleep prechecks, while the wake gate never received a release and suppressed
subsequent valid presses for more than 114 seconds. The weight acquisition path
was not causing these screen restorations.

The decoder now distinguishes the **exact full-length all-A5 pattern** as
`no_data` (state 3). This is a board-observed pattern, not a datasheet-confirmed
release code. No arbitrary invalid count, mixed A5 packet, coordinate error,
short packet or failed transaction qualifies as release.

UI and sleep use separate qualification state. Both require at least three
successful all-A5 reads spanning 300 ms; explicit zero-contact releases immediately.
UI qualification ignores GPIO8 (a shared wake interrupt, not a contact flag) and
allows up to one second between samples to tolerate display refresh stalls.
Sleep qualification still requires GPIO8 high at every sample and gaps no larger
than 100 ms. Thus UI recovery cannot make an unsafe sleep precheck pass.
Contacts, malformed responses, transaction errors, backwards time and stale
samples reset the respective window. Both windows reset after a sleep attempt;
elapsed sleep cannot release the gate. No additional expander reads acknowledge IRQs.

UI-qualified idle clears wake suppression; only sleep-qualified idle permits sleep preparation. The
existing physical expander input, touch rechecks and final GPIO checks remain
required before CPU sleep. Unqualified/invalid responses retain the 30-second
retry, screen-off state and uninterrupted acquisition. A tap within the 300 ms
release qualification period remains suppressed; lifting for at least 300 ms
before the next tap is the current expected behavior on the all-A5 path.

Packet logs distinguish raw state from qualified state; look for
`raw_state=3 state=0 reason=idle_a5_qualified`, then `gate=0`, and on the next tap
`delivered=1`. Packet logs include `source`, `gpio8`, `ui_state` and
`sleep_state` to distinguish release from sleep eligibility. For sleep, require `sleep_return result=ESP_OK` and
`sleep_result slept=1`; screen-off or a successful precheck alone is not proof.

**Hardware validation remains required:** hold a finger still for several
seconds, lift and tap again, drag, repeat screen-off/wake cycles, and verify
actual sleep/wake on battery and USB. GPIO8 is shared and its high level alone
does not prove no finger is present. A controller that emits all-A5 for over
300 ms during a held contact could cause an inferred release; this heuristic
must be validated on the target board. Conversely, a stuck-low shared IRQ
prevents sleep qualification, but no longer blocks UI release. The default verified-wake configuration gate
remains unchanged. Native regressions exercise qualification timing, brief gaps,
contact resets, bus errors, malformed/mixed packets, low IRQ, stale samples,
post-sleep reset, second-tap delivery, and the final sleep IRQ guard.



### Rev1.1 LCD sleep and optional wake-only button

`Smart scale power` now exposes `SMART_SCALE_LCD_SLEEP` (default enabled when
verified touch wake is enabled) and `SMART_SCALE_WAKE_BUTTON` (default disabled).
The existing `SMART_SCALE_TOUCH_WAKE_VERIFIED` opt-in still gates CPU light sleep.
The common 30-second dim / 60-second sleep policy above also applies while
charging. Peripheral rail shutdown is unchanged; weight activity is described below.

LCD sleep runs only during a real CPU sleep attempt, after HX711 suspension,
LVGL stop, completed DMA and initial wake preparation. With the backlight gated
off, QSPI sends Display Off (28h), Sleep In (10h), then waits 150 ms. The final
GPIO check remains after these operations. On every exit, including late IRQ,
failed commands and rejected CPU sleep, restore sends Sleep Out (11h), waits
150 ms, then Display On (29h). The next visible transition redraws the full frame
before enabling the backlight. Boot Sleep Out also waits 150 ms.

Command delivery errors mark the panel uncertain until a complete restore
succeeds. Restore errors keep the backlight and refresh timer off; active-state
transitions retry restoration. Logs include `event=lcd_command`, `cmd`, and
`result`. This does not reset the shared LCD/touch IC or put touch into Sleep.
The LCD/touch supplies, TCA9554 and SYS_EN remain enabled.

References: [AXS15231B datasheet, sections 1.5.2, 1.5.4, 3.2.2 and 6.1](https://files.waveshare.com/wiki/common/AXS15231B_Datasheet_V0.5.pdf)
and [Waveshare V2 QSPI driver](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/blob/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/Arduino/examples/09_LVGL_V8_Test/src/axs15231b/esp_lcd_axs15231b.c).
The 150 ms delay conservatively exceeds the documented 120 ms interval.

**Hardware validation required before accepting LCD sleep:** verify TP_INT still
reaches GPIO8 after 10h on this specific module firmware. Repeat touch wake,
held finger, late touch during preparation and screen restoration on USB and
battery. Compare battery-side backlight-only versus LCD-sleep current; no current
reduction figure is claimed. If touch wake fails, disable SMART_SCALE_LCD_SLEEP
and retain backlight-only behavior. Until validated, keep this PR draft.

For a future external normally-open button, wire a free GPIO38-41 to GND through
the button and enable SMART_SCALE_WAKE_BUTTON plus SMART_SCALE_WAKE_BUTTON_GPIO
(default 38). Do not enable with an unconnected button. The internal pull-up is
configured only when enabled. Remove SD and avoid external JTAG on shared pins;
compile-time checks reject HX711 conflicts. This button only wakes/extends idle;
it never tares or resets the session. Holding it blocks sleep and release allows
the existing idle timeout. Existing BOOT/reset behavior is unchanged.

ESP32-S3 light sleep has no per-pin status API equivalent to the deep-sleep GPIO
wake status. When the optional button is armed, any GPIO wake is conservatively
accepted as user activity so a short released press is not discarded. Thus a
shared IMU/RTC IRQ can also restore the screen in that optional configuration.
With the button disabled, existing shared-IRQ classification is unchanged.

Validation: native tests inject failure at each LCD command, exercise restore
retry/idempotence and repeated aborted/successful sleep cycles, verify QSPI
encoding and allowed/conflicting button pins. Firmware CI builds default,
expander + LCD sleep, expander + button, and backlight-only configurations.


### Weight activity and drift rejection

Idle-only `WeightActivityDetector` leaves scale filtering, tare, dose recognition
and brewing unchanged. `WeightActivitySettings` is injectable into `PowerPolicy`;
its initial tuning defaults are an 8 g step, a 500 ms hold with at most 1 g span,
a 500 ms baseline time constant, 1000 ms initial quiet plateau and 500 ms maximum
sample gap. These are hardware-tuning starting points, not a mobile settings API.
Invalid settings fall back to defaults.

The baseline tracks slow drift while small changes do not reset idle. Large
changes freeze that baseline until a same-direction plateau confirms addition
or removal. Spikes, reversals and unsettled candidates do not count. The 8 g
threshold exceeds the full 6 g span of +/-3 g vibration. A small bean addition
below threshold intentionally does not extend idle; touch/buttons still do,
and brewing/editor inhibition still keeps the scale awake.

Only fresh scale sample timestamps advance confirmation; repeated UI readings
cannot manufacture persistence. Invalid readings, acquisition gaps and every
resume (including cancelled sleep) discard pending candidates and re-establish
a quiet baseline without activity. Touch wake independently restarts idle.
HX711 is powered down during CPU sleep, so adding weight cannot wake CPU sleep.

Native regression traces cover +/-0.1/1/3 g vibration at several periods,
+/-0.1/1 g/s drift, cup addition/removal, exact threshold and hold boundaries,
small doses, spikes, reversals, unsettled loads, stale/duplicate samples,
invalid readings, resume offsets and custom tuning. Replay real device logs to
validate thresholds, especially slow placement and vibration above 3 g; weight
alone cannot distinguish a persistent mechanical shift from a user load.

### Rev1.1 peripheral sleep transaction

`SMART_SCALE_PERIPHERAL_SLEEP` defaults on only when verified touch wake is
selected. Disable it for the previous LCD/HX711-only path. This option is specific
to the populated Rev1.1 QMI8658 (0x6B, WHO_AM_I=0x05) and PCF85063 (0x51).
Missing devices, wrong IMU identity, I2C errors or mismatched readback reject the
sleep attempt. No default register values are fabricated on communication errors.

| Device / rail | Sleep behavior | Wake / abort behavior |
| --- | --- | --- |
| LCD | Existing Display Off/Sleep In, with DMA drained and LVGL stopped | Existing Sleep Out/Display On and full redraw before illumination |
| Backlight | EXIO1 low; explicitly stop LEDC output at inactive HIGH | Duty/update restarts PWM behind EXIO1 before enabling backlight |
| HX711 | Existing acquisition acknowledgement after PD_SCK high for 80 us | Existing power-up, queue/filter reset and fresh stable samples; no weight wake |
| QMI8658 | Save CTRL7/CTRL1; disable accel/gyro, disable INT1/INT2 output, set power-down | Restore CTRL1 before CTRL7; verify readback |
| PCF85063 | Save control registers; mask correction/alarm/periodic/timer IRQs, disable CLKOUT | Restore controls without clearing AF/TF; time/date/countdown remain running |
| Battery ADC | Synchronous conversions suppressed during peripheral quiet interval | Next sample uses fresh ADC conversions |
| Touch / TCA9554 | Power retained; no touch sleep or expander reset | Existing wake classification and first-gesture suppression |
| SYS_EN / shared 3V3 | Remain on; no rail removal | No cold initialization required |
| ES8311 / NS4150B audio | Not initialized by this application; no I2S clocks generated; **no new codec/amp shutdown command** | Unchanged; reset/idle analog current is not yet verified |
| SD | No mounted card or active SD host; **no card rail switch implemented** | Remove card for low-current validation, especially with SD-pin HX711/button wiring |
| Radio | No Wi-Fi/BLE stack initialized in current application | A future radio service must add its own suspend acknowledgement |

The transaction runs after HX711 suspension and LVGL stop, **before** the shared
expander input baseline is acknowledged. All five register snapshots must succeed
before the first write. Each attempted write is marked for rollback before I2C
submission, since a reported bus failure does not prove that the IC ignored it.
Every write is read back. Wake, late touch, LCD-command failure and rejected CPU
sleep all restore the register snapshot in reverse order. A failed restore retains
the remaining snapshot, blocks another sleep, and is retried on the active display
transition. It does not discard the snapshot or overwrite it with a new attempt.

RTC AF/TF bits use write-zero-to-clear semantics: both are written as 1 during
prepare and restore, and excluded from readback equality. Events arising after
the snapshot therefore remain pending. Restoring enabled RTC IRQs can assert the
shared line again; it is deliberately not acknowledged solely for diagnostics.
This firmware has no RTC alarm service or IMU consumer. Future scheduled RTC wake,
IMU wake, or concurrent sensor/audio services must negotiate sleep ownership;
they must not concurrently change the saved registers or expect these IRQs to
wake the CPU while this option is enabled.

Backlight diagnostic read failure no longer changes a successful screen restore
into failure or leaves the display refresh timer paused.

Implementation sources:
- [Waveshare Rev1.1 schematic](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/blob/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/schematic/ESP32-S3-Touch-LCD-3.49%20V2.pdf).
- [Vendor QMI8658 driver](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/blob/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/ESP-IDF/03_I2C_QMI8658/components/SensorLib/src/SensorQMI8658.hpp), `powerDown`, `enableINT`, and CTRL register definitions.
- [NXP PCF85063A datasheet](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf), Control_1/Control_2, interrupt scheme, CLKOUT and Timer_mode.

Native `peripheral_sleep_test` injects every snapshot/write/readback failure and
every restore/write/readback failure, including writes accepted by the device
but reported failed by the bus. It checks pending RTC flags, retained timekeeping,
wrong identity, readback mismatch, idempotent rollback and 100 transaction cycles.
These tests cover register policy; they do not simulate ESP-IDF, DMA or circuitry.

#### Measurements still required

Record board revision, firmware SHA/config, battery voltage, inserted SD card and
USB state alongside each measurement. Measure at the battery supply, not only the
USB cable. No measured current or saving is available from software tests.

| Configuration | Battery-side current | Touch wake / 100 cycles | Notes |
| --- | --- | --- | --- |
| Active | Not measured | Not applicable | Fixed brightness |
| Dim | Not measured | Pending | 30% brightness |
| Screen off, CPU awake | Not measured | Pending | Polling continues |
| Light sleep, peripheral option off | Not measured | Pending | Baseline |
| Light sleep, peripheral option on | Not measured | Pending | Check GPIO8 and IMU/RTC readbacks |

Verify on battery and USB: held touch, touch during preparation, shared IRQ,
I2C disconnect/reconnect, LCD failure, rejected sleep, stable HX711 recovery and
preserved tare/session. Separately measure codec/amp and inserted-card residual
current before claiming that **all** peripherals are powered off or minimized.
