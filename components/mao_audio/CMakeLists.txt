idf_component_register(
    SRCS "mao_audio.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_board esp_driver_i2s esp_timer
)
