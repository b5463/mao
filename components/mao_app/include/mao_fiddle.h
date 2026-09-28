/*
 * Fiddling (M4.1): is the user messing with HOME's light - sweeping it back
 * and forth, slamming it from end to end - rather than setting it?
 *
 * Controller first: this never changes what the knob does. The light follows
 * every detent; MAO only lets it show (a suspicious look, a tsk, then mad).
 *
 * Signals, per turn: a reversal (the direction flicks back within FIDDLE_GAP_MS of
 * the last turn) scores 1; arriving at an end of the range scores 2 (once per
 * arrival, not while pushed against it). The score is summed over the last
 * FIDDLE_WINDOW_MS. Setting a light carefully - a few overshoots and
 * corrections, or one sweep across to see the range - stays below level 1.
 *
 * Pure: host-tested (tests/fiddle).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FIDDLE_GAP_MS     450u    /* a flip later than this is a considered correction, not a flick back */
#define FIDDLE_WINDOW_MS  5000u
#define FIDDLE_QUIET_MS   4000u   /* this long without a scored event: it is over */
#define FIDDLE_REPEAT_MS  8000u   /* still at it this long after "mad": say it again */
#define FIDDLE_L1         8       /* scores for each level */
#define FIDDLE_L2         14
#define FIDDLE_L3         22
#define FIDDLE_EVENTS     32

typedef enum {
    MAO_FIDDLE_NONE = 0,
    MAO_FIDDLE_NOTICE,            /* "what are you doing" */
    MAO_FIDDLE_ANNOYED,           /* tsk */
    MAO_FIDDLE_FED_UP,            /* mad */
} mao_fiddle_level_t;

typedef struct {
    uint32_t at[FIDDLE_EVENTS];   /* scored events (ring) */
    uint8_t weight[FIDDLE_EVENTS];
    uint8_t head;
    int8_t last_dir;
    uint32_t last_turn_ms, last_event_ms;
    int8_t edge;                  /* -1 at the low end, +1 the high end, 0 between */
    uint8_t level;                /* the highest level reached in this bout */
    uint32_t level_ms;
    bool any;
} mao_fiddle_t;

void mao_fiddle_reset(mao_fiddle_t *f);

/* One turn of `d` detents; `edge` is where the value landed (-1 low end,
 * +1 high end, 0 between). Returns a level the first time a bout reaches
 * it (and FED_UP again every FIDDLE_REPEAT_MS while it goes on), else NONE. */
mao_fiddle_level_t mao_fiddle_turn(mao_fiddle_t *f, int32_t d, int8_t edge, uint32_t now_ms);

/* The score now (for logs and tests). */
int mao_fiddle_score(const mao_fiddle_t *f, uint32_t now_ms);
