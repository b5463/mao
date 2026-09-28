idf_component_register(
    SRCS "mao_app.c" "mao_hold.c" "mao_fiddle.c" "mao_state.c" "mao_app_transfer.c" "mao_app_rel.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_system mao_ui mao_character mao_display mao_input mao_audio mao_led mao_devices mao_relationships mao_link esp_timer
)
