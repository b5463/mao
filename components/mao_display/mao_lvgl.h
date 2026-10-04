/* Private to mao_display: LVGL port bring-up. */
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

typedef struct {
    lv_display_t *disp;
    esp_lcd_panel_handle_t panel;
    uint16_t board_rotation;     /* the board's mounting rotation (degrees) */
} mao_lvgl_t;

/* Initialise panel + esp_lvgl_port with partial draw buffers. */
esp_err_t mao_lvgl_start(mao_lvgl_t *out);
