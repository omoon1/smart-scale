idf_component_register(SRCS "main.cxx" "device.cxx" "session.cxx" "ui.cxx"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer esp_lcd_st7796 esp_lcd_touch_ft6336
             esp_lvgl_port esp_io_expander_tca9554 lvgl)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS YES)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
