#include "mao_ui.h"
#include "mao_ui_priv.h"

#include "esp_check.h"
#include "esp_log.h"
#include "mao_character.h"
#include "mao_display.h"

static const char *TAG = "MAO_UI";

static const char *const kMenuItems[] = { "DEVICES", "ACTIONS", "TOOLS", "SETUP" };
#define MENU_COUNT ((int)(sizeof(kMenuItems) / sizeof(kMenuItems[0])))

/* The view currently on screen: rendering bookkeeping for choreographing
 * transitions, not application state. */
static mao_view_t s_shown = MAO_VIEW_HOME;

/* ---------------------------------------------------------------------- */
/* Shared float animation helper                                          */
/* ---------------------------------------------------------------------- */

static void anim_float_exec(void *var, int32_t v)
{
    *(float *)var = (float)v / 1000.0f;
}

void mao_ui_anim_float(float *var, float to, uint32_t duration_ms, uint32_t delay_ms,
                       bool ease_out, lv_timer_t *timer)
{
    lv_anim_delete(var, anim_float_exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, anim_float_exec);
    lv_anim_set_values(&a, (int32_t)(*var * 1000.0f), (int32_t)(to * 1000.0f));
    lv_anim_set_duration(&a, duration_ms);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_path_cb(&a, ease_out ? lv_anim_path_ease_out : lv_anim_path_ease_in);
    lv_anim_start(&a);
    if (timer) {
        lv_timer_resume(timer);
    }
}

bool mao_ui_anim_running(float *var)
{
    return lv_anim_get(var, anim_float_exec) != NULL;
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
    return (index >= 0 && index < MENU_COUNT) ? kMenuItems[index] : "";
}

esp_err_t mao_ui_init(mao_view_t initial_view)
{
    ESP_RETURN_ON_FALSE(initial_view == MAO_VIEW_INTRO || initial_view == MAO_VIEW_HOME,
                        ESP_ERR_INVALID_ARG, TAG, "boot view must be INTRO or HOME");
    ESP_RETURN_ON_FALSE(mao_display_lock(0), ESP_ERR_TIMEOUT, TAG, "lock");

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(MAO_UI_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Draw order: character behind text layers. */
    mao_home_create(scr, initial_view == MAO_VIEW_HOME);
    esp_err_t err = mao_character_create(scr);
    mao_overlay_create(scr, initial_view == MAO_VIEW_INTRO);
    mao_devices_ui_create(scr);
    s_shown = initial_view;

    mao_display_unlock();
    ESP_LOGI(TAG, "ui ready, initial view %s", mao_ui_view_name(initial_view));
    return err;
}

void mao_ui_boot(void)
{
    if (mao_display_lock(0)) {
        if (s_shown == MAO_VIEW_HOME) {
            mao_home_boot();
        }
        mao_display_unlock();
    }
}

void mao_ui_show(mao_view_t view, int menu_index)
{
    if (!mao_display_lock(0)) {
        return;
    }
    const mao_view_t from = s_shown;
    s_shown = view;

    /* Leave the old view. */
    switch (from) {
    case MAO_VIEW_INTRO:
        mao_overlay_intro(false);
        break;
    case MAO_VIEW_MENU:
        if (view != MAO_VIEW_MENU) {
            mao_overlay_menu(false, menu_index, 0);
        }
        break;
    case MAO_VIEW_PLACEHOLDER:
        if (view != MAO_VIEW_PLACEHOLDER) {
            mao_overlay_placeholder(false, NULL, 0);
        }
        break;
    case MAO_VIEW_DEVICES:
        mao_devlist_show(false, 0);
        break;
    case MAO_VIEW_DEVICE:
        mao_devpanel_show(false, 0);
        break;
    case MAO_VIEW_HOME:
    default:
        break;
    }

    /* Enter the new one. Text arrives slightly after the previous layer
     * starts leaving, so the two never fully overlap. */
    switch (view) {
    case MAO_VIEW_HOME:
        if (from == MAO_VIEW_INTRO) {
            mao_character_react(MAO_CHAR_REACT_WAKE);   /* first appearance */
        } else if (from != MAO_VIEW_HOME) {
            mao_character_set_present(true);
        }
        break;
    case MAO_VIEW_MENU:
        if (from == MAO_VIEW_HOME) {
            mao_character_set_present(false);
        }
        mao_overlay_menu(true, menu_index, from == MAO_VIEW_HOME ? MAO_UI_T_STAGGER : MAO_UI_T_STAGGER / 2);
        break;
    case MAO_VIEW_PLACEHOLDER:
        mao_overlay_placeholder(true, mao_ui_menu_label(menu_index), MAO_UI_T_STAGGER);
        break;
    case MAO_VIEW_DEVICES:
        mao_devlist_show(true, MAO_UI_T_STAGGER);
        break;
    case MAO_VIEW_DEVICE:
        mao_devpanel_show(true, MAO_UI_T_STAGGER);
        break;
    case MAO_VIEW_INTRO:
    default:
        break;
    }
    mao_display_unlock();
}

void mao_ui_menu_select(int index)
{
    if (mao_display_lock(0)) {
        mao_overlay_menu_select(index);
        mao_display_unlock();
    }
}

void mao_ui_menu_bump(int direction)
{
    if (mao_display_lock(0)) {
        mao_overlay_menu_bump(direction);
        mao_display_unlock();
    }
}
