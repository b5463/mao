idf_component_register(
    SRCS "mao_input.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_board mao_system esp_driver_gpio esp_timer
)
