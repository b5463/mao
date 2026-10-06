idf_component_register(
    SRCS "mao_haptics.c" "mao_drv2605.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_board mao_system esp_driver_i2c esp_timer nvs_flash
)
