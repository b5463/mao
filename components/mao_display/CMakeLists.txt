idf_component_register(
    SRCS "mao_display.c" "mao_lvgl.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_board mao_system esp_lcd esp_timer
)
