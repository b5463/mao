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

#define EYES_OPEN_MS 1050            /* as the field draws back into the centre */

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

void mao_home_replay(uint32_t now_ms)
{
    /* Development: rerun the boot. */
    s_eyes_opened = false;
    mao_home_boot(now_ms);
}

bool mao_home_tick(float dt, uint32_t now_ms)
{
    (void)dt;
    if (!s_eyes_opened && s_boot_at && (int32_t)(now_ms - s_boot_at) >= EYES_OPEN_MS) {
        s_eyes_opened = true;
        s_boot_at = 0;
        mao_character_appear(0);           /* the eyes open from the centre point */
    }
    return !s_eyes_opened && s_boot_at != 0;
}
