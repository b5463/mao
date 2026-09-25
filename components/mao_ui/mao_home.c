/*
 * HOME: the screen is the object; at rest it contains only the character.
 *
 * Boot: the spaced "MAO" wordmark holds for a moment, then its letters draw
 * together into the centre (tracking collapses) and fade, while the eyes
 * open in the same place: one visual thought, not a logo screen followed by
 * another screen.
 */
#include <math.h>
#include "mao_character.h"
#include "mao_ui_priv.h"

#define WORDMARK_HOLD_MS    520
#define WORDMARK_TRACK_REST 8
#define WORDMARK_TRACK_GONE (-10)
#define EYES_AT             0.55f    /* wordmark progress at which the eyes open */

static lv_obj_t *s_word;
static mao_text_cache_t s_cache;
static mao_spring_t s_presence;      /* 1 = wordmark, 0 = gone */
static uint32_t s_start_at;
static bool s_eyes_opened;
static int32_t s_last_track = INT32_MIN;

void mao_home_create(lv_obj_t *scr, bool visible)
{
    s_word = mao_ui_make_text(scr, MAO_FONT_LARGE, MAO_COL_FG, WORDMARK_TRACK_REST, "MAO");
    mao_ui_text_cache_reset(&s_cache);
    mao_spring_init(&s_presence, visible ? 1.0f : 0.0f, MAO_SPRING_HEAVY);
    s_eyes_opened = !visible;
}

void mao_home_boot(uint32_t now_ms)
{
    s_start_at = now_ms + WORDMARK_HOLD_MS;
}

void mao_home_replay(uint32_t now_ms)
{
    /* Development: show the wordmark again and rerun the boot hand-over. */
    mao_spring_init(&s_presence, 1.0f, MAO_SPRING_HEAVY);
    s_eyes_opened = false;
    mao_home_boot(now_ms);
}

bool mao_home_tick(float dt, uint32_t now_ms)
{
    if (s_start_at && (int32_t)(now_ms - s_start_at) >= 0) {
        s_start_at = 0;
        s_presence.target = 0.0f;
    }
    mao_spring_step(&s_presence, dt);
    const float p = s_presence.x < 0.0f ? 0.0f : (s_presence.x > 1.0f ? 1.0f : s_presence.x);

    if (!s_eyes_opened && s_presence.target == 0.0f && p < EYES_AT) {
        s_eyes_opened = true;
        mao_character_appear(0);
    }
    /* Letters converge: tracking collapses from wide to overlapping. */
    const int32_t track = (int32_t)lrintf(WORDMARK_TRACK_GONE + (WORDMARK_TRACK_REST - WORDMARK_TRACK_GONE) * p);
    if (track != s_last_track) {
        s_last_track = track;
        lv_obj_set_style_text_letter_space(s_word, track, 0);
    }
    /* Fade out faster than the contraction so it never reads as a smear. */
    const float fade = p < 0.35f ? 0.0f : (p - 0.35f) / 0.65f;
    mao_ui_text_place(s_word, (float)track * 0.5f, -2.0f, 255.0f * fade, &s_cache);
    return s_start_at != 0 || !mao_spring_settled(&s_presence, 0.002f);
}
