/*
 * Shared text helpers and the typographic list used by the menu shell and
 * the DEVICES view: rows of words laid out from a continuous scroll position
 * (a spring chasing the selected index) and a presence value (0 gone .. 1
 * shown). The selected row is brightest, neighbours recede. A 30 Hz layout
 * timer runs only while something moves.
 */
#include <math.h>
#include <string.h>
#include "mao_ui_priv.h"

#define LIST_TICK_MS     33
#define LIST_ENTER_DY    26.0f
#define LIST_VISIBLE_Y   100   /* rows beyond this are hidden (round screen) */
#define SPRING_K         240.0f
#define SPRING_C         (2.0f * 0.78f * 15.49f)   /* zeta 0.78, sqrt(240) */

lv_obj_t *mao_ui_make_text(lv_obj_t *scr, const lv_font_t *font, uint32_t color, int32_t letter_space)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(l, letter_space, 0);
    lv_obj_set_style_text_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_align(l, LV_ALIGN_CENTER);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

void mao_ui_text_state(lv_obj_t *o, int16_t y, lv_opa_t opa, mao_ui_text_cache_t *c)
{
    if (opa < 4) {
        if (c->opa >= 4) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
        c->opa = 0;
        return;
    }
    if (c->opa < 4) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    if (y != c->y) {
        lv_obj_set_y(o, y);
        c->y = y;
    }
    if (opa != c->opa) {
        lv_obj_set_style_text_opa(o, opa, 0);
        c->opa = opa;
    }
}

/* ---------------------------------------------------------------------- */

static void list_layout(mao_ui_list_t *l)
{
    const float p = l->presence;
    const float spread = 0.45f + 0.55f * p;          /* rows gather to the centre when leaving */
    const float enter_dy = (1.0f - p) * LIST_ENTER_DY;
    for (int i = 0; i < l->capacity; i++) {
        float o = 0.0f;
        float y = 0.0f;
        if (i < l->count) {
            const float d = (float)i - l->pos;
            y = d * l->spacing * spread + enter_dy + (float)l->y_offset;
            o = 255.0f - fabsf(d) * 150.0f;
            if (o < 60.0f) {
                o = 60.0f;
            }
            o *= l->row_scale[i];
            if (fabsf(y) > LIST_VISIBLE_Y) {
                o = 0.0f;
            }
        }
        mao_ui_text_state(l->label[i], (int16_t)lrintf(y), (lv_opa_t)(o * p), &l->cache[i]);
    }
}

static void list_tick(lv_timer_t *t)
{
    mao_ui_list_t *l = lv_timer_get_user_data(t);
    const uint32_t now = lv_tick_get();
    float dt = (float)(now - l->last_ms) / 1000.0f;
    l->last_ms = now;
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    for (int k = 0; k < 2; k++) {
        const float h = dt / 2.0f;
        const float a = SPRING_K * (l->target - l->pos) - SPRING_C * l->vel;
        l->vel += a * h;
        l->pos += l->vel * h;
    }
    list_layout(l);
    const bool settled = fabsf(l->target - l->pos) < 0.002f && fabsf(l->vel) < 0.01f;
    if (settled && !mao_ui_anim_running(&l->presence)) {
        l->pos = l->target;
        list_layout(l);
        lv_timer_pause(t);
    }
}

static void list_wake(mao_ui_list_t *l)
{
    l->last_ms = lv_tick_get();
    lv_timer_resume(l->timer);
}

void mao_ui_list_create(mao_ui_list_t *l, lv_obj_t *scr, const lv_font_t *font, int capacity,
                        float spacing, int16_t y_offset)
{
    memset(l, 0, sizeof(*l));
    l->capacity = capacity > MAO_UI_LIST_MAX ? MAO_UI_LIST_MAX : capacity;
    l->spacing = spacing;
    l->y_offset = y_offset;
    for (int i = 0; i < l->capacity; i++) {
        l->label[i] = mao_ui_make_text(scr, font, MAO_UI_FG, 3);
        l->cache[i].y = INT16_MIN;
        l->row_scale[i] = 1.0f;
    }
    l->timer = lv_timer_create(list_tick, LIST_TICK_MS, l);
    lv_timer_pause(l->timer);
}

void mao_ui_list_set_items(mao_ui_list_t *l, const char *const *texts, const float *row_scale, int count)
{
    l->count = count > l->capacity ? l->capacity : count;
    for (int i = 0; i < l->count; i++) {
        if (strcmp(lv_label_get_text(l->label[i]), texts[i]) != 0) {
            lv_label_set_text(l->label[i], texts[i]);
        }
        l->row_scale[i] = row_scale ? row_scale[i] : 1.0f;
    }
    if (l->target > (float)(l->count - 1)) {
        l->target = l->count > 0 ? (float)(l->count - 1) : 0.0f;
    }
    list_wake(l);
}

void mao_ui_list_show(mao_ui_list_t *l, bool show, int index, uint32_t delay_ms)
{
    if (show) {
        l->target = (float)index;
        if (l->presence < 0.01f) {
            l->pos = (float)index;   /* appear already on the selection */
            l->vel = 0.0f;
        }
        l->last_ms = lv_tick_get();
        mao_ui_anim_float(&l->presence, 1.0f, MAO_UI_T_ENTER, delay_ms, true, l->timer);
    } else {
        mao_ui_anim_float(&l->presence, 0.0f, MAO_UI_T_LEAVE, delay_ms, false, l->timer);
    }
}

void mao_ui_list_select(mao_ui_list_t *l, int index)
{
    l->target = (float)index;
    list_wake(l);
}

void mao_ui_list_bump(mao_ui_list_t *l, int direction)
{
    /* Elastic nudge past the end; the spring pulls it back. */
    l->vel += direction > 0 ? 2.2f : -2.2f;
    list_wake(l);
}
