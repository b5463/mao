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

/* Timing vocabulary (M4.1, docs/m4_1_ui_motion_concept.md section 2). */
#define MAO_UI_TAP      ((mao_spring_profile_t){ .k = 700.0f, .zeta = 0.55f })   /* ~120 ms */
#define MAO_UI_SELECT   ((mao_spring_profile_t){ .k = 260.0f, .zeta = 0.78f })   /* ~200 ms */
#define MAO_UI_PAGE     MAO_SPRING_HEAVY                                         /* ~300 ms */
#define MAO_UI_SHEET    ((mao_spring_profile_t){ .k = 320.0f, .zeta = 0.95f })   /* ~220 ms */
#define MAO_UI_LAND     MAO_SPRING_SNAP                                          /* overshoot */
#define MAO_UI_SETTLE   MAO_SPRING_SOFT                                          /* ~450 ms */
#define MAO_UI_BEAT_MS  120      /* the one hand-off delay: gaze -> arrival, clear -> enter */

/* Circle-aware layout (mao_ui_layout.c). */
float mao_ui_chord_half(float y);                 /* usable half-width of the row at y */
int mao_ui_text_width(const lv_font_t *font, int32_t track, const char *txt);
/* Tighten a label's tracking until it fits its row; returns the width.
 * *applied caches the tracking last set (avoids restyling). */
int mao_ui_fit(lv_obj_t *label, const lv_font_t *font, int32_t track, float y, int32_t *applied);

/* The dot field (mao_ui_dots.c): build a frame with begin / glyphs... / end. */
typedef enum {
    MAO_GLYPH_DOT = 0,
    MAO_GLYPH_SQUARE,
    MAO_GLYPH_PLUS,
    MAO_GLYPH_CROSS,
    MAO_GLYPH_DASH,
    MAO_GLYPH_STAR,
} mao_glyph_t;
void mao_dots_create(lv_obj_t *scr);
void mao_dots_begin(void);
void mao_dots_glyph(float x, float y, float size, float opa, mao_glyph_t kind, uint8_t col);   /* col 1 = cut-out */
void mao_dots_add(float x, float y, float d, float opa);                                      /* a round dot */
int mao_dots_text_cols(const char *s);          /* width in matrix columns */
/* 5x7 dot text centred at (cx, cy); reveal_r >= 0 shows only dots within
 * that radius of the screen centre (a bloom reaching them). */
void mao_dots_text(const char *s, float cx, float cy, float pitch, float d, float opa, float reveal_r);
/* The same arriving through a ragged, soft front of radius `front` (each dot
 * at its own moment, as the field grows). */
void mao_dots_text_front(const char *s, float cx, float cy, float pitch, float d, float opa, float front);
/* The first encounter's state (mao_overlay.c): presence 0..1, lateral shift. */
void mao_overlay_intro_get(float *presence, float *shift);
/* The same on a black halo: readable over the field. */
void mao_dots_text_halo(const char *s, float cx, float cy, float pitch, float d, float opa);
/* Text along the rim: at the top (reading clockwise) or the bottom; r = the
 * middle row's radius; halo: a dark backing (over the field). */
void mao_dots_text_arc(const char *s, float r, bool bottom, float pitch, float d, float opa, bool halo);
float mao_dots_text_arc_width(const char *s, float pitch);   /* its length along the arc, px */
void mao_dots_field(float radius, float strength, float angle, uint32_t now_ms);   /* turning lattice bloom */
void mao_dots_ring(float r, int count, float size, float opa, mao_glyph_t kind, float phase, float fraction);
float mao_dots_noise(int i, int j, uint32_t t);  /* 0..1, deterministic */
bool mao_dots_end(void);

/* The ODD JOBS field (mao_ui_field.c): reach in px from the centre. */
void mao_field_create(lv_obj_t *scr);
void mao_field_set(float reach, float strength, float oy, uint32_t now_ms);   /* oy: centre, px down */

/* The focus line (mao_ui_focus.c). */
typedef enum {
    MAO_FOCUS_NORMAL = 0,
    MAO_FOCUS_PRESS,           /* the button is down: a little heavier */
    MAO_FOCUS_DASH,            /* the action is underway: an aperture under the word */
} mao_focus_mode_t;
void mao_focus_create(lv_obj_t *scr);
void mao_focus_target(float cx, float y, float w);
void mao_focus_presence(float p);
void mao_focus_mode(mao_focus_mode_t m);
void mao_focus_event(uint32_t color, uint32_t ms);   /* one colour event, then off-white again */
bool mao_focus_tick(float dt, uint32_t now_ms);

/* HOME wordmark (mao_home.c). Tick functions return true while still moving. */
void mao_home_create(lv_obj_t *scr, bool visible);
void mao_home_boot(uint32_t now_ms);
void mao_home_replay(uint32_t now_ms);
bool mao_home_tick(float dt, uint32_t now_ms);
/* The boot bloom (mao_ui_devices.c): the field from the centre, MAO in dots, back into the centre. */
void mao_devices_boot_bloom(uint32_t now_ms);

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
void mao_devlist_show(bool show, uint32_t delay_ms, bool lateral);   /* lateral: to / from the right */
void mao_devpanel_show(bool show, uint32_t delay_ms);
