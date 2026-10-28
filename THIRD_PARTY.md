# Third-party provenance

`components/esp_lcd_st7796` and `components/esp_lcd_touch_ft6336` are copied without source changes from Waveshare's `examples/esp-idf/02_lvgl_example/components/` at commit `d9ff7837148b47b33edadc4b87775c341cf35bbe` of [ESP32-S3-Touch-LCD-3.5](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5). Upstream copyright notices are retained. The upstream Apache-2.0 license is in `components/LICENSE.waveshare`.

LVGL 8.4.0 (MIT) and the Espressif components are fetched by the ESP-IDF Component Manager; exact versions/hashes are recorded in `dependencies.lock`, with their licenses in the downloaded component directories. Native rendering fetches LVGL at commit `4495f428630cc1741bd8bfd977f080e8460e8e8d`.

Application organization is inspired by the requested [farm360-ai/esp32_plc/serial_mb_slave](https://github.com/farm360-ai/esp32_plc/tree/main/serial_mb_slave) pattern. Application source was written for this project; no PLC business logic, credentials, or hardware pins were copied.
