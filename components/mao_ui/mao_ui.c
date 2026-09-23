/*
 * MAO UI core: view choreography, the shared motion tick and text helpers.
 *
 * Choreography (the character moves itself; see mao_character):
 *   HOME -> MENU   MAO widens, drops and folds out of the circle; the menu
 *                  words rise into the space it vacated (starting ~110 ms in).
 *   MENU -> HOME   the words sink away while MAO rises back into place.
 *   MENU -> PAGE   the selected word itself relocates upward to become the
 *                  page title; the neighbouring words fade out.
 *   PAGE -> MENU   the title slides back into its list position.
 */
#include "mao_ui.h"
#include "mao_ui_priv.h"

#include <math.h>
#include "esp_check.h"
#include "esp_log.h"
#include "mao_character.h"
#include "mao_display.h"

static const char *TAG = "MAO_UI";

#define UI_TICK_MS        33
#define MENU_ENTER_DELAY  110    /* words start rising once MAO has begun to drop */
#define HOME_RETURN_DELAY 40

static const char *const kMenuItems[] = { "DEVICES", "ACTIONS", "TOOLS", "SETUP" };
#define MENU_COUNT ((int)(sizeof(kMenuItems) / sizeof(kMenuItems[0])))

static mao_view_t s_shown = MAO_VIEW_HOME;   /* rendering bookkeeping only */
static lv_timer_t *s_tick;
static uint32_t s_last_ms;
static uint32_t s_return_at;                 /* delayed character return */
static uint32_t s_appear_at;                 /* delayed first appearance */
static int s_appear_dir;
#define INTRO_APPEAR_DELAY 110                /* eyes emerge once the word has cleared */

/* ---------------------------------------------------------------------- */
/* Text helpers                                                           */
/* ---------------------------------------------------------------------- */

lv_obj_t *mao_ui_make_text(lv_obj_t *scr, const lv_font_t *font, uint32_t color, int32_t track, const char *txt)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(l, track, 0);
    lv_obj_set_style_text_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_align(l, LV_ALIGN_CENTER);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    if (txt) {
        lv_label_set_text(l, txt);
    }
    return l;
}

void mao_ui_text_cache_reset(mao_text_cache_t *c)
{
    c->x = INT16_MIN;
    c->y = INT16_MIN;
    c->opa = 0;
}

void mao_ui_text_place(lv_obj_t *o, float x, float y, float opa, mao_text_cache_t *c)
{
    const lv_opa_t a = (lv_opa_t)(opa < 0.0f ? 0.0f : (opa > 255.0f ? 255.0f : opa));
    if (a < 4) {
        if (c->opa >= 4) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
        c->opa = 0;
        return;
    }
    const int16_t ix = (int16_t)lrintf(x), iy = (int16_t)lrintf(y);
    if (ix != c->x || iy != c->y) {
        lv_obj_set_pos(o, ix, iy);
        c->x = ix;
        c->y = iy;
    }
    if (a != c->opa) {
        lv_obj_set_style_text_opa(o, a, 0);
        if (c->opa < 4) {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
        c->opa = a;
    }
}

/* ---------------------------------------------------------------------- */
/* Shared motion tick                                                     */
/* ---------------------------------------------------------------------- */

static void tick_cb(lv_timer_t *t)
{
    const uint32_t now = lv_tick_get();
    float dt = (float)(now - s_last_ms) / 1000.0f;
    s_last_ms = now;
    dt = dt < 0.005f ? 0.005f : (dt > 0.05f ? 0.05f : dt);

    bool busy = false;
    if (s_return_at && (int32_t)(now - s_return_at) >= 0) {
        s_return_at = 0;
        mao_character_return();
    }
    if (s_appear_at && (int32_t)(now - s_appear_at) >= 0) {
        s_appear_at = 0;
        mao_character_appear(s_appear_dir);
    }
    busy |= s_return_at != 0 || s_appear_at != 0;
    busy |= mao_home_tick(dt, now);
    busy |= mao_overlay_tick(dt, now);
    if (!busy) {
        lv_timer_pause(t);
    }
}

void mao_ui_wake(void)
{
    if (s_tick) {
        if (lv_timer_get_paused(s_tick)) {
            s_last_ms = lv_tick_get();
        }
        lv_timer_resume(s_tick);
    }
}

/* ---------------------------------------------------------------------- */

const char *mao_ui_view_name(mao_view_t view)
{
    switch (view) {
    case MAO_VIEW_INTRO:       return "INTRO";
    case MAO_VIEW_HOME:        return "HOME";
    case MAO_VIEW_MENU:        return "MENU";
    case MAO_VIEW_PLACEHOLDER: return "PLACEHOLDER";
    default:                   return "?";
    }
}

int mao_ui_menu_count(void)
{
    return MENU_COUNT;
}

const char *mao_ui_menu_label(int index)
{
    return (index >= 0 && index < MENU_COUNT) ? kMenuItems[index] : "";
}

esp_err_t mao_ui_init(mao_view_t initial_view)
{
    ESP_RETURN_ON_FALSE(initial_view == MAO_VIEW_INTRO || initial_view == MAO_VIEW_HOME,
                        ESP_ERR_INVALID_ARG, TAG, "boot view must be INTRO or HOME");
    ESP_RETURN_ON_FALSE(mao_display_lock(0), ESP_ERR_TIMEOUT, TAG, "lock");

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(MAO_COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_tick = lv_timer_create(tick_cb, UI_TICK_MS, NULL);
    s_last_ms = lv_tick_get();

    /* Draw order: character behind the text layers. */
    mao_home_create(scr, initial_view == MAO_VIEW_HOME);
    esp_err_t err = mao_character_create(scr);
    mao_overlay_create(scr, initial_view == MAO_VIEW_INTRO);
    s_shown = initial_view;
    /* Lay out the first frame now (it is rendered before the panel lights). */
    mao_home_tick(0.0f, s_last_ms);
    mao_overlay_tick(0.0f, s_last_ms);

    mao_display_unlock();
    ESP_LOGI(TAG, "ui ready, initial view %s", mao_ui_view_name(initial_view));
    return err;
}

void mao_ui_boot(void)
{
    if (mao_display_lock(0)) {
        if (s_shown == MAO_VIEW_HOME) {
            mao_home_boot(lv_tick_get());
            mao_ui_wake();
        }
        mao_display_unlock();
    }
}

void mao_ui_debug_replay_boot(void)
{
    if (mao_display_lock(0)) {
        if (s_shown == MAO_VIEW_HOME) {
            mao_character_debug_preview(MAO_CHAR_PREVIEW_HIDE);
            mao_home_replay(lv_tick_get());
            mao_ui_wake();
        }
        mao_display_unlock();
    }
}

void mao_ui_intro_exit(int direction)
{
    if (!mao_display_lock(0)) {
        return;
    }
    if (s_shown == MAO_VIEW_INTRO) {
        s_shown = MAO_VIEW_HOME;
        mao_overlay_intro_exit(direction);
        s_appear_dir = direction;
        s_appear_at = lv_tick_get() + INTRO_APPEAR_DELAY;
        mao_ui_wake();
    }
    mao_display_unlock();
}

void mao_ui_show(mao_view_t view, int menu_index)
{
    if (!mao_display_lock(0)) {
        return;
    }
    const mao_view_t from = s_shown;
    s_shown = view;

    if (view == MAO_VIEW_MENU) {
        if (from == MAO_VIEW_HOME) {
            mao_character_leave();
            mao_overlay_menu_show(true, menu_index, MENU_ENTER_DELAY);
        } else {
            mao_overlay_page_show(false, NULL);          /* title slides back into the list */
            mao_overlay_menu_show(true, menu_index, 0);
        }
    } else if (view == MAO_VIEW_PLACEHOLDER) {
        mao_overlay_page_show(true, mao_ui_menu_label(menu_index));
    } else if (view == MAO_VIEW_HOME) {
        mao_overlay_page_show(false, NULL);
        mao_overlay_menu_show(false, menu_index, 0);
        if (from == MAO_VIEW_MENU || from == MAO_VIEW_PLACEHOLDER) {
            s_return_at = lv_tick_get() + HOME_RETURN_DELAY;
        }
    }
    mao_ui_wake();
    mao_display_unlock();
}

void mao_ui_menu_select(int index)
{
    if (mao_display_lock(0)) {
        mao_overlay_menu_select(index);
        mao_ui_wake();
        mao_display_unlock();
    }
}

void mao_ui_menu_bump(int direction)
{
    if (mao_display_lock(0)) {
        mao_overlay_menu_bump(direction);
        mao_ui_wake();
        mao_display_unlock();
    }
}
