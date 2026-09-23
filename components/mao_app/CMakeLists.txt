idf_component_register(
    SRCS "mao_app.c" "mao_state.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_system mao_ui mao_character mao_display mao_input mao_audio mao_led mao_devices esp_timer
)
