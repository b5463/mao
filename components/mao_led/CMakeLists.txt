idf_component_register(
    SRCS "mao_led.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_board esp_timer
)
