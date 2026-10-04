idf_component_register(
    SRCS "mao_power.c" "mao_power_gauge.c" "mao_power_policy.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_board mao_system esp_driver_gpio esp_driver_i2c esp_hw_support esp_timer
)
