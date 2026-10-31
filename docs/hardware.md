# Hardware bring-up

Target: Waveshare **ESP32-S3-Touch-LCD-3.5**, 16 MB flash and 8 MB OPI PSRAM. The **3.5B** is a different AXS15231B board and is not supported by this implementation. Confirm the board marking before flashing.

| Signal | Connection |
| --- | --- |
| ST7796 MOSI | GPIO1 |
| ST7796 SCLK | GPIO5, SPI2 at 40 MHz |
| ST7796 D/C | GPIO3 |
| LCD chip select | Not routed as a GPIO (`-1`), per vendor example |
| LCD reset | TCA9554 bit 1, low/high with 100 ms intervals |
| Backlight | GPIO6, LEDC 5 kHz, 10-bit, 80% |
| Shared I2C SDA/SCL | GPIO8 / GPIO7, 400 kHz devices |
| FT6336 touch | 0x38; native 320×480, rotated to landscape |
| TCA9554 | 0x20; touch IRQ on P2 / EXIO2 |
| V2 expander wake | EXIO_INT → GPIO4 (V1 schematic has no MCU connection) |
| AXP2101 | 0x34; DCDC1 forced-PWM bit enabled as in vendor fix |
| Application Reset input | BOOT / GPIO0; pull-up; active-low; 200 ms debounce |

Display and touch rotation follow the vendor 90-degree configuration. Display RGB565 uses BGR element ordering and byte-swapped LVGL colors. DMA transfer size is expressed in **bytes**, while the LVGL buffer size is in pixels. Two 480×20 buffers use 38,400 bytes of internal DMA memory; the controller and its record history are explicitly allocated in PSRAM. The dose editor uses discrete buttons rather than an animated wheel; large-coordinate support remains enabled for compatibility.

The UI uses the board's existing 3.3 V power rail. The adapter only changes the vendor-documented DCDC1 PWM bit; it does not initialize camera/audio/RTC rails or change battery charging policy. USB-powered display bring-up is the initial validation target. Battery-powered operation, wake/power policy, and any factory bootloader state still need validation on the physical device.

For 3.5 V2 touch wake, select the V2 board under `make menuconfig` → Smart scale power.
See [power configuration and validation](power-management.md#sleep-configuration-and-limitations).
GPIO4 is the shared expander interrupt, not a direct FT6336 interrupt.

## HX711 load cell

Use GPIO17 for DOUT, GPIO18 for SCK, 3V3 and GND. Keep the camera disconnected. See [HX711 bring-up](hx711.md) for module voltage requirements, calibration, and physical acceptance checks. Device firmware now uses live measurements; the scenario checks below apply to the desktop demo.

## Flash and check

1. Connect a USB data cable and find the serial port. Run `make flash-monitor PORT=...`.
2. If the ROM downloader does not respond, hold BOOT, tap hardware RESET, release BOOT, and flash again. Do not hold BOOT while restarting into the app.
3. Confirm the log identifies ST7796/FT6336 and the screen is 480×320 landscape.
4. Tap all four corners, object/dose cards, close buttons, and the dose editor. Verify touch/display alignment, color order, and readability.
5. Run each scenario, Finish, then Next. Press BOOT during extraction and confirm the remove-load screen. The physical RESET/EN switch is a reboot, not the application Reset.
6. Check all 20 registration icons and type-plus-icon names and scroll the equipment list after adding profiles. Confirm the screen remains responsive during a long extraction.

No board was attached during implementation. Compilation and desktop rendering do not validate panel timing, touch orientation, cold-start power sequencing, battery behavior, or scale accuracy.

## Sources

- [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5)
- [Waveshare ESP-IDF guide](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5/ESP-IDF)
- [Vendor example at pinned revision](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/tree/d9ff7837148b47b33edadc4b87775c341cf35bbe/examples/esp-idf/02_lvgl_example)
- [Vendor factory power implementation](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/d9ff7837148b47b33edadc4b87775c341cf35bbe/examples/factory/components/esp32_s3_touch_lcd_3_5/bsp_board.cpp)

The two vendor driver components retain their copyright headers and Apache-2.0 license. See [third-party provenance](../THIRD_PARTY.md).
