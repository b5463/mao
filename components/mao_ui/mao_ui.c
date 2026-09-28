/*
 * MAO UI core: view choreography, the shared motion tick and text helpers.
 *
 * Choreography (the character moves itself; see mao_character):
 *   HOME -> MENU   MAO widens, then drops and closes to lines as it folds out
 *                  of the circle; only then do the words open outward from
 *                  the centre (~210 ms in). The two never cross.
 *   MENU -> HOME   the words collapse quickly, then MAO rises back (~150 ms).
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
#define MENU_ENTER_DELAY  210    /* words appear only once MAO has dropped out of the centre */
#define HOME_RETURN_DELAY 320    /* the field collapses to its dot, then the eyes open from it */
#define DEVICES_ENTER_DELAY 120  /* a new text layer opens once the previous one has cleared */

/* Display order lives here alone; each entry carries its identity, so
 * reordering this table cannot break navigation. */
typedef struct {
    const char *label;
    mao_menu_id_t id;
} menu_entry_t;
static const menu_entry_t kMenuItems[] = {
    { "DEVICES", MAO_MENU_ID_DEVICES },
    { "ACTIONS", MAO_MENU_ID_ACTIONS },
    { "TOOLS",   MAO_MENU_ID_TOOLS },
    { "SETUP",   MAO_MENU_ID_SETUP },
};
#define MENU_COUNT ((int)(sizeof(kMenuItems) / sizeof(kMenuItems[0])))

static mao_view_t s_shown = MAO_VIEW_HOME;   /* rendering bookkeeping only */
static lv_timer_t *s_tick;
static uint32_t s_last_ms;

/* Away seam: where MAO passed through. It breathes slightly irregularly
 * (two incommensurate periods), grows quieter the longer MAO is away, and
 * stirs by a pixel or two when the dial turns. */
static struct {
    lv_obj_t *obj;
    bool on;
    int8_t dx, dy;
    float phase, phase2;
    float nudge;            /* px, decays */
    uint32_t since_ms;
    lv_opa_t last_opa;
    int16_t last_off;
} s_away;
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
    busy |= mao_devices_ui_tick(dt, now);
    busy |= mao_focus_tick(dt, now);
    if (s_away.on && s_away.obj) {
        s_away.phase += dt * 2.0f * 3.14159265f / 5.1f;    /* two slow, slightly */
        s_away.phase2 += dt * 2.0f * 3.14159265f / 7.9f;   /* mismatched breaths */
        const float u = 0.5f + 0.35f * sinf(s_away.phase) + 0.15f * sinf(s_away.phase2);
        /* The longer MAO is away, the quieter the seam (down to ~55 %). */
        const float age = (float)(now - s_away.since_ms) / 30000.0f;
        const float calm = 1.0f - 0.45f * (age > 1.0f ? 1.0f : age);
        const lv_opa_t opa = (lv_opa_t)((12.0f + 30.0f * u) * calm);
        if (opa != s_away.last_opa) {
            lv_obj_set_style_bg_opa(s_away.obj, opa, 0);
            s_away.last_opa = opa;
        }
        s_away.nudge *= expf(-dt / 0.25f);
        const int16_t off = (int16_t)lrintf(s_away.nudge);
        if (off != s_away.last_off) {
            s_away.last_off = off;
            if (s_away.dx) {
                lv_obj_set_pos(s_away.obj, s_away.dx * 111, off);
            } else {
                lv_obj_set_pos(s_away.obj, off, (s_away.dy ? s_away.dy : 1) * 111);
            }
        }
        busy = true;
    }
    if (!busy) {
        lv_timer_pause(t);
    }
}

void mao_ui_away_nudge(int direction)
{
    if (!s_away.on) {
        return;
    }
    if (mao_display_lock(0)) {
        s_away.nudge += direction > 0 ? 2.0f : -2.0f;
        s_away.nudge = s_away.nudge > 3.0f ? 3.0f : (s_away.nudge < -3.0f ? -3.0f : s_away.nudge);
        mao_ui_wake();
        mao_display_unlock();
    }
}

void mao_ui_away(bool on, int dx, int dy)
{
    if (!mao_display_lock(0)) {
        return;
    }
    if (!s_away.obj) {
        s_away.obj = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(s_away.obj);
        lv_obj_remove_flag(s_away.obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_align(s_away.obj, LV_ALIGN_CENTER);
        lv_obj_set_style_radius(s_away.obj, 2, 0);
        lv_obj_set_style_bg_color(s_away.obj, lv_color_hex(MAO_COL_FG), 0);
    }
    s_away.on = on;
    if (on) {
        s_away.dx = (int8_t)dx;
        s_away.dy = (int8_t)dy;
        s_away.since_ms = lv_tick_get();
        s_away.nudge = 0.0f;
        s_away.last_off = 0;
        if (dx) {
            lv_obj_set_size(s_away.obj, 4, 22);
            lv_obj_set_pos(s_away.obj, dx * 111, 0);
        } else {
            lv_obj_set_size(s_away.obj, 22, 4);
            lv_obj_set_pos(s_away.obj, 0, (dy ? dy : 1) * 111);
        }
        lv_obj_set_style_bg_opa(s_away.obj, 16, 0);
        s_away.last_opa = 16;
        s_away.phase = 0.0f;
        lv_obj_remove_flag(s_away.obj, LV_OBJ_FLAG_HIDDEN);
        mao_ui_wake();
    } else {
        lv_obj_add_flag(s_away.obj, LV_OBJ_FLAG_HIDDEN);
    }
    mao_display_unlock();
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
    case MAO_VIEW_DEVICES:     return "DEVICES";
    case MAO_VIEW_DEVICE:      return "DEVICE";
    default:                   return "?";
    }
}

int mao_ui_menu_count(void)
{
    return MENU_COUNT;
}

const char *mao_ui_menu_label(int index)
{
    return (index >= 0 && index < MENU_COUNT) ? kMenuItems[index].label : "";
}

mao_menu_id_t mao_ui_menu_id(int index)
{
    return (index >= 0 && index < MENU_COUNT) ? kMenuItems[index].id : MAO_MENU_ID_SETUP;
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
    mao_devices_ui_create(scr);
    mao_field_create(scr);
    mao_dots_create(scr);
    mao_focus_create(scr);
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

    /* The device views sit where the menu words were: they open once the
     * menu has cleared and close before it returns. */
    if (from == MAO_VIEW_DEVICES && view != MAO_VIEW_DEVICES) {
        mao_devlist_show(false, 0, view == MAO_VIEW_HOME);   /* home is to the left of the world */
    }
    if (from == MAO_VIEW_DEVICE && view != MAO_VIEW_DEVICE) {
        mao_devpanel_show(false, 0);
    }

    if (view == MAO_VIEW_DEVICES) {
        mao_overlay_page_show(false, NULL);
        mao_overlay_menu_show(false, menu_index, 0);
        /* YIELD (M4.1): the eyes step down below the rim and watch; the
         * names arrive from the right a beat later (ATTEND, then arrival). */
        /* The eyes gather into the centre; the field blooms from that dot. */
        const bool from_home = from == MAO_VIEW_HOME || from == MAO_VIEW_INTRO;
        if (from_home) {
            mao_character_gather();
        }
        mao_devlist_show(true, from_home ? MAO_CHAR_GATHER_MS : 0, from_home);
    } else if (view == MAO_VIEW_DEVICE) {
        /* Straight from HOME (back after a missed CONNECT): the eyes gather
         * away first, as they do for DEVICES - they never sit on a page. */
        const bool from_home = from == MAO_VIEW_HOME || from == MAO_VIEW_INTRO;
        if (from_home) {
            mao_character_gather();
        }
        mao_devpanel_show(true, from_home ? MAO_CHAR_GATHER_MS : DEVICES_ENTER_DELAY);
    } else if (view == MAO_VIEW_MENU) {
        if (from == MAO_VIEW_HOME) {
            mao_character_leave();
            mao_overlay_menu_show(true, menu_index, MENU_ENTER_DELAY);
        } else if (from == MAO_VIEW_DEVICES) {
            mao_overlay_menu_show(true, menu_index, DEVICES_ENTER_DELAY);
        } else {
            mao_overlay_page_show(false, NULL);          /* title slides back into the list */
            mao_overlay_menu_show(true, menu_index, 0);
        }
    } else if (view == MAO_VIEW_PLACEHOLDER) {
        mao_overlay_page_show(true, mao_ui_menu_label(menu_index));
    } else if (view == MAO_VIEW_HOME) {
        mao_overlay_page_show(false, NULL);
        mao_overlay_menu_show(false, menu_index, 0);
        if (from != MAO_VIEW_HOME && from != MAO_VIEW_INTRO) {
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
