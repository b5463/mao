/*
 * Character renderer: two pill-shaped eyes and an optional tiny mouth, as
 * plain LVGL objects aligned to the screen centre. Objects are only touched
 * when their integer geometry changes, so LVGL invalidates just the small
 * areas that actually moved.
 */
#include <math.h>
#include "mao_character_priv.h"

#define COLOR_EYE      0xF1ECE2   /* warm off-white */
#define COLOR_MOUTH    0xC9C3B8
#define OFFSCREEN      150        /* |coord| beyond which an object is hidden */

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

esp_err_t mao_char_draw_create(mao_char_draw_t *d, lv_obj_t *parent)
{
    d->eye[0] = make_part(parent, COLOR_EYE);
    d->eye[1] = make_part(parent, COLOR_EYE);
    d->mouth = make_part(parent, COLOR_MOUTH);
    if (!d->eye[0] || !d->eye[1] || !d->mouth) {
        return ESP_ERR_NO_MEM;
    }
    d->last_mouth_kind = MAO_MOUTH_NONE;
    mao_char_draw_hide(d);
    return ESP_OK;
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
        /* Forget geometry so it is re-applied when the part reappears. */
        *last = (mao_box_t) { .x = INT16_MIN, .hidden = true };
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
    const int16_t w = px(p->eye_w);
    const int16_t h = px(p->eye_h);
    apply_box(d->eye[0], &d->last_eye[0],
              (mao_box_t) { .x = px(p->left_x), .y = px(p->left_y), .w = w, .h = h });
    apply_box(d->eye[1], &d->last_eye[1],
              (mao_box_t) { .x = px(p->right_x), .y = px(p->right_y), .w = w, .h = h });

    if (p->mouth != d->last_mouth_kind) {
        if (p->mouth == MAO_MOUTH_O) {
            /* Ring: transparent fill with a border. */
            lv_obj_set_style_bg_opa(d->mouth, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(d->mouth, 2, 0);
            lv_obj_set_style_border_color(d->mouth, lv_color_hex(COLOR_MOUTH), 0);
        } else {
            lv_obj_set_style_bg_opa(d->mouth, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(d->mouth, 0, 0);
        }
        d->last_mouth_kind = p->mouth;
        d->last_mouth.w = -1;   /* force size refresh */
    }
    mao_box_t mouth = {
        .x = px(p->mouth_x),
        .y = px(p->mouth_y),
        .w = p->mouth == MAO_MOUTH_O ? 10 : 12,
        .h = p->mouth == MAO_MOUTH_O ? 10 : 3,
        .hidden = p->mouth == MAO_MOUTH_NONE,
    };
    apply_box(d->mouth, &d->last_mouth, mouth);
}
