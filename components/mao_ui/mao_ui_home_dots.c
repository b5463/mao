/*
 * HOME's dots (M4.1): the PRESS hint, the first encounter's name, the boot
 * bloom, the SOUND / SCREEN tune and the light's arc on the rim. Split
 * from mao_ui_devices.c, which owns the dot layer's frame and calls
 * mao_home_dots_layout() once per tick.
 */
#include <math.h>
#include <stdio.h>
#include "mao_character.h"
#include "mao_display.h"
#include "mao_ui_priv.h"
#include "mao_ui.h"

#define PI_F 3.14159265f

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float smooth01(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static struct {
    bool allowed;
    uint32_t since;
} s_hint;

void mao_ui_home_hint(bool allowed)
{
    if (!mao_display_lock(0)) {
        return;
    }
    s_hint.allowed = allowed;
    s_hint.since = lv_tick_get();
    mao_ui_wake();
    mao_display_unlock();
}

/* The first encounter, in MAO's own dots: the name arrives through a ragged
 * front, TURN breathes under it; turning pulls the name aside as it goes and
 * the eyes appear (mao_ui_intro_exit). */
static uint32_t s_intro_t0;

static bool intro_layout(uint32_t now)
{
    float p, shift;
    mao_overlay_intro_get(&p, &shift);
    if (p < 0.004f) {
        return false;
    }
    if (!s_intro_t0) {
        s_intro_t0 = now;
    }
    const float t = (float)(now - s_intro_t0) / 1000.0f;
    const float front = 20.0f + 150.0f * smooth01(t / 1.6f);
    mao_dots_text_front("MAO", shift, -12.0f, 6.0f, 5.0f, 255.0f * p, front);
    const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf(t * 3.2f));
    const float late = smooth01((t - 1.2f) / 0.8f);          /* after the name has arrived */
    mao_dots_text("TURN", 0.0f, 34.0f, 2.2f, 1.8f, 190.0f * p * p * p * pulse * late, -1.0f);
    return true;
}

/* HOME's settings (mao_ui_home_tune): chosen, then set, in dots. */
static struct {
    mao_tune_mode_t mode;
    int8_t sel;                  /* CHOOSE: -1 middle, 0 SOUND, 1 SCREEN; ADJUST: which */
    int value;
    uint32_t since;              /* when it came up (the eyes gather first) */
    mao_spring_t shown;          /* the drawn value follows the set one softly */
} s_tune;

void mao_ui_home_tune(mao_tune_mode_t mode, int8_t sel, int value)
{
    if (!mao_display_lock(0)) {
        return;
    }
    if (mode != MAO_TUNE_OFF && s_tune.mode == MAO_TUNE_OFF) {
        mao_character_gather();                  /* MAO makes room */
        s_tune.since = lv_tick_get();
        s_tune.shown.x = (float)value;
    } else if (mode == MAO_TUNE_OFF && s_tune.mode != MAO_TUNE_OFF) {
        mao_character_return();                  /* and comes back */
    }
    if (mode == MAO_TUNE_ADJUST && s_tune.mode != MAO_TUNE_ADJUST) {
        s_tune.shown.x = (float)value;
    }
    s_tune.mode = mode;
    s_tune.sel = sel;
    s_tune.value = value;
    s_tune.shown.target = (float)value;
    mao_ui_wake();
    mao_display_unlock();
}

static bool home_tune_layout(uint32_t now, float dt)
{
    if (s_tune.mode == MAO_TUNE_OFF) {
        return false;
    }
    mao_spring_step(&s_tune.shown, dt);
    const float in = smooth01(((float)(int32_t)(now - s_tune.since) - (float)MAO_CHAR_GATHER_MS) / 220.0f);   /* signed: after the eyes gather */
    if (s_tune.mode == MAO_TUNE_CHOOSE) {
        /* the same three-position switch as a device page's hold */
        if (s_tune.sel < 0) {
            mao_dots_glyph(0.0f, 0.0f, 5.0f, 220.0f * in, MAO_GLYPH_DOT, 0);
            mao_dots_text("SOUND", -60.0f, 0.0f, 1.9f, 1.6f, 160.0f * in, -1.0f);
            mao_dots_text("SCREEN", 60.0f, 0.0f, 1.9f, 1.6f, 160.0f * in, -1.0f);
        } else {
            mao_dots_text(s_tune.sel == 0 ? "SOUND" : "SCREEN", 0.0f, 0.0f, 3.0f, 2.6f, 255.0f * in, -1.0f);
        }
        return true;
    }
    /* ADJUST: a ring filled to the level, the level large, its name above */
    const float v = clampf(s_tune.shown.x, 0.0f, 100.0f);
    const int n = 40;
    for (int i = 0; i < n; i++) {
        const float f = (float)i / (float)(n - 1);
        const float an = -2.6f + 5.2f * f;             /* a 300 deg arc, open at the bottom */
        const bool lit = f * 100.0f <= v + 0.01f;
        mao_dots_glyph(94.0f * sinf(an), -94.0f * cosf(an), lit ? 5.0f : 3.0f, (lit ? 255.0f : 60.0f) * in,
                       MAO_GLYPH_SQUARE, 0);
    }
    mao_dots_text(s_tune.sel == 0 ? "SOUND" : "SCREEN", 0.0f, -44.0f, 2.2f, 1.8f, 170.0f * in, -1.0f);
    char num[4];
    snprintf(num, sizeof(num), "%d", (int)lrintf(v));
    mao_dots_text(num, 0.0f, 10.0f, 6.0f, 5.0f, 255.0f * in, -1.0f);
    return true;
}

/* HOME's light (mao_ui_home_lamp): a plain turn sets the light used last
 * without opening it. The level is an arc of dots along the rim, open at the
 * bottom where the light's name sits; the eyes look towards its lit end and
 * are held well inside it (mao_character_look), never touching it. It
 * leaves on its own shortly after. */
#define HOME_LAMP_HOLD_MS 1600
#define HOME_LAMP_R       104.0f
#define HOME_LAMP_ARC     2.30f            /* half the arc, rad from the top */

static struct {
    char name[24];
    bool on;
    uint32_t last;                         /* the last turn */
    mao_spring_t shown;                    /* the drawn level follows the set one softly */
    mao_spring_t vis;                      /* 0..1 */
} s_hl;

void mao_ui_home_lamp(const char *name, int pct, bool on)
{
    if (!mao_display_lock(0)) {
        return;
    }
    if (s_hl.vis.target < 0.5f) {
        s_hl.shown.x = (float)pct;         /* it appears at its level, not from zero */
    }
    snprintf(s_hl.name, sizeof(s_hl.name), "%s", name ? name : "");
    s_hl.on = on;
    s_hl.shown.target = (float)pct;
    s_hl.vis.target = 1.0f;
    s_hl.last = lv_tick_get();
    /* the eyes look where the scale ends - and keep clear of it */
    const float an = -HOME_LAMP_ARC + 2.0f * HOME_LAMP_ARC * (on ? (float)pct / 100.0f : 0.0f);
    mao_character_look((int)(HOME_LAMP_R * sinf(an)), (int)(-HOME_LAMP_R * cosf(an)), true);
    mao_ui_wake();
    mao_display_unlock();
}

static bool home_lamp_layout(uint32_t now, float dt)
{
    if (s_hl.vis.target > 0.5f && now - s_hl.last > HOME_LAMP_HOLD_MS) {
        s_hl.vis.target = 0.0f;
        mao_character_look(0, 0, false);
    }
    mao_spring_step(&s_hl.vis, dt);
    mao_spring_step(&s_hl.shown, dt);
    const float in = clampf(s_hl.vis.x, 0.0f, 1.0f);
    if (in < 0.01f && s_hl.vis.target < 0.5f) {
        return false;
    }
    const float v = clampf(s_hl.shown.x, 0.0f, 100.0f);
    const int n = 48;
    for (int i = 0; i < n; i++) {
        const float f = (float)i / (float)(n - 1);
        const float an = -HOME_LAMP_ARC + 2.0f * HOME_LAMP_ARC * f;
        const bool lit = s_hl.on && f * 100.0f <= v + 0.01f;
        /* the arc arrives from the name outwards, like the lamp page's field grows */
        const float arrive = clampf(in * 1.8f - (0.5f - fabsf(f - 0.5f)) * 1.6f, 0.0f, 1.0f);   /* the top last: the eyes settle first */
        mao_dots_glyph(HOME_LAMP_R * sinf(an), -HOME_LAMP_R * cosf(an), lit ? 4.0f : 2.4f,
                       (lit ? 255.0f : 60.0f) * arrive, MAO_GLYPH_SQUARE, 0);
    }
    const float nw = mao_dots_text_arc_width(s_hl.name, 1.0f);
    const float pitch = nw > 1.0f ? fminf(2.2f, 120.0f / nw) : 2.2f;
    mao_dots_text_arc(s_hl.name, HOME_LAMP_R, true, pitch, 1.8f, 200.0f * in, false);
    return true;
}
/* Boot (M4.1): first the maker's mark - the ODD JOBS symbol, alone, in MAO's
 * colour, a moment on black - then KINO D4's boot in MAO's field: it blooms
 * from the centre through the mark and over it, MAO's name arrives in dots,
 * then the field draws back into the centre, where the eyes open
 * (mao_home.c, MAO_BOOT_EYES_MS). The wordmark is never drawn. */
static uint32_t s_boot_t0;
static lv_obj_t *s_mark;
#define MARK_OX   -7                 /* optical centre: the mark's weight sits right of its box (half its centroid offset) */

static uint32_t s_mark_col;
static lv_opa_t s_mark_opa;

/* The mask drawn with the core image draw (no image widget in this build):
 * coverage from the asset, colour from MAO. */
static void mark_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &mao_mark_odd_jobs;
    d.opa = s_mark_opa;
    d.recolor = lv_color_hex(s_mark_col);
    d.recolor_opa = LV_OPA_COVER;
    lv_draw_image(lv_event_get_layer(e), &d, &a);
}

void mao_home_mark_create(lv_obj_t *scr)
{
    s_mark = lv_obj_create(scr);
    lv_obj_remove_style_all(s_mark);
    lv_obj_remove_flag(s_mark, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_mark, mao_mark_odd_jobs.header.w, mao_mark_odd_jobs.header.h);
    lv_obj_set_align(s_mark, LV_ALIGN_CENTER);
    lv_obj_set_pos(s_mark, MARK_OX, 0);
    lv_obj_add_event_cb(s_mark, mark_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_mark, LV_OBJ_FLAG_HIDDEN);
}

void mao_devices_boot_bloom(uint32_t now_ms)
{
    s_boot_t0 = now_ms ? now_ms : 1;
}

static void mark_show(float opa)
{
    if (!s_mark) {
        return;
    }
    static lv_opa_t last;
    const lv_opa_t o = (lv_opa_t)clampf(opa, 0.0f, 255.0f);
    if (o < 4) {
        if (last >= 4) {
            lv_obj_add_flag(s_mark, LV_OBJ_FLAG_HIDDEN);
        }
        last = 0;
        return;
    }
    if (last < 4) {
        s_mark_col = mao_character_accent();       /* MAO's own colour */
        lv_obj_remove_flag(s_mark, LV_OBJ_FLAG_HIDDEN);
    }
    if (o != last) {
        s_mark_opa = o;
        lv_obj_invalidate(s_mark);
        last = o;
    }
}

static bool boot_layout(uint32_t now)
{
    if (!s_boot_t0) {
        return false;
    }
    const float tm = (float)(now - s_boot_t0) / 1000.0f;
    /* the maker's mark: in, a held beat, and the bloom takes it from the centre */
    mark_show(255.0f * smooth01((tm - 0.08f) / 0.18f) * (1.0f - smooth01((tm - 0.56f) / 0.2f)));
    const float t = tm - MAO_BOOT_MARK_S;          /* the approved bloom, after the mark */
    if (t > 1.5f) {
        s_boot_t0 = 0;
        mark_show(0.0f);
        return false;
    }
    if (t < 0.0f) {
        return true;
    }
    const float grow = smooth01(t / 0.6f);                  /* out ... */
    const float back = smooth01((t - 0.8f) / 0.32f);        /* ... and back into the centre */
    mao_ui_field_request(122.0f * grow * (1.0f - back), 0.9f, 0.0f, 0.0f, 5.0f);   /* over anything else */
    const float in = smooth01((t - 0.22f) / 0.3f), out = 1.0f - smooth01((t - 0.72f) / 0.22f);
    if (in * out > 0.01f) {
        mao_dots_text_halo("MAO", 0.0f, -2.0f, 5.0f, 4.2f, 255.0f * in * out);
    }
    return true;
}

/* HOME, left alone for a moment by someone new to it: a quiet PRESS. */
static void home_hint_layout(uint32_t now)
{
    if (!s_hint.allowed || now - s_hint.since < 6000 || s_tune.mode != MAO_TUNE_OFF || s_hl.vis.x > 0.02f) {
        return;
    }
    const float t = (float)(now - s_hint.since - 6000) / 1000.0f;
    const float in = clampf(t / 0.8f, 0.0f, 1.0f);
    const float breathe = 0.75f + 0.25f * sinf(t * 2.0f * PI_F / 2.4f);
    mao_dots_text("PRESS", 0.0f, 98.0f, 2.0f, 1.7f, 190.0f * in * breathe, -1.0f);
}

/* One call per UI tick, after the device scenes and before the dots are
 * flushed; the order is the drawing order. True while any of them moves. */
bool mao_home_dots_layout(uint32_t now, float dt)
{
    home_hint_layout(now);
    const bool intro_up = intro_layout(now);
    const bool boot_up = boot_layout(now);
    const bool tune_up = home_tune_layout(now, dt);
    const bool lamp_up = home_lamp_layout(now, dt);
    return s_hint.allowed || intro_up || tune_up || lamp_up || boot_up;
}

void mao_home_dots_init(void)
{
    mao_spring_init(&s_tune.shown, 0.0f, ((mao_spring_profile_t){ .k = 180.0f, .zeta = 0.9f }));
    mao_spring_init(&s_hl.shown, 0.0f, ((mao_spring_profile_t){ .k = 180.0f, .zeta = 0.9f }));
    mao_spring_init(&s_hl.vis, 0.0f, MAO_UI_PAGE);
}
