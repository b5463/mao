#include "mao_fiddle.h"

#include <string.h>

void mao_fiddle_reset(mao_fiddle_t *f)
{
    memset(f, 0, sizeof(*f));
}

static int raw_score(const mao_fiddle_t *f, uint32_t now_ms)
{
    int s = 0;
    for (int i = 0; i < FIDDLE_EVENTS; i++) {
        if (f->weight[i] && now_ms - f->at[i] < FIDDLE_WINDOW_MS) {
            s += f->weight[i];
        }
    }
    return s;
}

/* The last bout's memory, fading; it only counts once this bout has begun
 * (a single calm turn after a bout is still just a turn). */
static int carry_bonus(const mao_fiddle_t *f, uint32_t now_ms, int raw)
{
    if (!f->carry || raw <= 0) {
        return 0;
    }
    const uint32_t age = now_ms - f->carry_ms;
    if (age >= FIDDLE_CARRY_MS) {
        return 0;
    }
    const int full = FIDDLE_CARRY_PER_LEVEL * (int)f->carry;
    return full - (int)((uint64_t)full * age / FIDDLE_CARRY_MS);
}

int mao_fiddle_score(const mao_fiddle_t *f, uint32_t now_ms)
{
    const int raw = raw_score(f, now_ms);
    return raw + carry_bonus(f, now_ms, raw);
}

static void score(mao_fiddle_t *f, uint8_t w, uint32_t now_ms)
{
    f->at[f->head] = now_ms;
    f->weight[f->head] = w;
    f->head = (uint8_t)((f->head + 1) % FIDDLE_EVENTS);
    f->last_event_ms = now_ms;
}

static mao_fiddle_level_t judge(mao_fiddle_t *f, uint32_t now_ms);

/* A quiet spell ends a bout (its level is remembered as the carry); a calm
 * spell ends its level. */
static void settle(mao_fiddle_t *f, uint32_t now_ms)
{
    if (f->any && now_ms - f->last_event_ms >= FIDDLE_QUIET_MS && now_ms - f->last_turn_ms >= FIDDLE_QUIET_MS) {
        const int8_t e = f->edge;
        const uint8_t peak = f->level > f->carry || now_ms - f->carry_ms >= FIDDLE_CARRY_MS ? f->level : f->carry;
        const uint32_t ended = f->level ? f->last_event_ms : f->carry_ms;
        mao_fiddle_reset(f);                  /* a new bout ... */
        f->edge = e;
        f->carry = peak;                      /* ... that remembers the last */
        f->carry_ms = ended;
    }
    if (f->level > 0 && now_ms - f->last_event_ms >= FIDDLE_QUIET_MS) {
        f->carry = f->level > f->carry ? f->level : f->carry;
        f->carry_ms = f->last_event_ms;
        f->level = 0;                         /* turning on, but calmly again: */
        memset(f->weight, 0, sizeof(f->weight));   /* what it did then is memory now, not score */
    }
}

mao_fiddle_level_t mao_fiddle_switch(mao_fiddle_t *f, uint32_t now_ms)
{
    settle(f, now_ms);
    score(f, FIDDLE_SWITCH_SCORE, now_ms);
    f->last_turn_ms = now_ms;
    f->any = true;
    f->run_clean = false;                     /* flicking the light is not setting it */
    return judge(f, now_ms);
}

mao_fiddle_level_t mao_fiddle_turn(mao_fiddle_t *f, int32_t d, int8_t edge, uint32_t now_ms)
{
    if (d == 0) {
        return MAO_FIDDLE_NONE;
    }
    settle(f, now_ms);
    const int8_t dir = d > 0 ? 1 : -1;
    /* the run: a new one after a pause */
    if (!f->any || now_ms - f->last_turn_ms >= FIDDLE_RUN_GAP_MS || f->run_detents == 0) {
        f->run_dir = dir;
        f->run_detents = 0;
        f->run_clean = true;
        f->run_start_ms = now_ms;
    }
    f->run_detents = (uint16_t)(f->run_detents + (d > 0 ? d : -d) > 60000 ? 60000 : f->run_detents + (d > 0 ? d : -d));
    if (dir != f->run_dir) {
        f->run_clean = false;                 /* it changed its mind: a correction, not one clean move */
    }
    if (f->any && dir != f->last_dir && now_ms - f->last_turn_ms < FIDDLE_GAP_MS) {
        score(f, 1, now_ms);
    }
    if (edge != 0 && edge != f->edge) {
        score(f, 2, now_ms);                  /* arrived at an end */
        f->run_clean = false;
    }
    f->edge = edge;
    f->last_dir = dir;
    f->last_turn_ms = now_ms;
    f->any = true;
    return judge(f, now_ms);
}

/* The level the score has reached: said the first time a bout reaches it,
 * and "fed up" again every FIDDLE_REPEAT_MS while it goes on. */
static mao_fiddle_level_t judge(mao_fiddle_t *f, uint32_t now_ms)
{
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

mao_fiddle_quiet_t mao_fiddle_quiet(mao_fiddle_t *f, uint32_t now_ms)
{
    if (!f->any || f->run_detents == 0 || now_ms - f->last_turn_ms < FIDDLE_RUN_GAP_MS) {
        return MAO_FIDDLE_QUIET_NONE;         /* nothing ended, or it has not stopped yet */
    }
    const int s = mao_fiddle_score(f, now_ms);
    const bool clean = f->run_clean && f->run_detents >= 2;
    f->run_detents = 0;                       /* each run is judged once */
    /* stopped with MAO noticing, just short of the tsk */
    if (f->level == MAO_FIDDLE_NOTICE && s >= FIDDLE_L2 - FIDDLE_HELD_MARGIN && s < FIDDLE_L2) {
        return MAO_FIDDLE_QUIET_HELD;
    }
    /* one clean, deliberate move while the memory of real annoyance is fresh */
    const uint8_t mood = f->level > f->carry ? f->level : f->carry;
    const bool fresh = f->level > 0 || (f->carry && now_ms - f->carry_ms < FIDDLE_CARRY_MS);
    if (clean && mood >= MAO_FIDDLE_ANNOYED && fresh && f->level < MAO_FIDDLE_FED_UP) {
        return MAO_FIDDLE_QUIET_CLEAN;
    }
    return MAO_FIDDLE_QUIET_NONE;
}
