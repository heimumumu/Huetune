idf_component_register(SRCS "src/XPowersLibInterface.cpp" INCLUDE_DIRS "src" REQUIRES esp_driver_i2c)
target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_XPOWERS_ESP_IDF_NEW_API=1)
