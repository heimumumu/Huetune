idf_component_register(SRCS "main.cpp" "board.cpp" "usb.cpp" "ui.cpp"
    INCLUDE_DIRS "." "../generated"
    REQUIRES driver esp_lcd esp_timer nvs_flash json esp_lvgl_port lvgl esp_tinyusb
             esp_codec_dev esp_io_expander_tca9554 esp_lcd_st7796 XPowersLib)
file(GLOB UI_FONTS "${CMAKE_CURRENT_LIST_DIR}/../generated/mc_font_*.c")
if(NOT UI_FONTS)
    message(FATAL_ERROR "Run tools/prepare-firmware.py to fetch drivers and generate fonts first")
endif()
target_sources(${COMPONENT_LIB} PRIVATE ${UI_FONTS})
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-parentheses -Wno-misleading-indentation)
