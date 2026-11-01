# Brew guide and appearance

The 640×172 screen uses an aligned mode/status/battery header and three full-height
setup cards: a 304×125 weight card plus equal 150×125 Container and Coffee cards.
Weight and units are centered and baseline-aligned. Empty cards show Select and
Set dose; registered cards show saved mass/dose and retain coffee-save feedback.

Brewing places the graph on the left, two equal metric cards on the right, and a
282×45 Finish button spanning both metrics. The timer remains in the header.
Actual mass is a solid line; targets are dashed with checkpoints, and a vertical
cursor marks elapsed time. Axes adapt to the guide and measured mass, with
bounded rendering of up to 120 historical points. Negative readings remain
visible numerically but are clipped to the graph's zero boundary. Sensor errors
hide the actual trace and show a visible error banner without blocking Finish.
The line styles distinguish Actual and Target even in Porcelain Gray. There is no tare or
Reset button during extraction. The controller also rejects tare outside ready.
The 480×320 layout shares the themes, weight-card tare and target guide.

Setup has no Calibrate, Reset or Start brew buttons and no extra placement-hint
label. The weight card contains **Tap to calibrate** when calibration data is
missing, opening the existing calibration dialog even before the sensor is ready.
After successful calibration it shows **Tap to tare**; tare is only enabled with
a live, calibrated, ready, stable sensor. A sensor fault does not erase calibration
or change the card into a calibration action. Cancelling calibration keeps the
calibration prompt until valid calibration data is available. With registered
container and dose, **Pour to start** indicates that sustained flow is the only
live UI start path. Reset remains available through the existing physical action;
no new physical-button hardware is introduced here.
An unavailable reading stays `?`; it is never represented as zero. No approximation
prefix (`~`) is displayed. During brewing the mass is labeled **Water added** only
for a pour-over set (dripper plus receiving vessel on the scale); otherwise it is
beverage mass. Live flow remains g/s. The simulator uses the same g/s flow unit.

## Theme integration

| Stable wire name | ID | Display name |
| --- | --- | --- |
| `sage_green` | 0 | Sage Green (default) |
| `espresso_amber` | 1 | Espresso Amber |
| `porcelain_gray` | 2 | Porcelain Gray |

There is no on-scale theme selector or settings page. The future mobile transport
should parse its theme string with `parse_theme`, then dispatch `View::set_theme`
to the UI task under the LVGL lock. `View::theme()` supplies the current value.
A possible future application payload is `{"theme":"espresso_amber"}`; this PR
does not add a BLE service, HTTP endpoint, pairing, or mobile application.

`set_theme` rejects unsupported IDs and returns false on storage failure. It
commits the single-byte ID to NVS namespace `appearance`, key `theme`, before
changing the displayed theme. Duplicate updates skip writes. A successful call
schedules a redraw on the UI loop; acknowledge success to the app only after the
call succeeds. The redraw preserves the running controller session, timer,
samples, selections and active dialog. Like the rest of the UI, these calls are
not safe from an arbitrary BLE callback thread.

Boot loads NVS after the existing storage initialization. Missing, corrupt,
unknown, or unreadable values fall back to Sage Green without erasing other NVS
data. Native simulator theme changes are volatile. Main screens and dialogs use
the same palette; battery state/history colors remain semantic status colors.

## Time windows

The amber/contrasting **Guide** line is a cumulative mass target; **Actual** is
measured mass. `Now` is the interpolated guide target at elapsed time, while
`Total` is the final mass target. Axis labels show window boundaries in seconds;
closely spaced labels are suppressed to prevent overlap, but all guide points
remain plotted. The chart extends for overruns and holds the final target.

Defaults are illustrative guidance, not a user-authored recipe:

| Mode | Elapsed seconds | Fraction of final mass |
| --- | --- | --- |
| Espresso | 0, 30 | 0%, 100% |
| Pour-over | 0, 30, 60, 90, 120, 180 | 0%, 20%, 20%, 60%, 60%, 100% |

Pours interpolate linearly; equal fractions form hold/bloom windows. Existing
final-target calculations are unchanged. `Controller::set_brew_guide(BrewGuide)`
accepts 2–12 points while ready: finite, strictly increasing time (0–1800 seconds),
nondecreasing fractions (0–1), beginning at (0,0) and ending at fraction 1.
Invalid data and changes during brewing/results are rejected. A mode change or
session reset clears the custom guide; completed RAM records retain it. Guide
persistence and mobile recipe transfer are future work.

## Verification

- `make test`: state, guide interpolation/validation and NVS fallback/write-failure tests.
- `make preview-wide`: wide interaction, calibration, coffee and error paths.
- `make preview`: legacy layout interactions and live sensor scenarios.
- `make preview-themes`: setup, dose dialog, brewing and result for all themes;
  invalid theme rejection, session preservation across theme changes, no brew tare,
  and Finish interaction.
- `make simulator` uses the configured device resolution, preserving the shared
  resolution and buffer checks from PR #17. Theme previews use that resolution too.
  Wide-screen demo controls live outside the device display: Space runs the demo,
  S opens scenarios, and R resets. These shortcuts are shown in the SDL window title.
  The legacy 480×320 test screen retains its original simulator controls.

Hardware checks still needed: physical touch targets, color/contrast on the LCD,
NVS persistence across power cycles, and continued sample timing during a theme
write. The desktop NVS test uses a fake backend and cannot establish flash timing.
