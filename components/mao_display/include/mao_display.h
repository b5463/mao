/*
 * MAO display infrastructure: GC9A01 panel + LVGL (via esp_lvgl_port).
 *
 * This component owns the panel, LVGL runtime, backlight and the frame
 * performance probe. It draws nothing itself: views live in mao_ui.
 *
 * Bring-up order: mao_display_init() (panel stays dark) -> build the first
 * view under mao_display_lock() -> mao_display_start() renders that frame
 * and only then switches the panel and backlight on.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mao_display_init(void);

/* Render the current screen, then enable panel and backlight. */
esp_err_t mao_display_start(uint8_t brightness_percent);

/* LVGL access from tasks other than the LVGL task. timeout_ms 0 = forever. */
bool mao_display_lock(uint32_t timeout_ms);
void mao_display_unlock(void);

/* Backlight 0..100 %. */
esp_err_t mao_display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif
