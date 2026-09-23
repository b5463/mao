idf_component_register(
    SRCS "mao_board.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_i2s led_strip
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_spi esp_hw_support
)
