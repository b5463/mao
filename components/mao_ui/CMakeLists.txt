idf_component_register(
    SRCS "mao_ui.c" "mao_home.c" "mao_overlay.c" "mao_ui_list.c" "mao_ui_devices.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_display mao_character lvgl
)
