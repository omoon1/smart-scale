# Scale troubleshooting logs

> Current 3.49 V2 firmware disables `scale_trace` capture/dumps and periodic `hx711_diag` output. The diagnostic descriptions below are reference material; use [power and touch troubleshooting](power-management.md#diagnostic-logs) for current sleep/wake captures. Ordinary `scale_event` output remains enabled.

Capture serial output from power-on, keep the load untouched for at least two
minutes, and include the firmware configuration and the complete trace blocks.
Use the usual `make monitor /dev/ttyACM0` command (choose the actual port).

## Log records

- `scale_config`: application version (ESP-IDF's project/git version), IDF,
  HX711 pins, selected channel/gain, filter and stability settings, and capture
  limits. HX711 RATE is a hardware setting; the log reports observed Hz rather
  than claiming the configured sample rate. `startup` / `calibration_restored`
  records include the actual calibration factor and zero. This firmware uses
  the real HX711; the obsolete display initialization `dummy data` text is removed.
- `hx711_diag`: unconditional five-second health summary. Existing numeric
  fields remain, alongside named diagnostic failures, monotonic timestamps,
  accepted sample sequence, acquisition sequence, queue loss and high-water
  counters. `log_dropped` counts live messages rejected by the logging queue;
  `sample_dropped_estimate` estimates queued readings discarded during overflow
  recovery (the consumer can race the queue-depth observation). Sequence
  differences and overflow counts provide additional evidence. Counters are
  cumulative except the existing interval max-age and batch fields.
- `scale_event`: operational stability transitions, sensor invalidation,
  overload, rejected timestamps, tare/set-tare/clear-tare, zero capture,
  calibration restore/rejection and persistence success/failure. `reason` is the
  operational state/transition reason, while `failed` describes independent
  diagnostic checks. A `calibration_restored` event means the in-memory factor
  changed, not that NVS persistence has completed. Tare events include the new
  `tare_g`; compare with the prior event/trace to find the before value.
- `weight`: actual visible transitions, still rate-limited to one per second,
  now correlated with the accepted sequence and monotonic timestamp. This is
  the authoritative displayed text, including archived brew results. Trace
  `net_g` is the live scale measurement; it is not the UI's rounded/held result.

## Anomaly traces

A PSRAM ring retains up to five seconds before a trigger and five seconds after.
Triggers: a median-to-median jump of at least 1 g, a new diagnostic failure
(noise/drift/residual after the diagnostic warmup), operational stable exit,
sample gap over the existing sensor timeout, sensor invalidation, or overload.
These are evidence-capture triggers: a normal vessel placement or pour can
trigger them too. They do not identify a hardware fault or change scale behavior.

There is a 30-second cooldown from the trigger. Only one capture can be active
or draining. At nominal 10/80 SPS, the 1024-row limit holds the complete interval;
unexpectedly high rates truncate oldest rows and increment `trace_truncated`.
A disconnect still completes a capture after five seconds, with fewer post-event
samples. A frozen capture is retained until the asynchronous logger accepts it.
New samples during draining are excluded from that capture and counted in
`trace_missed_samples`. Live health and state events continue.

`scale_trace event=trace_begin` identifies the trace ID, trigger time/reason,
and cumulative truncation. Each `scale_event event=trace_sample trace=N` contains:

| Field | Meaning |
| --- | --- |
| `seq` | Accepted Scale sample sequence, including median warmup samples |
| `sample_ms` | Monotonic timestamp taken immediately after completing the HX711 read |
| `now_ms` / `age_ms` | Snapshot processing time / elapsed time since acquisition |
| `dt_ms` | Difference from the preceding accepted acquisition timestamp; 0 for first sample |
| `raw` / `raw_g` | ADC counts / signed calibration conversion before median/filter |
| `median_g` | Three-sample median converted to grams |
| `filtered_g` / `net_g` | Adaptive-filter gross grams / gross minus tare |
| `tare_g`, `factor`, `zero` | Calibration and tare active at that snapshot |
| `stable`, `quiet`, `reason` | Operational latch, adaptive filter mode, operational reason |
| `op_sd`, `half_shift`, `anchor_delta` | Operational short-window SD, half-window mean shift, absolute recent median-to-anchor difference |
| `drift_guard` | Whether long-window drift currently blocks stable entry; not an exit criterion |
| `failed`, `sd`, `drift`, `p2p`, `residual` | Independent diagnostic outcomes/statistics |

With calibration, statistics use g or g/s; without calibration they use counts
or counts/s and gram-stage fields are `nan`. `raw=NA` means no sample has arrived.
An old raw value can remain visible after a fault; check `ready` and sample age.
Use `sample_ms`/`now_ms` inside the record, not the ESP log prefix: output is
asynchronous and trace samples are deliberately historical. `trace_end` marks
successful enqueue of the last row. If output ends before it, the trace is partial.

The logging task serializes output separately from acquisition and LVGL.
Producers use nonblocking queue sends; trace rows retry rather than disappear
under queue congestion. Live messages can be dropped and are counted. Startup
configuration, health, visible-weight records and events use this queue; existing
unrelated ESP-IDF/display/error logs keep their original routes.

## Reading the original drop/recovery symptom

1. Find the same sequence across raw, median and filtered stages. A raw drop
   points upstream of filtering; median rejects a single isolated raw spike.
2. If raw has recovered but filtered remains low, inspect the preceding trace
   rows and `quiet` mode before attributing the difference to a filter defect.
3. Check tare/calibration events and factor/zero to rule out a changed baseline.
4. Check sample timing, acquisition sequence gaps, overflow and diagnostic loss
   counters before interpreting missing history as quiet operation.
5. Compare startup and warmed-up recordings under the same mechanical/electrical
   conditions. Logs locate the affected stage; power, wiring and mechanical
   causes still require controlled hardware comparisons.

## Validation

`make test` includes native capture tests at 100 ms and 13 ms intervals,
pre/post duration, cooldown, frozen-buffer retention, overload-rate truncation,
fault completion without new samples, both calibration polarities, and
observed-versus-unobserved measurement equivalence. The existing ESP32-S3 CI job
builds the device integration. Serial throughput, PSRAM use and UI responsiveness
must also be checked on the real device with a complete capture.
