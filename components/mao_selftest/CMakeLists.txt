idf_component_register(
    SRCS "mao_selftest.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_board mao_system mao_sense mao_power mao_haptics mao_audio mao_ir mao_input
                  mao_display mao_radio esp_driver_gpio esp_timer esp_app_format esp_hw_support nvs_flash
)
