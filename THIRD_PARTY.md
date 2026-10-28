# Third-party provenance

`components/esp_lcd_st7796` and `components/esp_lcd_touch_ft6336` are copied without source changes from Waveshare's `examples/esp-idf/02_lvgl_example/components/` at commit `d9ff7837148b47b33edadc4b87775c341cf35bbe` of [ESP32-S3-Touch-LCD-3.5](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5). Upstream copyright notices are retained. The upstream Apache-2.0 license is in `components/LICENSE.waveshare`.

LVGL 8.4.0 (MIT) and the Espressif components are fetched by the ESP-IDF Component Manager; exact versions/hashes are recorded in `dependencies.lock`, with their licenses in the downloaded component directories. Native rendering fetches LVGL at commit `4495f428630cc1741bd8bfd977f080e8460e8e8d`.

Application organization is inspired by the requested [farm360-ai/esp32_plc/serial_mb_slave](https://github.com/farm360-ai/esp32_plc/tree/main/serial_mb_slave) pattern. Application source was written for this project; no PLC business logic, credentials, or hardware pins were copied.

`main/weight_font_72.c` is a 72 px, 4-bpp subset of DejaVu Sans (digits, decimal point, minus, question mark, space and g), generated with `lv_font_conv`. Its Bitstream Vera license and public-domain DejaVu changes notice are in `main/LICENSE.weight-font`. The bitmap is stored in flash and drawn without runtime scaling.

`main/equipment_emoji_22.c` contains 20 monochrome Unicode emoji/symbol glyphs from DejaVu Sans, generated at 22 px and 4 bpp with `lv_font_conv`. It uses the same license in `main/LICENSE.weight-font`. The glyph subset renders on both device and host without an OS emoji font.
