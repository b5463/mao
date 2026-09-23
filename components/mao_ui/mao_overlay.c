/*
 * Text layers above HOME: the menu shell, placeholder pages and the first
 * encounter prompt. Typography, spacing and motion only: no boxes, icons or
 * selection rectangles.
 */
#include <math.h>
#include "mao_ui_priv.h"
#include "mao_ui.h"

#define PAGE_TICK_MS   33
#define INTRO_TICK_MS  66   /* the waiting pulse is slow; 15 Hz is plenty */

typedef struct {
    lv_obj_t *title;
    lv_obj_t *note;
    mao_ui_text_cache_t cache[2];
    float presence;
    lv_timer_t *timer;
} page_t;

typedef struct {
    lv_obj_t *word;
    lv_obj_t *prompt;
    mao_ui_text_cache_t cache[2];
    float presence;
    lv_timer_t *timer;
} intro_t;

static mao_ui_list_t s_menu;
static page_t s_page;
static intro_t s_intro;

/* ---------------------------------------------------------------------- */
/* Menu                                                                   */
/* ---------------------------------------------------------------------- */

void mao_overlay_menu(bool show, int index, uint32_t delay_ms)
{
    mao_ui_list_show(&s_menu, show, index, delay_ms);
}

void mao_overlay_menu_select(int index)
{
    mao_ui_list_select(&s_menu, index);
}

void mao_overlay_menu_bump(int direction)
{
    mao_ui_list_bump(&s_menu, direction);
}

/* ---------------------------------------------------------------------- */
/* Placeholder page                                                       */
/* ---------------------------------------------------------------------- */

static void page_tick(lv_timer_t *t)
{
    const float p = s_page.presence;
    mao_ui_text_state(s_page.title, (int16_t)lrintf(-8.0f + (1.0f - p) * 14.0f), (lv_opa_t)(255.0f * p),
                      &s_page.cache[0]);
    mao_ui_text_state(s_page.note, (int16_t)lrintf(26.0f + (1.0f - p) * 8.0f), (lv_opa_t)(150.0f * p),
                      &s_page.cache[1]);
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

static void intro_tick(lv_timer_t *t)
{
    const float p = s_intro.presence;
    /* "TURN" breathes gently while waiting; everything fades on exit. */
    const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf((float)lv_tick_get() / 1000.0f * 3.2f));
    mao_ui_text_state(s_intro.word, (int16_t)lrintf(-14.0f - (1.0f - p) * 10.0f), (lv_opa_t)(255.0f * p),
                      &s_intro.cache[0]);
    mao_ui_text_state(s_intro.prompt, 26, (lv_opa_t)(170.0f * p * pulse), &s_intro.cache[1]);
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
    mao_ui_list_create(&s_menu, scr, &lv_font_montserrat_20, mao_ui_menu_count(), 40.0f, 0);
    const char *items[MAO_UI_LIST_MAX];
    for (int i = 0; i < mao_ui_menu_count(); i++) {
        items[i] = mao_ui_menu_label(i);
    }
    mao_ui_list_set_items(&s_menu, items, NULL, mao_ui_menu_count());

    s_page.title = mao_ui_make_text(scr, &lv_font_montserrat_28, MAO_UI_FG, 4);
    s_page.note = mao_ui_make_text(scr, &lv_font_montserrat_14, MAO_UI_DIM, 4);
    lv_label_set_text(s_page.note, "SOON");
    s_page.cache[0].y = s_page.cache[1].y = INT16_MIN;
    s_page.timer = lv_timer_create(page_tick, PAGE_TICK_MS, NULL);
    lv_timer_pause(s_page.timer);

    s_intro.word = mao_ui_make_text(scr, &lv_font_montserrat_28, MAO_UI_FG, 6);
    lv_label_set_text(s_intro.word, "MAO");
    s_intro.prompt = mao_ui_make_text(scr, &lv_font_montserrat_14, MAO_UI_FG, 5);
    lv_label_set_text(s_intro.prompt, "TURN");
    s_intro.cache[0].y = s_intro.cache[1].y = INT16_MIN;
    s_intro.timer = lv_timer_create(intro_tick, INTRO_TICK_MS, NULL);
    if (intro_visible) {
        /* Fully visible in the very first frame (no fade-in at boot). */
        s_intro.presence = 1.0f;
        intro_tick(s_intro.timer);
        lv_timer_resume(s_intro.timer);
    } else {
        lv_timer_pause(s_intro.timer);
    }
}
