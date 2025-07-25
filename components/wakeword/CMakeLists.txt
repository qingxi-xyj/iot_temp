idf_component_register(
    SRCS "wakeword_service.c"
    INCLUDE_DIRS "."
    REQUIRES esp-sr esp_driver_i2s esp-skainet  esp-dsp
        driver
        esp_driver_i2s
)
