idf_component_register(
    SRCS "mao_main.c"
    PRIV_REQUIRES mao_system mao_board mao_display mao_input mao_audio mao_led mao_ir mao_app mao_devices
)
