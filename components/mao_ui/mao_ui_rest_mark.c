/*
 * The resting mark (M4.1 power, mao_ui_home_dots.c's frame): the ODD JOBS
 * symbol that rests in the centre while MAO sleeps, and the wake's little
 * act that turns it back into the eyes.
 */
#include <math.h>
#include "mao_character.h"
#include "mao_display.h"
#include "mao_ui_priv.h"
#include "mao_ui.h"

#define REST_OX   (-7)                /* optical centring, as the startup mark's (mao_ui_home_dots.c) */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float smooth01(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* The resting mark (M4.1 power): once MAO is asleep the eyes gather into
 * the centre and go, and the ODD JOBS symbol rests there instead - a little
 * smaller than at startup, dim, in MAO's colour of the moment (lavender
 * asleep). At the wake it gives way and the eyes open from the centre: the
 * boot's own grammar, shortened. Symbol only, never the wordmark. */
#define REST_MARK_OPA    200.0f
/* The wake (poke, wobble, stretch, gulp, pop): squashed by the touch, a
 * jelly wobble, stretched tall with a hop, a beat at the top - then
 * swallowed into a point, and the eyes burst out of it. Keys: time ms,
 * width and height (1 = rest size), lift px (negative = up). */
typedef struct { int16_t t; float sx, sy, dy; } pop_key_t;
static const pop_key_t kPop[] = {
    {   0, 1.00f, 1.00f,   0.0f },
    { 110, 1.18f, 0.78f,   4.0f },   /* poke */
    { 190, 0.92f, 1.10f,   1.0f },   /* wobble ... */
    { 260, 1.07f, 0.94f,   2.0f },
    { 320, 0.98f, 1.03f,   1.0f },
    { 450, 0.72f, 1.32f, -12.0f },   /* stretch and hop */
    { 530, 0.76f, 1.26f, -13.0f },   /* hang */
    { 640, 0.00f, 0.00f,  -4.0f },   /* swallowed */
};
#define POP_EYES_MS     600
#define POP_END_MS      640
static lv_obj_t *s_smark;
static uint32_t s_pop_at;             /* 0 = no wake running */
static bool s_pop_eyes;
static uint16_t s_pop_sx = 256, s_pop_sy = 256;
static int16_t s_pop_dy;
static float s_smark_vis;
static bool s_smark_on;
static uint32_t s_smark_col;
static lv_opa_t s_smark_opa;

/* Room round the mask for the wake's stretch (132 % tall), squash (118 %
 * wide) and hop: an object only draws inside its own area. */
#define REST_PAD_X  16
#define REST_PAD_Y  30

static void smark_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    a.x1 += REST_PAD_X;                          /* the mask's own area, centred in the padded object */
    a.x2 -= REST_PAD_X;
    a.y1 += REST_PAD_Y;
    a.y2 -= REST_PAD_Y;
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &mao_mark_odd_jobs_rest;           /* its own 0.8-size mask: clean edges, no scaling at rest */
    if (s_pop_at) {
        d.scale_x = s_pop_sx;
        d.scale_y = s_pop_sy;
        d.pivot.x = mao_mark_odd_jobs_rest.header.w / 2;
        d.pivot.y = mao_mark_odd_jobs_rest.header.h / 2;
        a.y1 += s_pop_dy;
        a.y2 += s_pop_dy;
    }
    d.opa = s_smark_opa;
    d.recolor = lv_color_hex(s_smark_col);
    d.recolor_opa = LV_OPA_COVER;
    lv_draw_image(lv_event_get_layer(e), &d, &a);
}

void mao_home_sleep_mark_create(lv_obj_t *scr)
{
    s_smark = lv_obj_create(scr);
    lv_obj_remove_style_all(s_smark);
    lv_obj_remove_flag(s_smark, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_smark, mao_mark_odd_jobs_rest.header.w + 2 * REST_PAD_X,
                    mao_mark_odd_jobs_rest.header.h + 2 * REST_PAD_Y);
    lv_obj_set_align(s_smark, LV_ALIGN_CENTER);
    lv_obj_set_pos(s_smark, REST_OX, 0);            /* where the startup mark sits */
    lv_obj_add_event_cb(s_smark, smark_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_smark, LV_OBJ_FLAG_HIDDEN);
}

void mao_ui_sleep_mark(bool on)
{
    if (!mao_display_lock(0)) {
        return;
    }
    if (on && !s_smark_on) {
        mao_character_gather();                      /* the sleeping eyes draw into the centre and go */
        s_smark_on = true;
    } else if (!on && s_smark_on) {
        mao_home_sleep_mark_set(false);              /* ... and burst out of the mark at the wake */
    }
    mao_ui_wake();
    mao_display_unlock();
}

void mao_home_sleep_mark_set(bool on)
{
    if (!on && s_smark_on && !s_pop_at) {
        s_pop_at = lv_tick_get() | 1u;
        s_pop_eyes = false;
    }
    s_smark_on = on;
}

/* The wake's little act; returns true while it runs. */
static bool rest_pop(uint32_t now)
{
    if (!s_pop_at) {
        return false;
    }
    const int32_t t = (int32_t)(now - s_pop_at);
    float sx = 0.0f, sy = 0.0f, dy = 0.0f;
    for (size_t k = 1; k < sizeof(kPop) / sizeof(kPop[0]); k++) {
        if (t <= kPop[k].t || k == sizeof(kPop) / sizeof(kPop[0]) - 1) {
            const pop_key_t *a = &kPop[k - 1], *b = &kPop[k];
            const float u = smooth01((float)(t - a->t) / (float)(b->t - a->t));
            sx = a->sx + (b->sx - a->sx) * u;
            sy = a->sy + (b->sy - a->sy) * u;
            dy = a->dy + (b->dy - a->dy) * u;
            break;
        }
    }
    s_pop_sx = (uint16_t)clampf(256.0f * sx, 1.0f, 400.0f);
    s_pop_sy = (uint16_t)clampf(256.0f * sy, 1.0f, 400.0f);
    s_pop_dy = (int16_t)lrintf(dy);
    if (!s_pop_eyes && t >= POP_EYES_MS) {
        s_pop_eyes = true;
        mao_character_return();                       /* the eyes open from the point ... */
        mao_character_quirk(MAO_QUIRK_WAKEPOP);       /* ... with a pop, two blinks and a look round */
    }
    if (t >= POP_END_MS) {
        s_pop_at = 0;
        s_smark_vis = 0.0f;
        s_pop_sx = s_pop_sy = 256;
        s_pop_dy = 0;
        return false;
    }
    lv_obj_invalidate(s_smark);
    return true;
}

void mao_ui_sleep_mark_now(bool on)
{
    /* the resting frame is drawn at once (before the chip sleeps, or at a deep-sleep wake) */
    s_smark_on = on;
    s_smark_vis = on ? 1.0f : 0.0f;
}

bool mao_home_rest_mark_layout(float dt)
{
    if (!s_smark) {
        return false;
    }
    if (rest_pop(lv_tick_get())) {
        return true;                                  /* the mark is the act: full presence while it plays */
    }
    const float tgt = s_smark_on ? 1.0f : 0.0f;
    const float tau = 0.5f;                           /* it settles in (it leaves by the pop) */
    s_smark_vis += (tgt - s_smark_vis) * (1.0f - expf(-dt / tau));
    if (fabsf(tgt - s_smark_vis) < 0.01f) {
        s_smark_vis = tgt;
    }
    const lv_opa_t o = (lv_opa_t)clampf(REST_MARK_OPA * s_smark_vis, 0.0f, 255.0f);
    const uint32_t col = mao_character_accent() & 0xF8FCF8u;
    if (o < 4) {
        if (s_smark_opa >= 4) {
            lv_obj_add_flag(s_smark, LV_OBJ_FLAG_HIDDEN);
        }
        s_smark_opa = 0;
        return s_smark_vis != tgt;
    }
    if (s_smark_opa < 4) {
        lv_obj_remove_flag(s_smark, LV_OBJ_FLAG_HIDDEN);
    }
    if (o != s_smark_opa || col != s_smark_col) {
        s_smark_opa = o;
        s_smark_col = col;
        lv_obj_invalidate(s_smark);
    }
    return s_smark_vis != tgt;
}
