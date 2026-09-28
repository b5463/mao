#include "mao_fiddle.h"

#include <string.h>

void mao_fiddle_reset(mao_fiddle_t *f)
{
    memset(f, 0, sizeof(*f));
}

int mao_fiddle_score(const mao_fiddle_t *f, uint32_t now_ms)
{
    int s = 0;
    for (int i = 0; i < FIDDLE_EVENTS; i++) {
        if (f->weight[i] && now_ms - f->at[i] < FIDDLE_WINDOW_MS) {
            s += f->weight[i];
        }
    }
    return s;
}

static void score(mao_fiddle_t *f, uint8_t w, uint32_t now_ms)
{
    f->at[f->head] = now_ms;
    f->weight[f->head] = w;
    f->head = (uint8_t)((f->head + 1) % FIDDLE_EVENTS);
    f->last_event_ms = now_ms;
}

mao_fiddle_level_t mao_fiddle_turn(mao_fiddle_t *f, int32_t d, int8_t edge, uint32_t now_ms)
{
    if (d == 0) {
        return MAO_FIDDLE_NONE;
    }
    if (f->any && now_ms - f->last_event_ms >= FIDDLE_QUIET_MS && now_ms - f->last_turn_ms >= FIDDLE_QUIET_MS) {
        const int8_t e = f->edge;
        mao_fiddle_reset(f);                  /* a new bout */
        f->edge = e;
    }
    if (f->level > 0 && now_ms - f->last_event_ms >= FIDDLE_QUIET_MS) {
        f->level = 0;                         /* turning on, but calmly again */
    }
    const int8_t dir = d > 0 ? 1 : -1;
    if (f->any && dir != f->last_dir && now_ms - f->last_turn_ms < FIDDLE_GAP_MS) {
        score(f, 1, now_ms);
    }
    if (edge != 0 && edge != f->edge) {
        score(f, 2, now_ms);                  /* arrived at an end */
    }
    f->edge = edge;
    f->last_dir = dir;
    f->last_turn_ms = now_ms;
    f->any = true;

    const int s = mao_fiddle_score(f, now_ms);
    const uint8_t want = s >= FIDDLE_L3 ? MAO_FIDDLE_FED_UP : s >= FIDDLE_L2 ? MAO_FIDDLE_ANNOYED
                         : s >= FIDDLE_L1 ? MAO_FIDDLE_NOTICE : MAO_FIDDLE_NONE;
    if (want > f->level) {
        f->level = want;
        f->level_ms = now_ms;
        return (mao_fiddle_level_t)want;
    }
    if (f->level == MAO_FIDDLE_FED_UP && want == MAO_FIDDLE_FED_UP && now_ms - f->level_ms >= FIDDLE_REPEAT_MS) {
        f->level_ms = now_ms;
        return MAO_FIDDLE_FED_UP;
    }
    return MAO_FIDDLE_NONE;
}
