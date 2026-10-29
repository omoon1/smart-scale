# 3.49-inch wide-panel UX

The Waveshare ESP32-S3-Touch-LCD-3.49 has a native **172 × 640** panel. This UI uses it horizontally as **640 × 172 logical pixels**. The view selects this layout from LVGL's registered resolution; the existing 480 × 320 UI remains the default on the current board.

![Actual LVGL renders](screens/wide/overview.png)

## Layout

- Home: large weight and baseline-aligned unit at left; workflow guidance above side-by-side container and coffee cards at right. Start, Tare, Calibrate and Reset remain directly accessible. Coffee still pulses for measured-dose registration.
- Brewing/results: chart at left, weight in the middle, elapsed time and flow/ratio at right. Full-width status line keeps sensor faults and recovery countdowns visible. Finish, Reset and Next remain accessible. The wide demo omits the optional flow-pause control to preserve space for the graph; real brewing has no pause action.
- Editors: full-screen sheets replace tall centered modals. Coffee has presets and press-and-hold adjustment controls. Calibration separates live readings from the zero/mass controls.
- Equipment: a vertically scrollable two-row viewport and persistent Register button; deletion still requires confirmation. Registration retains type, stable weight and icon steps, with all 20 icons in two rows. The type dropdown is height-limited and scrollable.
- Wide action buttons are at least 44 px high; text and values retain native font sizes rather than scaling a 320 px layout down.

All layouts share the same controller, bindings, timers, validation and value-refresh path. No calibration, equipment, persistence or brew-state schema changes are introduced. Weight units and graph point positions use widget geometry instead of legacy screen coordinates.

## Reproduce and validate

```sh
make test
make preview                   # existing 480x320 demo, live and coffee suites
make preview-wide              # 640x172 demo and live interaction suites
python scripts/wide-screens.py  # export selected PNGs; requires Pillow
make simulator-wide            # interactive 1280x344 window; requires SDL2
```

`make preview-wide` produces 23 actual LVGL framebuffer captures in `build-host/screens/wide-*.ppm`. The interaction suites exercise dose presets/hold/release, all icon hit areas, registration stability gating, removal cancel/confirm, calibration with synthetic HX711 counts, measured coffee registration, manual editing, automatic brew start, sensor-fault countdown and return-home recovery. Existing state and legacy UI suites are also checked.

## Hardware boundary

This is a **UX adaptation, not a completed new-board firmware port**. `make build` still targets the existing ST7796/FT6336 board and its pin assignments. Changing `config::screen_width/height` alone is not sufficient and is intentionally avoided.

The new board requires an AXS15231B QSPI display and I2C touch BSP. That BSP must register 640 × 172 logical dimensions and apply matching display/touch rotation. Confirm the PCB revision before configuring pins: Waveshare documents V1/V2 swaps for LCD reset/TE and backlight/expander interrupt. Recheck HX711 pin availability on that board. Hardware bring-up must verify touch at all four corners, backlight startup, refresh behavior and HX711 operation; those checks cannot be established by the host renderer.

Sources: [product specifications](https://www.waveshare.com/esp32-s3-touch-lcd-3.49.htm), [revision compatibility and reference examples](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.49/Resources-And-Documents).
