# One implementation per board, picked by the "MAO board" Kconfig choice.
# The requirement lists are the union of all boards: IDF expands component
# requirements before the Kconfig values exist, so they cannot be conditional.
set(srcs "mao_board_common.c")
set(board_dir "")
if(CONFIG_MAO_BOARD_LCDKIT)
    set(board_dir "boards/lcdkit")
    list(APPEND srcs "boards/lcdkit/mao_board.c"
                     "boards/lcdkit/mao_board_lcdkit_ext.c")
elseif(CONFIG_MAO_BOARD_MAIN_A1)
    set(board_dir "boards/main_a1")
    list(APPEND srcs "boards/main_a1/mao_board_a1.c"
                     "boards/main_a1/mao_board_a1_i2c.c"
                     "boards/main_a1/mao_board_a1_display.c"
                     "boards/main_a1/mao_board_a1_audio.c"
                     "boards/main_a1/mao_board_a1_sleep.c")
elseif(NOT CMAKE_BUILD_EARLY_EXPANSION)
    message(FATAL_ERROR "MAO: no board profile for target ${IDF_TARGET}")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "." ${board_dir}
    REQUIRES esp_lcd esp_driver_i2s esp_driver_i2c led_strip
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_spi esp_hw_support esp_timer freertos
                  nvs_flash mao_system
)
