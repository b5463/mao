/*
 * The focus line (M4.1): the one focus indicator on every surface. A 2 px
 * off-white line under the focused word, as wide as the word. It travels
 * with the dial: the edge towards the destination leads, the other follows
 * softer, so a fast turn stretches it and it gathers again on arrival
 * (CARRY). It also carries the action lifecycle of a primary word - press
 * (thicker), pending (a short dash, an aperture), landing (full width with
 * one colour event) - see docs/m4_1_ui_motion_concept.md.
 *
 * One LVGL object, touched only when its integer geometry, opacity or colour
 * changes. The owning surface sets the target and its presence every tick.
 */
#include <math.h>
#include "mao_ui_priv.h"

#define DASH_W        10.0f
#define THIN_H        2.0f
#define THICK_H       3.0f
#define LEAD_P        ((mao_spring_profile_t){ .k = 460.0f, .zeta = 0.80f })
#define TRAIL_P       ((mao_spring_profile_t){ .k = 170.0f, .zeta = 0.90f })
#define EVEN_P        ((mao_spring_profile_t){ .k = 300.0f, .zeta = 0.85f })

static struct {
    lv_obj_t *obj;
    mao_spring_t l, r, y, h;
    float cx, w;               /* requested centre / width */
    float presence;
    mao_focus_mode_t mode;
    uint32_t ev_color;
    uint32_t ev_start, ev_ms;
    bool placed;               /* a target has been set since it was last invisible */
    int16_t last_x, last_y, last_w, last_h;
    lv_opa_t last_opa;
    uint32_t last_color;
} s_f;

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    return ((uint32_t)(ar + (br - ar) * t) << 16) | ((uint32_t)(ag + (bg - ag) * t) << 8) |
           (uint32_t)(ab + (bb - ab) * t);
}

static void retarget(void)
{
    const float w = s_f.mode == MAO_FOCUS_DASH ? DASH_W : s_f.w;
    const float l = s_f.cx - w * 0.5f, r = s_f.cx + w * 0.5f;
    const float now_c = (s_f.l.x + s_f.r.x) * 0.5f;
    if (s_f.cx > now_c + 1.0f) {          /* travelling right: the right edge leads */
        s_f.r.p = LEAD_P;
        s_f.l.p = TRAIL_P;
    } else if (s_f.cx < now_c - 1.0f) {
        s_f.l.p = LEAD_P;
        s_f.r.p = TRAIL_P;
    } else {
        s_f.l.p = EVEN_P;
        s_f.r.p = EVEN_P;
    }
    s_f.l.target = l;
    s_f.r.target = r;
}

void mao_focus_create(lv_obj_t *scr)
{
    s_f.obj = lv_obj_create(scr);
    lv_obj_remove_style_all(s_f.obj);
    lv_obj_remove_flag(s_f.obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_align(s_f.obj, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(s_f.obj, 1, 0);
    lv_obj_set_style_bg_color(s_f.obj, lv_color_hex(MAO_COL_FG), 0);
    lv_obj_set_style_bg_opa(s_f.obj, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_f.obj, LV_OBJ_FLAG_HIDDEN);
    mao_spring_init(&s_f.l, 0.0f, EVEN_P);
    mao_spring_init(&s_f.r, 0.0f, EVEN_P);
    mao_spring_init(&s_f.y, 0.0f, ((mao_spring_profile_t){ .k = 300.0f, .zeta = 0.90f }));
    mao_spring_init(&s_f.h, THIN_H, ((mao_spring_profile_t){ .k = 700.0f, .zeta = 0.70f }));
    s_f.last_color = MAO_COL_FG;
    s_f.last_x = INT16_MIN;
}

void mao_focus_target(float cx, float y, float w)
{
    if (!s_f.placed) {
        /* Appearing: start at the target, no sweep from wherever it was. */
        s_f.l.x = s_f.l.target = cx - w * 0.5f;
        s_f.r.x = s_f.r.target = cx + w * 0.5f;
        s_f.l.v = s_f.r.v = 0.0f;
        s_f.y.x = s_f.y.target = y;
        s_f.y.v = 0.0f;
        s_f.placed = true;
    }
    if (cx != s_f.cx || w != s_f.w) {
        s_f.cx = cx;
        s_f.w = w;
        retarget();
    }
    s_f.y.target = y;
}

void mao_focus_presence(float p)
{
    s_f.presence = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    if (s_f.presence < 0.02f) {
        s_f.placed = false;
    }
}

void mao_focus_mode(mao_focus_mode_t m)
{
    if (m != s_f.mode) {
        s_f.mode = m;
        s_f.h.target = m == MAO_FOCUS_PRESS ? THICK_H : THIN_H;
        retarget();
    }
}

void mao_focus_event(uint32_t color, uint32_t ms)
{
    s_f.ev_color = color;
    s_f.ev_start = lv_tick_get();
    s_f.ev_ms = ms ? ms : 1;
}

bool mao_focus_tick(float dt, uint32_t now)
{
    mao_spring_step(&s_f.l, dt);
    mao_spring_step(&s_f.r, dt);
    mao_spring_step(&s_f.y, dt);
    mao_spring_step(&s_f.h, dt);

    const lv_opa_t opa = (lv_opa_t)(235.0f * s_f.presence);
    if (opa < 4) {
        if (s_f.last_opa >= 4) {
            lv_obj_add_flag(s_f.obj, LV_OBJ_FLAG_HIDDEN);
        }
        s_f.last_opa = 0;
        return false;
    }
    const float left = fminf(s_f.l.x, s_f.r.x), right = fmaxf(s_f.l.x, s_f.r.x);
    const int16_t w = (int16_t)lrintf(fmaxf(right - left, 2.0f));
    const int16_t x = (int16_t)lrintf((left + right) * 0.5f);
    const int16_t h = (int16_t)lrintf(s_f.h.x);
    const int16_t y = (int16_t)lrintf(s_f.y.x);
    if (w != s_f.last_w || h != s_f.last_h) {
        lv_obj_set_size(s_f.obj, w, h);
        s_f.last_w = w;
        s_f.last_h = h;
    }
    if (x != s_f.last_x || y != s_f.last_y) {
        lv_obj_set_pos(s_f.obj, x, y);
        s_f.last_x = x;
        s_f.last_y = y;
    }
    bool colouring = false;
    uint32_t color = MAO_COL_FG;
    if (s_f.ev_ms) {
        const uint32_t el = now - s_f.ev_start;
        if (el < s_f.ev_ms) {
            /* The colour arrives at once, holds a moment, then leaves. */
            const float t = (float)el / (float)s_f.ev_ms;
            color = mix(s_f.ev_color, MAO_COL_FG, t < 0.35f ? 0.0f : (t - 0.35f) / 0.65f);
            colouring = true;
        } else {
            s_f.ev_ms = 0;
        }
    }
    if (color != s_f.last_color) {
        lv_obj_set_style_bg_color(s_f.obj, lv_color_hex(color), 0);
        s_f.last_color = color;
    }
    if (opa != s_f.last_opa) {
        lv_obj_set_style_bg_opa(s_f.obj, opa, 0);
        if (s_f.last_opa < 4) {
            lv_obj_remove_flag(s_f.obj, LV_OBJ_FLAG_HIDDEN);
        }
        s_f.last_opa = opa;
    }
    return colouring || !mao_spring_settled(&s_f.l, 0.05f) || !mao_spring_settled(&s_f.r, 0.05f) ||
           !mao_spring_settled(&s_f.y, 0.05f) || !mao_spring_settled(&s_f.h, 0.02f);
}
