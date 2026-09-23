/* Private to mao_ui. All functions expect the display lock to be held. */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

/* Palette. */
#define MAO_UI_BG          0x08080A
#define MAO_UI_FG          0xF1ECE2
#define MAO_UI_DIM         0x6E6C68

/* Motion language: short, direct, eased. */
#define MAO_UI_T_ENTER     220
#define MAO_UI_T_LEAVE     150
#define MAO_UI_T_STAGGER   70

/* Animate *var from its current value to `to` (0..1 style floats) and wake
 * `timer` so the owner can re-layout. ease_out = true for arrivals. */
void mao_ui_anim_float(float *var, float to, uint32_t duration_ms, uint32_t delay_ms,
                       bool ease_out, lv_timer_t *timer);
bool mao_ui_anim_running(float *var);

/* HOME (mao_home.c) */
void mao_home_create(lv_obj_t *scr, bool wordmark_visible);
void mao_home_boot(void);

/* Overlay views (mao_overlay.c) */
void mao_overlay_create(lv_obj_t *scr, bool intro_visible);
void mao_overlay_intro(bool show);
void mao_overlay_menu(bool show, int index, uint32_t delay_ms);
void mao_overlay_menu_select(int index);
void mao_overlay_menu_bump(int direction);
void mao_overlay_placeholder(bool show, const char *title, uint32_t delay_ms);
