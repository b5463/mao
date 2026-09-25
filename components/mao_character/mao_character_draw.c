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
#include <stdlib.h>
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

/* Fill between a polyline (n points, x increasing) and a horizontal line
 * y_line, one 1 px column at a time: adjacent anti-aliased triangles would
 * leave hairline seams. below = the region under the polyline. */
static void fill_columns(lv_layer_t *layer, const lv_area_t *a, const lv_point_precise_t *p, int n, int32_t y_line,
                         bool below)
{
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_color = lv_color_hex(MAO_LID_COLOR);
    int k = 0;
    int32_t run_x = 0, run_y = INT32_MIN;
    const int32_t x_end = (int32_t)p[n - 1].x;
    for (int32_t x = (int32_t)p[0].x; x <= x_end + 1; x++) {
        int32_t y = INT32_MIN;
        if (x <= x_end) {
            while (k < n - 2 && x > p[k + 1].x) {
                k++;
            }
            const float x0 = p[k].x, x1 = p[k + 1].x;
            const float u = x1 > x0 ? ((float)x - x0) / (x1 - x0) : 0.0f;
            y = (int32_t)lrintf(p[k].y + (p[k + 1].y - p[k].y) * (u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u)));
        }
        if (y != run_y) {
            /* Flush the run of equal-height columns as one rectangle. */
            if (run_y != INT32_MIN) {
                const lv_area_t col = { a->x1 + run_x, a->y1 + (below ? run_y : y_line), a->x1 + x - 1,
                                        a->y1 + (below ? y_line : run_y) };
                if (col.y2 >= col.y1) {
                    lv_draw_rect(layer, &r, &col);
                }
            }
            run_x = x;
            run_y = y;
        }
    }
}

/* Anti-aliased edge: the column fill is exact but stair-stepped on slopes
 * and curves; a thin line in the lid colour along the edge blends each step
 * into the eye white. */
static void aa_edge(lv_layer_t *layer, const lv_area_t *a, const lv_point_precise_t *p, int n)
{
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = lv_color_hex(MAO_LID_COLOR);
    l.width = 2;
    l.round_start = 1;
    l.round_end = 1;
    for (int k = 0; k < n - 1; k++) {
        l.p1 = (lv_point_precise_t) { a->x1 + p[k].x, a->y1 + p[k].y };
        l.p2 = (lv_point_precise_t) { a->x1 + p[k + 1].x, a->y1 + p[k + 1].y };
        lv_draw_line(layer, &l);
    }
}

/* Upper lid (pts: top-left, top-right, bottom-right, bottom-left): fill
 * from the top down to the sloped edge bottom-left -> bottom-right. */
static void lid_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    const lv_point_precise_t *p = lv_event_get_user_data(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const lv_point_precise_t edge[2] = { p[3], p[2] };
    fill_columns(lv_event_get_layer(e), &a, edge, 2, (int32_t)p[0].y, false);
    if (abs((int)(edge[0].y - edge[1].y)) >= 3) {
        aa_edge(lv_event_get_layer(e), &a, edge, 2);   /* only visibly sloped lids step */
    }
}

/* Lower lid: pts 0..11 the arc (left to right), 12 / 13 the bottom corners. */
static void low_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    const lv_point_precise_t *p = lv_event_get_user_data(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    fill_columns(lv_event_get_layer(e), &a, p, 12, (int32_t)p[12].y, true);
    /* Every other arc point: 6 segments are smooth enough and half the cost. */
    const lv_point_precise_t arc[7] = { p[0], p[2], p[4], p[6], p[8], p[10], p[11] };
    aa_edge(lv_event_get_layer(e), &a, arc, 7);
}

/* Place a custom-drawn object over the screen-centred box and store its
 * points relative to the box; invalidates only when something changed. */
static void place_custom(lv_obj_t *o, mao_box_t *last, lv_point_precise_t *dst, const float (*pts)[2], int n,
                         bool hidden)
{
    if (hidden) {
        if (!last->hidden) {
            hide_part(o, last);
        }
        return;
    }
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int k = 0; k < n; k++) {
        minx = fminf(minx, pts[k][0]); maxx = fmaxf(maxx, pts[k][0]);
        miny = fminf(miny, pts[k][1]); maxy = fmaxf(maxy, pts[k][1]);
    }
    const float cx = LV_HOR_RES / 2.0f, cy = LV_VER_RES / 2.0f;
    const mao_box_t box = { .x = (int16_t)lrintf(cx + minx) - 1, .y = (int16_t)lrintf(cy + miny) - 1,
                            .w = (int16_t)lrintf(maxx - minx) + 3, .h = (int16_t)lrintf(maxy - miny) + 3 };
    bool moved = false;
    for (int k = 0; k < n; k++) {
        const lv_point_precise_t q = { (lv_value_precise_t)lrintf(cx + pts[k][0]) - box.x,
                                       (lv_value_precise_t)lrintf(cy + pts[k][1]) - box.y };
        if (q.x != dst[k].x || q.y != dst[k].y) {
            dst[k] = q;
            moved = true;
        }
    }
    if (last->hidden || box.x != last->x || box.y != last->y || box.w != last->w || box.h != last->h) {
        lv_obj_set_pos(o, box.x, box.y);
        lv_obj_set_size(o, box.w, box.h);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        *last = box;
    } else if (moved) {
        lv_obj_invalidate(o);
    }
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

void mao_char_draw_marks(mao_char_draw_t *d, int dx, int dy, bool show)
{
    /* Three short radial ticks just inside the rim, fanned out around the
     * impact point. Very restrained: no cracks, no particles. */
    static const int8_t kFan[3] = { -17, 0, 17 };
    static const int8_t kLen[3] = { 7, 11, 7 };
    for (int i = 0; i < 3; i++) {
        if (!d->mark[i]) {
            continue;
        }
        if (!show) {
            lv_obj_add_flag(d->mark[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int in = 104 - (i == 1 ? 2 : 0);   /* the middle tick sits a touch deeper */
        if (dx) {
            lv_obj_set_size(d->mark[i], kLen[i], 2);
            lv_obj_set_pos(d->mark[i], dx * in, kFan[i]);
        } else {
            lv_obj_set_size(d->mark[i], 2, kLen[i]);
            lv_obj_set_pos(d->mark[i], kFan[i], dy * in);
        }
        lv_obj_set_style_bg_opa(d->mark[i], i == 1 ? LV_OPA_90 : LV_OPA_60, 0);
        lv_obj_remove_flag(d->mark[i], LV_OBJ_FLAG_HIDDEN);
    }
}

esp_err_t mao_char_draw_create(mao_char_draw_t *d, lv_obj_t *parent)
{
    bool ok = true;
    /* Each eye is its own stack (white, iris, pupil, catchlights, star, lids,
     * covers), so the nearer eye can be raised over the farther one. */
    for (int i = 0; i < 2; i++) {
        ok &= (d->eye[i] = make_part(parent, MAO_EYE_COLOR)) != NULL;
        ok &= (d->pupil[i] = make_part(parent, 0x000000)) != NULL;   /* iris */
        ok &= (d->core[i] = make_part(parent, 0x000000)) != NULL;    /* pupil inside the iris */
        ok &= (d->shine[i * 2] = make_part(parent, MAO_SHINE_COLOR)) != NULL;
        ok &= (d->shine[i * 2 + 1] = make_part(parent, MAO_SHINE2_COLOR)) != NULL;
        ok &= (d->star[i] = make_custom(parent, star_draw_cb, NULL)) != NULL;
        ok &= (d->lid[i] = make_custom(parent, lid_draw_cb, &d->lid_pts[i][0])) != NULL;
        ok &= (d->lower[i] = make_custom(parent, low_draw_cb, &d->low_pts[i][0])) != NULL;
        ok &= (d->cover[i] = make_part(parent, MAO_LID_COLOR)) != NULL;
    }
    ok &= (d->mouth = make_part(parent, MAO_EYE_COLOR)) != NULL;
    for (int i = 0; i < 3; i++) {
        ok &= (d->mark[i] = make_part(parent, MAO_EYE_COLOR)) != NULL;
        if (d->mark[i]) {
            lv_obj_set_style_radius(d->mark[i], 1, 0);
            lv_obj_add_flag(d->mark[i], LV_OBJ_FLAG_HIDDEN);
        }
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
    d->front = 1;   /* creation order: eye 1 on top */
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

static void raise_eye(mao_char_draw_t *d, int i)
{
    lv_obj_t *const parts[] = { d->eye[i], d->pupil[i], d->core[i], d->shine[i * 2], d->shine[i * 2 + 1],
                                d->star[i], d->lid[i], d->lower[i], d->cover[i] };
    for (size_t k = 0; k < sizeof(parts) / sizeof(parts[0]); k++) {
        lv_obj_move_foreground(parts[k]);
    }
    lv_obj_move_foreground(d->mouth);
    d->front = (uint8_t)i;
}

void mao_char_draw_apply(mao_char_draw_t *d, const mao_pose_t *p)
{
    if (p->front != d->front) {
        raise_eye(d, p->front);
    }
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
        const float f = p->fore[i];
        const bool pupil_on = rig && ph[i] >= MAO_PUPIL_MIN_H && f > 0.0f;
        const float pwi = p->pw * f;
        apply_box(d->pupil[i], &d->last_pupil[i],
                  (mao_box_t) { .x = px(pcx[i]), .y = px(pcy[i]), .w = px(pwi), .h = px(ph[i]),
                                .hidden = !pupil_on });
        const float chh = i ? p->ch_r : p->ch_l;
        apply_box(d->core[i], &d->last_core[i],
                  (mao_box_t) { .x = px(pcx[i]), .y = px(pcy[i] - ph[i] * 0.04f), .w = px(p->cw * f), .h = px(chh),
                                .hidden = !pupil_on || p->cw * f < 2.0f || chh < 3.0f });
        /* Catchlights: a big one and a small one, only on a round-enough pupil. */
        const bool shine_on = pupil_on && ph[i] >= p->pw * 0.6f && f > 0.35f;
        const float s = p->ss, s2 = s * MAO_SHINE_SMALL;
        apply_box(d->shine[i * 2], &d->last_shine[i * 2],
                  (mao_box_t) { .x = px(p->sx[i]), .y = px(p->sy[i]), .w = px(s * f), .h = px(s),
                                .hidden = !shine_on || s < 3.0f });
        apply_box(d->shine[i * 2 + 1], &d->last_shine[i * 2 + 1],
                  (mao_box_t) { .x = px(pcx[i] - pwi * 0.18f), .y = px(pcy[i] + ph[i] * 0.22f),
                                .w = px(s2 * f), .h = px(s2), .hidden = !shine_on || s2 < 3.0f });
        /* Upper lid: its lower edge sits at lid[i], sloped by lid_tilt (inner
         * end lower when positive); it reaches just above the eyeball. */
        {
            const float top = ey[i] - eh[i] * 0.5f - 3.0f;
            const float hw = ew[i] * 0.5f + 4.0f;
            const float inner = i ? -1.0f : 1.0f;              /* left eye: inner side is +x */
            const float t = p->lid_tilt[i];
            const float y_in = fmaxf(top, lid[i] + t * 0.5f), y_out = fmaxf(top, lid[i] - t * 0.5f);
            const float xl = ex[i] - hw, xr = ex[i] + hw;
            const float yl = inner > 0 ? y_out : y_in, yr = inner > 0 ? y_in : y_out;
            const float q[4][2] = { { xl, top }, { xr, top }, { xr, yr }, { xl, yl } };
            place_custom(d->lid[i], &d->last_lid[i], &d->lid_pts[i][0], q, 4,
                         !rig || ew[i] < 1.0f || fmaxf(yl, yr) - top < 4.0f);
        }
        /* Lower lid: its upper edge runs parallel to the eye's own round top,
         * `band` below it. A light smile lifts the bottom centre like a cheek;
         * a full one leaves an even thin arch - "^ ^" - with the iris hidden. */
        {
            const float low[2] = { p->low_l, p->low_r };
            /* low[] is the centre of an eye-sized lid; its top edge is the smile line. */
            const float lift = ey[i] + eh[i] * 0.5f - (low[i] - (eh[i] + 2.0f) * 0.5f);   /* smile, px */
            const float band = fmaxf(eh[i] - lift, 7.0f);
            const float top = ey[i] - eh[i] * 0.5f, bottom = ey[i] + eh[i] * 0.5f + 3.0f;
            const float r = fminf(ew[i], eh[i]) * 0.5f, flat = ew[i] * 0.5f - r;
            const float hw = ew[i] * 0.5f + 3.0f;
            float q[14][2];
            for (int k = 0; k < 12; k++) {
                const float u = -1.0f + 2.0f * (float)k / 11.0f;
                const float dx = fminf(fabsf(u) * hw, ew[i] * 0.5f);
                const float over = dx - flat;
                const float edge = over > 0.0f ? top + r - sqrtf(fmaxf(r * r - over * over, 0.0f)) : top;
                q[k][0] = ex[i] + u * hw;
                /* Light smile: a gentle wide cheek curve; full smile: parallel to
                 * the top, an even arch. */
                const float cheek = bottom - 3.0f - lift * (1.0f - 0.22f * u * u);
                const float w = fminf(fmaxf((lift / eh[i] - 0.5f) / 0.35f, 0.0f), 1.0f);
                q[k][1] = fminf(cheek + (edge + band - cheek) * w * w * (3.0f - 2.0f * w), bottom);
            }
            q[12][0] = ex[i] + hw; q[12][1] = bottom;
            q[13][0] = ex[i] - hw; q[13][1] = bottom;
            place_custom(d->lower[i], &d->last_lower[i], &d->low_pts[i][0], q, 14,
                         !rig || !p->low_on || ew[i] < 1.0f || lift < 3.0f);
        }
        /* Round cover, eye-sized, sliding down over the eye. */
        apply_box(d->cover[i], &d->last_cover[i],
                  (mao_box_t) { .x = px(ex[i]), .y = px(cov[i]), .w = px(ew[i] + 4.0f), .h = px(eh[i] + 2.0f),
                                .hidden = !rig || !p->cover_on || ew[i] < 1.0f });
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
        if (!p->has_pupils || st < 4.0f || ph < MAO_PUPIL_MIN_H || p->fore[i] < 0.5f) {
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
