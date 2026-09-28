idf_component_register(
    SRCS "mao_ui.c" "mao_home.c" "mao_overlay.c" "mao_ui_devices.c" "mao_ui_layout.c" "mao_ui_focus.c" "mao_ui_dots.c"
         "fonts/mao_font_fact.c" "fonts/mao_font_name.c" "fonts/mao_font_word.c"
         "fonts/mao_font_code.c" "fonts/mao_font_numeral.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_display mao_character lvgl
)
