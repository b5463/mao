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

/* The panel came up (false: the UI runs headless, nothing is drawn). */
bool mao_display_ok(void);

/* Image rotation on the panel, clockwise degrees (0/90/180/270). Starts at
 * the board's mounting rotation (or the override saved with "mao rotate
 * save"); changing it is live and costs nothing per frame (MADCTL). */
esp_err_t mao_display_set_rotation(uint16_t degrees);
uint16_t mao_display_get_rotation(void);

/* Factory self-test helpers. A full-screen plate above every view with an
 * optional big / small text line (rgb as 0xRRGGBB); clear hides it. */
esp_err_t mao_display_test_show(uint32_t bg_rgb, uint32_t fg_rgb, const char *big, const char *small);
void mao_display_test_clear(void);
/* Self-test: switch the panel rail off (rendering pauses, the UI keeps
 * running undrawn) or on again (panel re-initialised and redrawn). Lets a
 * fixture measure the switched rail at its test pad in both states.
 * ESP_ERR_NOT_SUPPORTED where the rail is not switchable.
 * After an expander reset the panel is re-initialised automatically. */
esp_err_t mao_display_rail(bool on);

#ifdef __cplusplus
}
#endif
