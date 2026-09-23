/*
 * Character renderer: two pill-shaped eyes and an optional tiny mouth as
 * plain LVGL objects aligned to the screen centre. Objects are touched only
 * when their integer geometry or colour changes, so LVGL invalidates just the
 * small areas that actually moved. Parts beyond the circle are hidden; parts
 * crossing it are clipped by the panel itself (the circle is the frame).
 */
#include <math.h>
#include "mao_character_priv.h"

#define OFFSCREEN 150   /* |coord| beyond which a part is fully outside the circle */

static lv_obj_t *make_part(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_align(o, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_size(o, 2, 2);
    return o;
}

void mao_char_draw_hide(mao_char_draw_t *d)
{
    for (int i = 0; i < 2; i++) {
        lv_obj_add_flag(d->eye[i], LV_OBJ_FLAG_HIDDEN);
        d->last_eye[i] = (mao_box_t) { .x = INT16_MIN, .hidden = true };
    }
    lv_obj_add_flag(d->mouth, LV_OBJ_FLAG_HIDDEN);
    d->last_mouth = (mao_box_t) { .x = INT16_MIN, .hidden = true };
}

esp_err_t mao_char_draw_create(mao_char_draw_t *d, lv_obj_t *parent)
{
    d->eye[0] = make_part(parent, MAO_EYE_COLOR);
    d->eye[1] = make_part(parent, MAO_EYE_COLOR);
    d->mouth = make_part(parent, MAO_EYE_COLOR);
    if (!d->eye[0] || !d->eye[1] || !d->mouth) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_style_bg_opa(d->mouth, LV_OPA_TRANSP, 0);   /* ring */
    lv_obj_set_style_border_width(d->mouth, 2, 0);
    lv_obj_set_style_border_color(d->mouth, lv_color_hex(MAO_EYE_COLOR), 0);
    d->last_mouth_kind = MAO_MOUTH_NONE;
    d->last_color = MAO_EYE_COLOR;
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
                  next.y > OFFSCREEN || next.y < -OFFSCREEN;
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

void mao_char_draw_apply(mao_char_draw_t *d, const mao_pose_t *p)
{
    /* Quantise colour so tint animations don't redraw on invisible changes. */
    const uint32_t color = p->color & 0xF8FCF8;
    if (color != d->last_color) {
        d->last_color = color;
        lv_obj_set_style_bg_color(d->eye[0], lv_color_hex(color), 0);
        lv_obj_set_style_bg_color(d->eye[1], lv_color_hex(color), 0);
        lv_obj_set_style_border_color(d->mouth, lv_color_hex(color), 0);
    }
    apply_box(d->eye[0], &d->last_eye[0],
              (mao_box_t) { .x = px(p->lx), .y = px(p->ly), .w = px(p->lw), .h = px(p->lh) });
    apply_box(d->eye[1], &d->last_eye[1],
              (mao_box_t) { .x = px(p->rx), .y = px(p->ry), .w = px(p->rw), .h = px(p->rh) });
    apply_box(d->mouth, &d->last_mouth,
              (mao_box_t) { .x = px(p->mx), .y = px(p->my), .w = 8, .h = 8,
                            .hidden = p->mouth == MAO_MOUTH_NONE });
}
