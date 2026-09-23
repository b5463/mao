idf_component_register(
    SRCS "mao_character.c" "mao_character_draw.c" "mao_character_motion.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    PRIV_REQUIRES esp_hw_support
)
