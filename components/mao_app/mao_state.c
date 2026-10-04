/*
 * The one authoritative MAO application state, mutated only from the event
 * dispatcher task (via mao_app). UI and character are told what to show;
 * they never own state that the application depends on.
 *
 * Dial velocity estimation: recent dial events are kept in a small ring.
 * Speed = detents over the last SPEED_WINDOW; a direction reversal counter
 * over REVERSAL_WINDOW detects rapid back-and-forth.
 */
#include "mao_app_priv.h"

#include <stdlib.h>
#include <string.h>

#define DIAL_HISTORY          16
#define SPEED_WINDOW_US       (250 * 1000)
#define REVERSAL_WINDOW_US    (900 * 1000)
#define REVERSALS_FOR_DIZZY   3

/* Detents-per-second thresholds (LCDkit EC11: 30 detents per revolution).
 * These are the physical speeds approved in the first M1 hands-on test
 * (which counted every second detent), expressed in true detents. */
#define SPEED_NORMAL_DPS      12.0f    /* ~0.4 rev/s */
#define SPEED_FAST_DPS        28.0f    /* ~0.9 rev/s */
#define SPEED_VERY_FAST_DPS   56.0f    /* ~1.9 rev/s */

typedef struct {
    int64_t t_us;
    int32_t detents;   /* signed */
} dial_sample_t;

static mao_app_state_t s_state;
static dial_sample_t s_hist[DIAL_HISTORY];
static int s_hist_head;   /* next write position */

void mao_state_init(mao_view_t initial_view)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_hist, 0, sizeof(s_hist));
    s_state.view = initial_view;
    s_state.awake = true;
}

const mao_app_state_t *mao_state(void)
{
    return &s_state;
}

void mao_state_set_view(mao_view_t view)
{
    s_state.view = view;
}

void mao_state_set_menu_index(int index)
{
    s_state.menu_index = index;
}

void mao_state_set_devices_index(int index)
{
    s_state.devices_index = index;
}

void mao_state_set_device(uint64_t id)
{
    s_state.device_id = id;
}

void mao_state_set_awake(bool awake)
{
    s_state.awake = awake;
}

void mao_state_set_room_dark(bool dark)
{
    s_state.room_dark = dark;
}

void mao_state_set_room_quiet(bool quiet)
{
    s_state.room_quiet = quiet;
}

void mao_state_set_upside_down(bool upside)
{
    s_state.upside_down = upside;
}

void mao_state_set_annoyance(uint8_t level)
{
    s_state.annoyance = level > 3 ? 3 : level;
}

void mao_state_set_grudge_until(int64_t until_us)
{
    s_state.grudge_until_us = until_us;
}

void mao_state_note_input(int64_t now_us)
{
    s_state.last_input_us = now_us;
    s_state.interacting = true;
}

void mao_state_note_idle(void)
{
    s_state.interacting = false;
}

uint32_t mao_state_idle_ms(int64_t now_us)
{
    return (uint32_t)((now_us - s_state.last_input_us) / 1000);
}

mao_dial_motion_t mao_state_dial(int32_t detents, int64_t now_us)
{
    s_state.dial_position += detents;
    s_hist[s_hist_head] = (dial_sample_t) { .t_us = now_us, .detents = detents };
    s_hist_head = (s_hist_head + 1) % DIAL_HISTORY;

    int32_t recent = 0;
    int reversals = 0;
    int last_sign = 0;
    /* Walk newest -> oldest. */
    for (int i = 0; i < DIAL_HISTORY; i++) {
        const dial_sample_t *s = &s_hist[(s_hist_head - 1 - i + DIAL_HISTORY) % DIAL_HISTORY];
        if (s->t_us == 0) {
            break;
        }
        const int64_t age = now_us - s->t_us;
        if (age > REVERSAL_WINDOW_US) {
            break;
        }
        if (age <= SPEED_WINDOW_US) {
            recent += abs(s->detents);
        }
        const int sign = s->detents > 0 ? 1 : -1;
        if (last_sign != 0 && sign != last_sign) {
            reversals++;
        }
        last_sign = sign;
    }

    mao_dial_motion_t m = {
        .detents_per_s = (float)recent * 1e6f / (float)SPEED_WINDOW_US,
        .reversing = reversals >= REVERSALS_FOR_DIZZY,
    };
    if (m.detents_per_s >= SPEED_VERY_FAST_DPS) {
        m.speed = MAO_DIAL_VERY_FAST;
    } else if (m.detents_per_s >= SPEED_FAST_DPS) {
        m.speed = MAO_DIAL_FAST;
    } else if (m.detents_per_s >= SPEED_NORMAL_DPS) {
        m.speed = MAO_DIAL_NORMAL;
    } else {
        m.speed = MAO_DIAL_SLOW;
    }
    return m;
}

const char *mao_dial_speed_name(mao_dial_speed_t speed)
{
    switch (speed) {
    case MAO_DIAL_STILL:     return "STILL";
    case MAO_DIAL_SLOW:      return "SLOW";
    case MAO_DIAL_NORMAL:    return "NORMAL";
    case MAO_DIAL_FAST:      return "FAST";
    case MAO_DIAL_VERY_FAST: return "VERY_FAST";
    default:                 return "?";
    }
}
