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

/* Text helpers + typographic list (mao_ui_list.c) */
typedef struct {
    int16_t y;
    lv_opa_t opa;
} mao_ui_text_cache_t;

lv_obj_t *mao_ui_make_text(lv_obj_t *scr, const lv_font_t *font, uint32_t color, int32_t letter_space);
/* Move/fade a label, touching LVGL only when the values change. */
void mao_ui_text_state(lv_obj_t *o, int16_t y, lv_opa_t opa, mao_ui_text_cache_t *cache);

#define MAO_UI_LIST_MAX 8

typedef struct {
    lv_obj_t *label[MAO_UI_LIST_MAX];
    mao_ui_text_cache_t cache[MAO_UI_LIST_MAX];
    float row_scale[MAO_UI_LIST_MAX];   /* per-row brightness factor (e.g. offline rows) */
    int capacity;
    int count;
    float spacing;
    int16_t y_offset;
    float pos, vel, target, presence;
    lv_timer_t *timer;
    uint32_t last_ms;
} mao_ui_list_t;

void mao_ui_list_create(mao_ui_list_t *l, lv_obj_t *scr, const lv_font_t *font, int capacity,
                        float spacing, int16_t y_offset);
void mao_ui_list_set_items(mao_ui_list_t *l, const char *const *texts, const float *row_scale, int count);
void mao_ui_list_show(mao_ui_list_t *l, bool show, int index, uint32_t delay_ms);
void mao_ui_list_select(mao_ui_list_t *l, int index);
void mao_ui_list_bump(mao_ui_list_t *l, int direction);

/* HOME (mao_home.c) */
void mao_home_create(lv_obj_t *scr, bool wordmark_visible);
void mao_home_boot(void);
void mao_home_skip_wordmark(void);
void mao_home_resume(void);
void mao_home_fault(const char *code);

/* Overlay views (mao_overlay.c) */
void mao_overlay_create(lv_obj_t *scr, bool intro_visible);
void mao_overlay_intro(bool show);
void mao_overlay_menu(bool show, int index, uint32_t delay_ms);
void mao_overlay_menu_select(int index);
void mao_overlay_menu_bump(int direction);
void mao_overlay_placeholder(bool show, const char *title, uint32_t delay_ms);

/* Device views (mao_ui_devices.c) */
void mao_devices_ui_create(lv_obj_t *scr);
void mao_devlist_show(bool show, uint32_t delay_ms);
void mao_devpanel_show(bool show, uint32_t delay_ms);
