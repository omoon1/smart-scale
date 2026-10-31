# Automatic brew detection

Prepared against PR #18 revision ee07a8fb24f37db64ea182219d7bc93ca6d75d18.

A registered receiving vessel, a saved valid coffee dose, healthy calibrated
sensor and stable arming reading are still required. Setup dialogs pause
arming; battery history does not. There is no new UI control or storage format.

The detector consumes sensor timestamps and averages 200 ms buckets. A fixed
31-point history covers up to six seconds without heap allocation. It checks
suffix windows so old disturbances do not indefinitely block a new pour:

- Fast path: at least 0.60 g net gain, four rising intervals, rising evidence
  spanning at least one second, rising edges, and no 400 ms gap between rises.
- Slow/intermittent path: at least three seconds, 0.40 g gain and 0.08 g/s
  average gain, with four distributed rises spanning two thirds of the window.
  The most recent rise must be within one bucket. Positive steps use 0.008 g
  rather than the fast path's 0.025 g threshold.
- Both reject any backward step beyond 0.20 g and excessive accumulated
  backward movement (over the larger of 0.08 g or 25% of net gain).
- Positive bucket gains up to 12 g can qualify only with repeated rises.
  A single placement cannot start a brew. Larger gains or a drop over 3 g
  reset detection and require stable rearming.
- Nonfinite data, backwards timestamps, and gaps over the sensor timeout reset
  evidence. Duplicate timestamps cannot age the confirmation window.

Existing controller behavior preserves the vessel/water tare and all liquid
added during confirmation. Timer origin is the first qualifying rise in the
accepted window, not an exact physical first-drop timestamp. No change to
calibration, coffee dose validation, tare locking, or removal/error behavior.

## Validation

`make test` runs the expanded existing `auto_brew_test.cxx` suite. Added twelve
controller-level scenarios, each at 100 ms and 13 ms sampling and both sensor
polarities (48 combinations): slow drip, slow noisy flow, fast pour,
intermittent drops, pause/resume, sample jitter, drift, stationary noise,
single step, tap/return, small step, and step followed by flow.

Observed synthetic start latency: 0.1 g/s ~4.16–4.20 s; 30 g/s ~1.20–1.25 s;
0.16 g drops every 0.7 s ~3.00–3.12 s. The tested 0.03 g/s drift, sinusoidal
noise, isolated steps and brief tap did not start brewing. These are test
fixtures, not device accuracy guarantees or exhaustive flow bounds.

Also passed existing session (502 checks), scale, filter, brew error, brew guide,
auto-brew and coffee-registration suites. Address/undefined behavior sanitizers
passed for auto-brew; LeakSanitizer disabled because this sandbox cannot inspect
process threads. Full ESP-IDF build and physical-device validation not performed
for this change.

## Limits and device acceptance

Mass alone cannot distinguish liquid from a sustained hand force or several
small added solids producing the same trend. Flows below thresholds may remain
undetected; excessive abrupt pouring can require stable rearming. Pouring before
a stable baseline is available or while a blocking dialog is open is unsupported.

Before release, replay real HX711 traces for dry idle, vibration, slow espresso
drops, fast pour-over and interrupted pours. Verify trigger latency and retained
water mass, then tune thresholds using false-start and missed-start measurements.
