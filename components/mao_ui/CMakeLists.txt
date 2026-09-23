idf_component_register(
    SRCS "mao_ui.c" "mao_home.c" "mao_overlay.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_display mao_character lvgl
)
