idf_component_register(
    SRCS "mao_battery.c" "mao_battery_gauge.c" "mao_battery_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES mao_board mao_system esp_driver_gpio esp_timer freertos
)
