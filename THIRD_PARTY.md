# Third-party provenance

The Rev1.1 pin map, initialization sequence, full-frame rotation and touch protocol in `main/device.cxx` and `main/board349.hxx` are adapted from Waveshare's [ESP32-S3-Touch-LCD-3.49-V2](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2) ESP-IDF examples 07 and 09 at commit `1c157e6e8e68b89fd4dc400f46bf1724cb64a57e` (Apache-2.0, license in `main/LICENSE.waveshare`). Adaptations include bounded DMA waits, strip-wise rotation, touch bounds/release handling, PSRAM allocation, and integration with the existing LVGL task. The obsolete local ST7796/FT6336 components have been removed.

The AXS15231B driver is the pinned Espressif Component Manager dependency `espressif/esp_lcd_axs15231b` 1.0.1~1 (Apache-2.0).

LVGL 8.4.0 (MIT) and the Espressif components are fetched by the ESP-IDF Component Manager; exact versions/hashes are recorded in `dependencies.lock`, with their licenses in the downloaded component directories. Native rendering fetches LVGL at commit `4495f428630cc1741bd8bfd977f080e8460e8e8d`.

Application organization is inspired by the requested [farm360-ai/esp32_plc/serial_mb_slave](https://github.com/farm360-ai/esp32_plc/tree/main/serial_mb_slave) pattern. Application source was written for this project; no PLC business logic, credentials, or hardware pins were copied.

`main/weight_font_72.c` is a 72 px, 4-bpp subset of DejaVu Sans (digits, decimal point, minus, question mark, space and g), generated with `lv_font_conv`. Its Bitstream Vera license and public-domain DejaVu changes notice are in `main/LICENSE.weight-font`. The bitmap is stored in flash and drawn without runtime scaling.

`main/equipment_icons.c` contains original monochrome artwork generated from the vector geometry in `scripts/equipment_icons.py`. It does not use a third-party emoji font. CairoSVG and Pillow are development-only generation tools; generated 22 px and 32 px glyphs are compiled directly into firmware.
