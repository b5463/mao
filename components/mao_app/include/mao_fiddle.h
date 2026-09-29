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
/* Memory: a new bout begins where the last one left MAO - its highest level
 * is carried as a score bonus that fades over FIDDLE_CARRY_MS. Teasing a
 * MAO that is still wary or glaring gets there sooner. */
#define FIDDLE_CARRY_MS   20000u
#define FIDDLE_CARRY_PER_LEVEL 4
/* A run: turns less than FIDDLE_RUN_GAP_MS apart. */
#define FIDDLE_RUN_GAP_MS 700u

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
    uint8_t carry;                /* the level the last bout reached (its memory) ... */
    uint32_t carry_ms;            /* ... and when that bout ended */
    /* the current run: one direction, no reversal, no end reached = clean */
    int8_t run_dir;
    uint16_t run_detents;
    bool run_clean;
    uint32_t run_start_ms;
} mao_fiddle_t;

/* What a pause after turning means (mao_fiddle_quiet). */
typedef enum {
    MAO_FIDDLE_QUIET_NONE = 0,
    MAO_FIDDLE_QUIET_HELD,        /* stopped just short of the next level: MAO was waiting for it */
    MAO_FIDDLE_QUIET_CLEAN,       /* a clean, deliberate setting soon after heavy fiddling */
} mao_fiddle_quiet_t;

void mao_fiddle_reset(mao_fiddle_t *f);

/* One turn of `d` detents; `edge` is where the value landed (-1 low end,
 * +1 high end, 0 between). Returns a level the first time a bout reaches
 * it (and FED_UP again every FIDDLE_REPEAT_MS while it goes on), else NONE. */
mao_fiddle_level_t mao_fiddle_turn(mao_fiddle_t *f, int32_t d, int8_t edge, uint32_t now_ms);

/* The light was switched on or off (a press, or a turn past the bottom).
 * Flicking it scores FIDDLE_SWITCH_SCORE a time: a few switches in a few
 * seconds is fiddling; one switch, or two a while apart, never is. Same
 * return as mao_fiddle_turn. */
#define FIDDLE_SWITCH_SCORE 3
mao_fiddle_level_t mao_fiddle_switch(mao_fiddle_t *f, uint32_t now_ms);

/* The score now, the carried memory included (for logs and tests). */
int mao_fiddle_score(const mao_fiddle_t *f, uint32_t now_ms);

/* Call once when the turning has paused (~1.2 s after the last turn): what
 * the stop means, read from the bout and the run just ended. Each run is
 * judged once. */
#define FIDDLE_HELD_MARGIN 4      /* within this of the next level: "stopped just before" */
mao_fiddle_quiet_t mao_fiddle_quiet(mao_fiddle_t *f, uint32_t now_ms);
