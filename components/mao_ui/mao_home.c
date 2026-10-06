/*
 * HOME: the screen is the object; at rest it contains only the character.
 *
 * Boot (M4.1): the ODD field blooms from the centre - KINO D4's boot, the
 * same field - with MAO's name in dots over it; the field draws back into
 * the centre and the eyes open from that point. One visual thought in
 * MAO's own language (mao_ui_devices.c draws the bloom), not a logo screen
 * followed by another screen. The phase-1 wordmark label stays, invisible,
 * so nothing else changes shape.
 */
#include <math.h>
#include "mao_character.h"
#include "mao_ui_priv.h"

#define EYES_OPEN_MS MAO_BOOT_EYES_MS   /* as the field draws back into the centre (after the maker's mark) */

static lv_obj_t *s_word;
static mao_text_cache_t s_cache;
static uint32_t s_boot_at;           /* 0 = no boot running */
static bool s_eyes_opened;

void mao_home_create(lv_obj_t *scr, bool visible)
{
    s_word = mao_ui_make_text(scr, MAO_FONT_LARGE, MAO_COL_FG, 8, "MAO");
    mao_ui_text_cache_reset(&s_cache);
    mao_ui_text_place(s_word, 0.0f, 0.0f, 0.0f, &s_cache);   /* never shown */
    s_eyes_opened = !visible;
}

void mao_home_boot(uint32_t now_ms)
{
    s_boot_at = now_ms ? now_ms : 1;
    mao_devices_boot_bloom(s_boot_at);
}

/* The short wake (after MAO's own deep sleep): the resting frame - the
 * mark in lavender - then the eyes open from the centre. */
#define WAKE_EYES_MS 450
static bool s_short;

void mao_home_wake_boot(uint32_t now_ms)
{
    s_boot_at = now_ms ? now_ms : 1;
    s_short = true;
    s_eyes_opened = false;
    mao_ui_sleep_mark_now(true);
    mao_character_set_sleepy(true);           /* lavender, eyes closed: where it fell asleep */
}

void mao_home_replay(uint32_t now_ms)
{
    /* Development: rerun the boot. */
    s_eyes_opened = false;
    mao_home_boot(now_ms);
}

bool mao_home_tick(float dt, uint32_t now_ms)
{
    (void)dt;
    if (!s_eyes_opened && s_boot_at && (int32_t)(now_ms - s_boot_at) >= (s_short ? WAKE_EYES_MS : EYES_OPEN_MS)) {
        s_eyes_opened = true;
        s_boot_at = 0;
        if (s_short) {
            s_short = false;
            mao_character_set_sleepy(false);   /* stirs: the colour eases from lavender */
            mao_home_sleep_mark_set(false);    /* poke, gulp - and the eyes pop out of the mark */
            return true;
        }
        mao_character_appear(0);           /* the eyes open from the centre point */
    }
    return !s_eyes_opened && s_boot_at != 0;
}

/* Fatal hardware fault: the name stays, with a quiet service code under it
 * instead of the character (which never appears: no boot runs). No debug
 * text in normal operation; the console explains. */
void mao_home_fault(const char *code)
{
    static lv_obj_t *s_code;
    static mao_text_cache_t s_code_cache;
    mao_ui_text_cache_reset(&s_cache);
    mao_ui_text_place(s_word, 0.0f, -14.0f, 255.0f, &s_cache);
    if (!s_code) {
        s_code = mao_ui_make_text(lv_obj_get_parent(s_word), MAO_FONT_SMALL, MAO_COL_DIM, 3, NULL);
        mao_ui_text_cache_reset(&s_code_cache);
    }
    lv_label_set_text(s_code, code ? code : "SERVICE");
    mao_ui_text_place(s_code, 0.0f, 26.0f, 255.0f, &s_code_cache);
    s_boot_at = 0;
    s_eyes_opened = true;            /* nothing to open */
}
