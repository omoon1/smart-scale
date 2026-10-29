# Hardware bring-up — Rev1.1

Target: **Waveshare ESP32-S3-Touch-LCD-3.49 V2 (PCB Rev1.1)**, 16 MB flash and 8 MB OPI PSRAM. The default firmware now targets this board. V1, the original 3.5-inch ST7796 board and the 3.5B are not supported by this BSP.

| Signal | Rev1.1 connection |
| --- | --- |
| LCD controller | AXS15231B, RGB565, native 172×640 |
| LCD interface | SPI3 QSPI, mode 3, 40 MHz, 32-bit command / 8-bit parameters |
| LCD CS / clock | GPIO9 / GPIO10 |
| LCD D0 / D1 / D2 / D3 | GPIO11 / GPIO12 / GPIO13 / GPIO14 |
| LCD reset | TCA9554 EXIO5, high 30 ms → low 250 ms → high 30 ms |
| Backlight enable | TCA9554 EXIO1 |
| Backlight PWM | GPIO42, 50 kHz, 8-bit, duty 204/255 |
| System power hold | TCA9554 EXIO6, high |
| Expander I²C bus | I2C0, SDA GPIO47 / SCL GPIO48; TCA9554 address 0x20 |
| Touch I²C bus | I2C1, SDA GPIO17 / SCL GPIO18; address 0x3B, 300 kHz |
| HX711 DOUT / SCK | Header IO44 / IO43 |
| Application Reset | BOOT / GPIO0, active-low, 200 ms debounce |

There is **no AXP2101 access at 0x34**. This board's charger/power circuitry does not use the previous board's PMU register setup. EXIO6 holds system power on; audio, RTC, IMU and SD are not initialized. PWR-button shutdown handling is not implemented; USB-powered operation is the initial bring-up target. GPIO43/44 are also UART0 pins: keep console output on USB Serial/JTAG and do not attach a UART device to these pins while using HX711.

## Display and touch implementation

The vendor QSPI driver starts a frame with RAMWR and continues subsequent strips with RAMWRC; it does not set a row window for arbitrary dirty rectangles. The BSP therefore registers a **full-refresh 640×172 LVGL display** and rotates into the native 172×640 raster in ten 64-row strips. Each strip is copied into 22,016 bytes of internal DMA memory; the single 220,160-byte LVGL framebuffer lives in PSRAM. It waits for completion before reusing the strip and before declaring the frame complete. LVGL colors remain byte-swapped RGB565; panel color order is RGB, not the old BGR setting.

The existing `esp_lvgl_port` supplies the LVGL task, tick and mutex. The BSP registers its own flush and input callbacks, avoiding the generic partial-area rotation path. GPIO42 and EXIO1 keep the backlight off until `show()` has synchronously completed the first frame. QSPI failure or a one-second strip timeout aborts rather than reusing a potentially active DMA buffer.

Touch uses the vendor's 11-byte query and 32-byte response on the separate touch bus. Landscape mapping reverses both raw axes with zero-based bounds (639−x, 171−y). Failed reads, out-of-range points, release packets and multi-touch all report release; read errors are logged on failure/recovery transitions. A touch disconnect must not keep a dose button held or reset the device.

## HX711 migration

**Move DOUT from GPIO17 to IO44 and SCK from GPIO18 to IO43.** GPIO17/18 are occupied by the new board's touch bus. Use header labels, not physical pin numbers. Power off before rewiring, then connect module VCC to 3V3 and GND to GND. See [HX711 bring-up](hx711.md) for voltage requirements and calibration.

## Build, flash and check

```sh
make build
make probe
make flash /dev/ttyACM0   # substitute your actual port; also opens monitor
```

The component manager updates dependencies automatically. A normal rebuild is sufficient; do not erase NVS as a routine migration step. Calibrate the connected cell before trusting measurements.

1. Confirm Rev1.1 on the PCB and disconnect the old HX711 wiring before powering on.
2. Confirm the log identifies `Waveshare 3.49 V2/Rev1.1: AXS15231B QSPI, 640x172 landscape` and there is no PMU read/abort at 0x34.
3. Cold-start several times: verify the initial frame, backlight enable, color order and absence of stale/noisy panel content.
4. Tap all four corners, cards, dose adjustments and Close. Verify no axis reversal or edge offset. Exercise release/slide-off during a long press.
5. Register equipment, scroll the full list, select all icon positions, calibrate, tare and start/finish a brew. Verify the graph remains coherent across its full width.
6. With the HX711 connected to IO44/43, verify sample cadence and stable weight while touching/redrawing the screen. Test HX711 disconnect separately from touch communication loss.
7. If battery operation is needed, verify power hold and the intended shutdown behavior physically; this firmware does not provide a PWR-button shutdown action.

No physical board is connected to the development environment. Compilation and host tests do not establish electrical timing, touch orientation, cold-start backlight behavior, battery behavior or weighing accuracy.

## Sources

- [Board documentation and revision differences](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.49)
- [V2 LVGL8 reference, pinned revision](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/tree/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/ESP-IDF/09_LVGL_V8_Test)
- [V2 battery/system-enable reference](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/tree/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/ESP-IDF/07_BATT_PWR_Test)
- [Rev1.1 schematic](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/blob/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/schematic/ESP32-S3-Touch-LCD-3.49%20V2.pdf)
