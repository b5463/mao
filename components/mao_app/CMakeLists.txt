idf_component_register(
    SRCS "mao_app.c" "mao_state.c" "mao_app_percept.c" "mao_app_power.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_system mao_ui mao_character mao_display mao_input mao_audio mao_led mao_devices esp_timer
                  mao_haptics mao_perception mao_power mao_selftest esp_hw_support
)
