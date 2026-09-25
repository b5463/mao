/* Private to mao_ui. All functions expect the display lock to be held. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "mao_spring.h"
#include "mao_ui_type.h"

/* Wake the shared UI motion tick (it pauses itself once everything settles). */
void mao_ui_wake(void);

/* Text helpers. */
typedef struct {
    int16_t x, y;
    lv_opa_t opa;
} mao_text_cache_t;

lv_obj_t *mao_ui_make_text(lv_obj_t *scr, const lv_font_t *font, uint32_t color, int32_t track, const char *txt);
/* Move/fade a label, touching LVGL only when values change; hides at ~0 opacity. */
void mao_ui_text_place(lv_obj_t *o, float x, float y, float opa, mao_text_cache_t *c);
void mao_ui_text_cache_reset(mao_text_cache_t *c);

/* HOME wordmark (mao_home.c). Tick functions return true while still moving. */
void mao_home_create(lv_obj_t *scr, bool visible);
void mao_home_boot(uint32_t now_ms);
void mao_home_replay(uint32_t now_ms);
bool mao_home_tick(float dt, uint32_t now_ms);

/* Menu, placeholder and first encounter (mao_overlay.c). */
void mao_overlay_create(lv_obj_t *scr, bool intro_visible);
bool mao_overlay_tick(float dt, uint32_t now_ms);
void mao_overlay_menu_show(bool show, int index, uint32_t delay_ms);
void mao_overlay_menu_select(int index);
void mao_overlay_menu_bump(int direction);
void mao_overlay_page_show(bool show, const char *title);
void mao_overlay_intro_exit(int direction);

/* DEVICES list and device view (mao_ui_devices.c). */
void mao_devices_ui_create(lv_obj_t *scr);
bool mao_devices_ui_tick(float dt, uint32_t now_ms);
void mao_devlist_show(bool show, uint32_t delay_ms);
void mao_devpanel_show(bool show, uint32_t delay_ms);
