idf_component_register(
    SRCS "mao_character.c" "mao_character_dial.c" "mao_character_attention.c" "mao_character_react.c"
         "mao_character_draw.c" "mao_character_motion.c" "mao_character_idle.c" "mao_character_transfer.c"
         "mao_lark.c" "mao_lark_gen.c" "mao_lark_library.c" "mao_lark_states_daily.c" "mao_lark_states_moods.c"
         "mao_lark_states_life.c" "mao_lark_states_cat.c" "mao_lark_states_more.c" "mao_lark_states_ctrl.c" "mao_life.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    PRIV_REQUIRES esp_hw_support esp_timer log mao_audio
)
