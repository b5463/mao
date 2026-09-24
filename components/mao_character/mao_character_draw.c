/*
 * Character renderer: two eyes (solid pills, or eyes with pupils,
 * catchlights, flat half-lids and round covers) and an optional tiny mouth as
 * plain LVGL objects aligned to the screen centre. Objects are touched only
 * when their integer geometry or colour changes, so LVGL invalidates just the
 * small areas that actually moved. Parts beyond the circle are hidden; parts
 * crossing it are clipped by the panel itself (the circle is the frame).
 *
 * Lids and covers are drawn in the background colour on top of the eye:
 * a flat lid gives Maomao's half-lidded look, a round cover comes down over
 * the eye and leaves a lower crescent (blink, happy, asleep).
 */
#include <math.h>
#include <string.h>
#include "mao_character_priv.h"

#define OFFSCREEN 185   /* |coord| beyond which a part is fully outside the circle */

static lv_obj_t *make_part(lv_obj_t *parent, uint32_t color)
{
    /* Creation order is draw order. */
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) {
        return NULL;
    }
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_align(o, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_size(o, 2, 2);
    return o;
}

static void hide_part(lv_obj_t *o, mao_box_t *last)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    *last = (mao_box_t) { .x = INT16_MIN, .hidden = true };
}

/* Filled triangle in object space. */
static void tri(lv_layer_t *layer, const lv_area_t *a, lv_point_precise_t p0, lv_point_precise_t p1,
                lv_point_precise_t p2, uint32_t color)
{
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.p[0] = (lv_point_precise_t) { a->x1 + p0.x, a->y1 + p0.y };
    t.p[1] = (lv_point_precise_t) { a->x1 + p1.x, a->y1 + p1.y };
    t.p[2] = (lv_point_precise_t) { a->x1 + p2.x, a->y1 + p2.y };
    t.color = lv_color_hex(color);
    t.opa = LV_OPA_COVER;
    lv_draw_triangle(layer, &t);
}

/* Four-point star filling the object. */
static void star_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    const int32_t cx = w / 2, cy = h / 2, t = w / 7 + 1;
    const lv_point_precise_t c = { cx, cy };
    const lv_point_precise_t top = { cx, 0 }, bot = { cx, h - 1 }, lft = { 0, cy }, rgt = { w - 1, cy };
    const lv_point_precise_t nl = { cx - t, cy }, nr = { cx + t, cy }, nu = { cx, cy - t }, nd = { cx, cy + t };
    tri(layer, &a, top, nl, c, MAO_STAR_COLOR);
    tri(layer, &a, top, nr, c, MAO_STAR_COLOR);
    tri(layer, &a, bot, nl, c, MAO_STAR_COLOR);
    tri(layer, &a, bot, nr, c, MAO_STAR_COLOR);
    tri(layer, &a, lft, nu, c, MAO_STAR_COLOR);
    tri(layer, &a, lft, nd, c, MAO_STAR_COLOR);
    tri(layer, &a, rgt, nu, c, MAO_STAR_COLOR);
    tri(layer, &a, rgt, nd, c, MAO_STAR_COLOR);
}

static lv_obj_t *make_custom(lv_obj_t *parent, lv_event_cb_t cb, void *user)
{
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) {
        return NULL;
    }
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, 2, 2);
    lv_obj_add_event_cb(o, cb, LV_EVENT_DRAW_MAIN, user);
    return o;
}

void mao_char_draw_hide(mao_char_draw_t *d)
{
    for (int i = 0; i < 2; i++) {
        hide_part(d->eye[i], &d->last_eye[i]);
        hide_part(d->pupil[i], &d->last_pupil[i]);
        hide_part(d->lid[i], &d->last_lid[i]);
        hide_part(d->cover[i], &d->last_cover[i]);
        hide_part(d->lower[i], &d->last_lower[i]);
        hide_part(d->core[i], &d->last_core[i]);
    }
    for (int i = 0; i < 4; i++) {
        hide_part(d->shine[i], &d->last_shine[i]);
    }
    hide_part(d->mouth, &d->last_mouth);
    for (int i = 0; i < 2; i++) {
        hide_part(d->star[i], &d->last_star[i]);
    }
}

esp_err_t mao_char_draw_create(mao_char_draw_t *d, lv_obj_t *parent)
{
    bool ok = true;
    for (int i = 0; i < 2; i++) {
        ok &= (d->eye[i] = make_part(parent, MAO_EYE_COLOR)) != NULL;
    }
    for (int i = 0; i < 2; i++) {
        ok &= (d->pupil[i] = make_part(parent, 0x000000)) != NULL;   /* iris */
    }
    for (int i = 0; i < 2; i++) {
        ok &= (d->core[i] = make_part(parent, 0x000000)) != NULL;    /* pupil inside the iris */
    }
    for (int i = 0; i < 4; i++) {
        ok &= (d->shine[i] = make_part(parent, (i & 1) ? MAO_SHINE2_COLOR : MAO_SHINE_COLOR)) != NULL;
    }
    for (int i = 0; i < 2; i++) {
        ok &= (d->lid[i] = make_part(parent, MAO_LID_COLOR)) != NULL;
        if (d->lid[i]) {
            lv_obj_set_style_radius(d->lid[i], MAO_LID_RADIUS, 0);
        }
    }
    for (int i = 0; i < 2; i++) {
        ok &= (d->cover[i] = make_part(parent, MAO_LID_COLOR)) != NULL;
    }
    for (int i = 0; i < 2; i++) {
        ok &= (d->lower[i] = make_part(parent, MAO_LID_COLOR)) != NULL;
    }
    ok &= (d->mouth = make_part(parent, MAO_EYE_COLOR)) != NULL;
    /* Star pupils: custom-drawn filled shapes. */
    for (int i = 0; i < 2; i++) {
        ok &= (d->star[i] = make_custom(parent, star_draw_cb, NULL)) != NULL;
    }
    if (!ok) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_style_bg_opa(d->mouth, LV_OPA_TRANSP, 0);   /* ring */
    lv_obj_set_style_border_width(d->mouth, 2, 0);
    lv_obj_set_style_border_color(d->mouth, lv_color_hex(MAO_EYE_COLOR), 0);
    d->last_mouth_kind = MAO_MOUTH_NONE;
    d->last_color = MAO_EYE_COLOR;
    d->last_pupil_color = 0;
    mao_char_draw_hide(d);
    return ESP_OK;
}

static int16_t px(float v)
{
    return (int16_t)lrintf(v);
}

static void apply_box(lv_obj_t *o, mao_box_t *last, mao_box_t next)
{
    next.hidden = next.hidden || next.x > OFFSCREEN || next.x < -OFFSCREEN ||
                  next.y > OFFSCREEN || next.y < -OFFSCREEN || next.w < 1 || next.h < 1;
    if (next.hidden) {
        if (!last->hidden) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
        *last = (mao_box_t) { .x = INT16_MIN, .hidden = true };   /* re-apply on return */
        return;
    }
    if (next.w != last->w || next.h != last->h) {
        lv_obj_set_size(o, next.w, next.h);
    }
    if (next.x != last->x || next.y != last->y) {
        lv_obj_set_pos(o, next.x, next.y);
    }
    if (last->hidden) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    *last = next;
}

static void set_color(lv_obj_t *const *objs, int n, uint32_t *last, uint32_t color)
{
    color &= 0xF8FCF8;   /* quantise so tint animations don't redraw invisible changes */
    if (color == *last) {
        return;
    }
    *last = color;
    for (int i = 0; i < n; i++) {
        lv_obj_set_style_bg_color(objs[i], lv_color_hex(color), 0);
    }
}

void mao_char_draw_apply(mao_char_draw_t *d, const mao_pose_t *p)
{
    const uint32_t before = d->last_color;
    set_color(d->eye, 2, &d->last_color, p->color);
    if (d->last_color != before) {
        lv_obj_set_style_border_color(d->mouth, lv_color_hex(d->last_color), 0);
    }
    apply_box(d->eye[0], &d->last_eye[0],
              (mao_box_t) { .x = px(p->lx), .y = px(p->ly), .w = px(p->lw), .h = px(p->lh) });
    apply_box(d->eye[1], &d->last_eye[1],
              (mao_box_t) { .x = px(p->rx), .y = px(p->ry), .w = px(p->rw), .h = px(p->rh) });

    const bool rig = p->has_pupils;
    if (rig) {
        set_color(d->pupil, 2, &d->last_pupil_color, p->pupil_color);
        set_color(d->core, 2, &d->last_core_color, p->core_color);
    }
    const float pcx[2] = { p->plx, p->prx }, pcy[2] = { p->ply, p->pry }, ph[2] = { p->plh, p->prh };
    const float ex[2] = { p->lx, p->rx }, ey[2] = { p->ly, p->ry }, eh[2] = { p->lh, p->rh };
    const float ew[2] = { p->lw, p->rw }, lid[2] = { p->lid_l, p->lid_r };
    const float cov[2] = { p->cover_l, p->cover_r };
    for (int i = 0; i < 2; i++) {
        const bool pupil_on = rig && ph[i] >= MAO_PUPIL_MIN_H;
        apply_box(d->pupil[i], &d->last_pupil[i],
                  (mao_box_t) { .x = px(pcx[i]), .y = px(pcy[i]), .w = px(p->pw), .h = px(ph[i]),
                                .hidden = !pupil_on });
        const float chh = i ? p->ch_r : p->ch_l;
        apply_box(d->core[i], &d->last_core[i],
                  (mao_box_t) { .x = px(pcx[i]), .y = px(pcy[i] - ph[i] * 0.04f), .w = px(p->cw), .h = px(chh),
                                .hidden = !pupil_on || p->cw < 2.0f || chh < 3.0f });
        /* Catchlights: a big one and a small one, only on a round-enough pupil. */
        const bool shine_on = pupil_on && ph[i] >= p->pw * 0.6f;
        const float s = p->ss, s2 = s * MAO_SHINE_SMALL;
        apply_box(d->shine[i * 2], &d->last_shine[i * 2],
                  (mao_box_t) { .x = px(p->sx[i]), .y = px(p->sy[i]), .w = px(s), .h = px(s),
                                .hidden = !shine_on || s < 3.0f });
        apply_box(d->shine[i * 2 + 1], &d->last_shine[i * 2 + 1],
                  (mao_box_t) { .x = px(pcx[i] - p->pw * 0.18f), .y = px(pcy[i] + ph[i] * 0.22f),
                                .w = px(s2), .h = px(s2), .hidden = !shine_on || s2 < 3.0f });
        /* Flat lid: a background-coloured block whose lower edge sits at
         * lid[i]; it reaches just above the eye's top. */
        const float top = ey[i] - eh[i] * 0.5f - 2.0f;
        const float depth = lid[i] - top;
        const int16_t lh = px(depth);
        apply_box(d->lid[i], &d->last_lid[i],
                  (mao_box_t) { .x = px(ex[i]), .y = px(top + depth * 0.5f), .w = px(ew[i] + 8.0f), .h = lh,
                                .hidden = !rig || lh < 5 });
        /* Lower lid, eye-sized, rising from below: smiling eyes. */
        const float low[2] = { p->low_l, p->low_r };
        apply_box(d->lower[i], &d->last_lower[i],
                  (mao_box_t) { .x = px(ex[i]), .y = px(low[i]), .w = px(ew[i] + 4.0f), .h = px(eh[i] + 2.0f),
                                .hidden = !rig || !p->low_on });
        /* Round cover, eye-sized, sliding down over the eye. */
        apply_box(d->cover[i], &d->last_cover[i],
                  (mao_box_t) { .x = px(ex[i]), .y = px(cov[i]), .w = px(ew[i] + 4.0f), .h = px(eh[i] + 2.0f),
                                .hidden = !rig || !p->cover_on });
    }
    apply_box(d->mouth, &d->last_mouth,
              (mao_box_t) { .x = px(p->mx), .y = px(p->my), .w = 8, .h = 8,
                            .hidden = p->mouth == MAO_MOUTH_NONE });
}


/* Star pupils (greed), centred on each iris. */
void mao_char_draw_star(mao_char_draw_t *d, const mao_pose_t *p)
{
    const float cx = LV_HOR_RES / 2.0f, cy = LV_VER_RES / 2.0f;
    for (int i = 0; i < 2; i++) {
        /* Star pupil, centred on the iris (screen-space top-left positioning). */
        const float pcx = i ? p->prx : p->plx, pcy = i ? p->pry : p->ply, ph = i ? p->prh : p->plh;
        const float st = p->star;
        if (!p->has_pupils || st < 4.0f || ph < MAO_PUPIL_MIN_H) {
            if (!d->last_star[i].hidden) {
                hide_part(d->star[i], &d->last_star[i]);
            }
        } else {
            const mao_box_t b = { .x = px(cx + pcx - st * 0.5f), .y = px(cy + pcy - st * 0.5f), .w = px(st), .h = px(st) };
            if (d->last_star[i].hidden || b.x != d->last_star[i].x || b.y != d->last_star[i].y || b.w != d->last_star[i].w) {
                lv_obj_set_pos(d->star[i], b.x, b.y);
                lv_obj_set_size(d->star[i], b.w, b.h);
                lv_obj_remove_flag(d->star[i], LV_OBJ_FLAG_HIDDEN);
                d->last_star[i] = b;
            }
        }
    }
}
