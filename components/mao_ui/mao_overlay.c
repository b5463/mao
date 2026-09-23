/*
 * Text layers above HOME: the menu shell, placeholder pages and the first
 * encounter prompt. Typography, spacing and motion only: no boxes, icons or
 * selection rectangles.
 *
 * The menu is laid out from two floats: `pos` (continuous scroll position, a
 * spring chasing the selected index) and `presence` (0 = gone, 1 = shown).
 * A 30 Hz layout timer runs only while either is moving.
 */
#include <math.h>
#include "mao_ui_priv.h"
#include "mao_ui.h"

#define MENU_SPACING      40.0f
#define MENU_ENTER_DY     26.0f
#define MENU_TICK_MS      33
#define MENU_VISIBLE_Y    100       /* rows beyond this are hidden (round screen) */

#define SPRING_K          240.0f
#define SPRING_C          (2.0f * 0.78f * 15.49f)   /* zeta 0.78, sqrt(240) = 15.49 */

typedef struct {
    lv_obj_t *label[8];
    int count;
    float pos;          /* continuous selection position */
    float vel;
    float target;
    float presence;
    int16_t last_y[8];
    lv_opa_t last_opa[8];
    lv_timer_t *timer;
    uint32_t last_ms;
} menu_t;

typedef struct {
    lv_obj_t *title;
    lv_obj_t *note;
    float presence;
    lv_timer_t *timer;
} page_t;

typedef struct {
    lv_obj_t *word;
    lv_obj_t *prompt;
    float presence;
    lv_timer_t *timer;
} intro_t;

static menu_t s_menu;
static page_t s_page;
static intro_t s_intro;

static lv_obj_t *make_text(lv_obj_t *scr, const lv_font_t *font, uint32_t color, int32_t letter_space)
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

static void set_text_state(lv_obj_t *o, int16_t y, lv_opa_t opa, int16_t *last_y, lv_opa_t *last_opa)
{
    const bool hide = opa < 4;
    if (hide) {
        if (*last_opa >= 4) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
        *last_opa = 0;
        return;
    }
    if (*last_opa < 4) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    if (y != *last_y) {
        lv_obj_set_y(o, y);
        *last_y = y;
    }
    if (opa != *last_opa) {
        lv_obj_set_style_text_opa(o, opa, 0);
        *last_opa = opa;
    }
}

/* ---------------------------------------------------------------------- */
/* Menu                                                                   */
/* ---------------------------------------------------------------------- */

static void menu_layout(void)
{
    const float p = s_menu.presence;
    const float spread = 0.45f + 0.55f * p;          /* rows gather to the centre when leaving */
    const float enter_dy = (1.0f - p) * MENU_ENTER_DY;
    for (int i = 0; i < s_menu.count; i++) {
        const float d = (float)i - s_menu.pos;
        const float y = d * MENU_SPACING * spread + enter_dy;
        /* Selected row full brightness, neighbours recede with distance. */
        float o = 255.0f - fabsf(d) * 150.0f;
        if (o < 60.0f) {
            o = 60.0f;
        }
        if (fabsf(y) > MENU_VISIBLE_Y) {
            o = 0.0f;
        }
        set_text_state(s_menu.label[i], (int16_t)lrintf(y), (lv_opa_t)(o * p),
                       &s_menu.last_y[i], &s_menu.last_opa[i]);
    }
}

static void menu_tick(lv_timer_t *t)
{
    const uint32_t now = lv_tick_get();
    float dt = (float)(now - s_menu.last_ms) / 1000.0f;
    s_menu.last_ms = now;
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    /* Two sub-steps of a damped spring towards the selected index. */
    for (int k = 0; k < 2; k++) {
        const float h = dt / 2.0f;
        const float a = SPRING_K * (s_menu.target - s_menu.pos) - SPRING_C * s_menu.vel;
        s_menu.vel += a * h;
        s_menu.pos += s_menu.vel * h;
    }
    menu_layout();

    const bool settled = fabsf(s_menu.target - s_menu.pos) < 0.002f && fabsf(s_menu.vel) < 0.01f;
    if (settled && !mao_ui_anim_running(&s_menu.presence)) {
        s_menu.pos = s_menu.target;
        menu_layout();
        lv_timer_pause(t);
    }
}

void mao_overlay_menu(bool show, int index, uint32_t delay_ms)
{
    if (show) {
        s_menu.target = (float)index;
        if (s_menu.presence < 0.01f) {
            s_menu.pos = (float)index;   /* appear already on the selection */
            s_menu.vel = 0.0f;
        }
        s_menu.last_ms = lv_tick_get();
        mao_ui_anim_float(&s_menu.presence, 1.0f, MAO_UI_T_ENTER, delay_ms, true, s_menu.timer);
    } else {
        mao_ui_anim_float(&s_menu.presence, 0.0f, MAO_UI_T_LEAVE, delay_ms, false, s_menu.timer);
    }
}

void mao_overlay_menu_select(int index)
{
    s_menu.target = (float)index;
    s_menu.last_ms = lv_tick_get();
    lv_timer_resume(s_menu.timer);
}

void mao_overlay_menu_bump(int direction)
{
    /* Elastic nudge past the end; the spring pulls it back. */
    s_menu.vel += direction > 0 ? 2.2f : -2.2f;
    s_menu.last_ms = lv_tick_get();
    lv_timer_resume(s_menu.timer);
}

/* ---------------------------------------------------------------------- */
/* Placeholder page                                                       */
/* ---------------------------------------------------------------------- */

static int16_t s_page_last_y[2];
static lv_opa_t s_page_last_opa[2];

static void page_tick(lv_timer_t *t)
{
    const float p = s_page.presence;
    set_text_state(s_page.title, (int16_t)lrintf(-8.0f + (1.0f - p) * 14.0f), (lv_opa_t)(255.0f * p),
                   &s_page_last_y[0], &s_page_last_opa[0]);
    set_text_state(s_page.note, (int16_t)lrintf(26.0f + (1.0f - p) * 8.0f), (lv_opa_t)(150.0f * p),
                   &s_page_last_y[1], &s_page_last_opa[1]);
    if (!mao_ui_anim_running(&s_page.presence)) {
        lv_timer_pause(t);
    }
}

void mao_overlay_placeholder(bool show, const char *title, uint32_t delay_ms)
{
    if (show && title) {
        lv_label_set_text(s_page.title, title);
    }
    mao_ui_anim_float(&s_page.presence, show ? 1.0f : 0.0f,
                      show ? MAO_UI_T_ENTER : MAO_UI_T_LEAVE, delay_ms, show, s_page.timer);
}

/* ---------------------------------------------------------------------- */
/* First encounter                                                        */
/* ---------------------------------------------------------------------- */

static int16_t s_intro_last_y[2];
static lv_opa_t s_intro_last_opa[2];

static void intro_tick(lv_timer_t *t)
{
    const float p = s_intro.presence;
    /* "TURN" breathes gently while waiting; everything fades on exit. */
    const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf((float)lv_tick_get() / 1000.0f * 3.2f));
    set_text_state(s_intro.word, (int16_t)lrintf(-14.0f - (1.0f - p) * 10.0f), (lv_opa_t)(255.0f * p),
                   &s_intro_last_y[0], &s_intro_last_opa[0]);
    set_text_state(s_intro.prompt, 26, (lv_opa_t)(170.0f * p * pulse),
                   &s_intro_last_y[1], &s_intro_last_opa[1]);
    if (p < 0.01f && !mao_ui_anim_running(&s_intro.presence)) {
        lv_timer_pause(t);
    }
}

void mao_overlay_intro(bool show)
{
    mao_ui_anim_float(&s_intro.presence, show ? 1.0f : 0.0f,
                      show ? MAO_UI_T_ENTER : MAO_UI_T_LEAVE, 0, show, s_intro.timer);
}

/* ---------------------------------------------------------------------- */

void mao_overlay_create(lv_obj_t *scr, bool intro_visible)
{
    s_menu.count = mao_ui_menu_count();
    for (int i = 0; i < s_menu.count; i++) {
        s_menu.label[i] = make_text(scr, &lv_font_montserrat_20, MAO_UI_FG, 3);
        lv_label_set_text(s_menu.label[i], mao_ui_menu_label(i));
        s_menu.last_y[i] = INT16_MIN;
    }
    s_menu.timer = lv_timer_create(menu_tick, MENU_TICK_MS, NULL);
    lv_timer_pause(s_menu.timer);

    s_page.title = make_text(scr, &lv_font_montserrat_28, MAO_UI_FG, 4);
    s_page.note = make_text(scr, &lv_font_montserrat_14, MAO_UI_DIM, 4);
    lv_label_set_text(s_page.note, "SOON");
    s_page_last_y[0] = s_page_last_y[1] = INT16_MIN;
    s_page.timer = lv_timer_create(page_tick, MENU_TICK_MS, NULL);
    lv_timer_pause(s_page.timer);

    s_intro.word = make_text(scr, &lv_font_montserrat_28, MAO_UI_FG, 6);
    lv_label_set_text(s_intro.word, "MAO");
    s_intro.prompt = make_text(scr, &lv_font_montserrat_14, MAO_UI_FG, 5);
    lv_label_set_text(s_intro.prompt, "TURN");
    s_intro_last_y[0] = s_intro_last_y[1] = INT16_MIN;
    /* The waiting pulse is slow; 15 Hz is plenty and halves its render cost. */
    s_intro.timer = lv_timer_create(intro_tick, 66, NULL);
    if (intro_visible) {
        /* Fully visible in the very first frame (no fade-in at boot). */
        s_intro.presence = 1.0f;
        intro_tick(s_intro.timer);
        lv_timer_resume(s_intro.timer);
    } else {
        lv_timer_pause(s_intro.timer);
    }
}
