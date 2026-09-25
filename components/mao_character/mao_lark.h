/*
 * MAO's replica of a Lark-style animation engine (after CREATURE's Lark,
 * which drives LITTLE GUY / STARBOY), running on top of MAO's procedural
 * character rig.
 *
 * Model (following the public Lark documentation):
 *   STATES    - emotions / interactions. Each has a looping (or one-shot)
 *               timeline of KEYFRAMES on the eye rig's channels (gaze, upper
 *               and lower lids, covers, iris size, tilt, squash, tints...).
 *               MAO is eyes only: Lark's extra scene objects are not used.
 *   TRANSITIONS - switching state blends every channel over the incoming
 *               state's duration and curve. Both timelines keep playing
 *               during the blend, so a switch never jumps.
 *   BLENDING  - the result is ADDED to the procedural pose, as Lark blends a
 *               look direction on top of any animation.
 *   VARIANTS  - each play is a variant: mirrored or not, played a little
 *               faster or slower, with a little more or less amplitude
 *               (Lark: "animations can be sped up, slowed down, blended,
 *               flipped"), and every keyframe value and every track's timing
 *               is nudged by a per-play seed - the same state never plays
 *               twice the same.
 *   MEMORY    - recently played states are strongly avoided, so nothing
 *               feels like a rerun.
 *   GENERATED - reactions can be procedural (mao_lark_gen.c): a generator
 *               composes a brand-new timeline each time the state plays
 *               (how many loops, which way, how fast, how it ends...);
 *               looping ones regenerate every cycle.
 *   BEHAVIOUR - a mood model (boredom, agitation, affection) picks states
 *               while MAO is idle, like LITTLE GUY wandering through its
 *               states; events can request or force a state.
 * Private to mao_character.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mao_character_priv.h"

typedef enum {
    LARK_LINEAR = 0,
    LARK_IN_OUT,      /* smooth both ends */
    LARK_OUT,         /* fast start, soft landing */
    LARK_IN,          /* soft start, hard landing */
    LARK_BACK,        /* soft landing with a small overshoot */
    LARK_HOLD,        /* jump at the key */
} lark_ease_t;

/* The curve applies to the segment that ENDS at this key. */
typedef struct {
    uint16_t t_ms;
    float v;
    uint8_t ease;
} lark_key_t;

typedef struct {
    uint8_t ch;               /* mao_channel_t */
    uint8_t n;
    const lark_key_t *keys;   /* sorted by t_ms */
} lark_track_t;

#define LARK_ONESHOT   0x01   /* plays once, then goes to `next` (or "neutral") */
#define LARK_NO_PICK   0x02   /* never chosen by the behaviour model (events only) */
#define LARK_NO_MIRROR 0x04   /* direction matters; never flipped */
#define LARK_REGEN     0x08   /* generated and looping: compose a new cycle each time round */

/* A generated timeline (one per playing state). */
#define LARK_GEN_TRACKS 9
#define LARK_GEN_KEYS   30
typedef struct {
    lark_track_t tracks[LARK_GEN_TRACKS];
    lark_key_t keys[LARK_GEN_TRACKS][LARK_GEN_KEYS];
    uint8_t n_tracks;
    uint16_t length_ms;
} lark_gen_t;
typedef void (*lark_gen_fn)(lark_gen_t *g);

/* Mood weights: chance of being picked in idle =
 * base + bored * boredom + agit * agitation + aff * affection (clamped >= 0). */
typedef struct {
    int8_t base, bored, agit, aff;
} lark_mood_t;

typedef struct {
    const char *name;
    uint16_t length_ms;       /* timeline length; loops unless ONESHOT */
    uint16_t in_ms;           /* transition into this state */
    uint8_t in_ease;
    uint8_t flags;
    lark_mood_t mood;
    const char *next;         /* ONESHOT follow-up; NULL = neutral */
    uint8_t n_tracks;
    const lark_track_t *tracks;
    lark_gen_fn gen;          /* procedural: composes the timeline at play time (tracks unused) */
} lark_state_t;

/* Library registry (mao_lark_states*.c). Index 0 is "neutral". */
const lark_state_t *mao_lark_state(int i);
int mao_lark_state_count(void);

typedef enum {
    LARK_EV_INPUT = 0,    /* any dial or press */
    LARK_EV_REVERSAL,
    LARK_EV_DIZZY,        /* disturbance crossed the dizzy level */
    LARK_EV_FAST,         /* fast orbit (per tick while orbiting) */
    LARK_EV_PRESS,
    LARK_EV_WARM,         /* long press */
    LARK_EV_RETURN,       /* came back from the menu */
} lark_event_t;

typedef struct {
    int8_t mirror;            /* +1 / -1 */
    float tempo;              /* playback speed */
    float amp;                /* motion amplitude */
    uint32_t seed;            /* per-play keyframe / timing jitter */
} lark_variant_t;

#define LARK_HISTORY 10

typedef struct {
    int cur, prev;
    uint32_t cur_t0, prev_t0;     /* playhead origins */
    lark_variant_t cur_v, prev_v;
    uint32_t trans_t0;
    uint32_t next_pick_ms;
    int pending;                  /* event-requested state, played when allowed; -1 none */
    mao_spring_t gain;            /* layer weight, follows the current priority */
    float boredom, agitation, affection;
    uint32_t last_input_ms;
    uint32_t last_ms;
    uint32_t press_ms[4];         /* recent presses (pestering) */
    uint8_t press_i;
    uint32_t plays;               /* statistics: states played */
    int16_t history[LARK_HISTORY];   /* recently played, newest first */
    lark_gen_t gen[2];            /* generated timelines: current and outgoing */
    int8_t cur_gen, prev_gen;     /* index into gen[] or -1 */
} mao_lark_t;

void mao_lark_init(mao_lark_t *l, uint32_t now);
int mao_lark_find(const char *name);
void mao_lark_switch(mao_lark_t *l, int state, uint32_t now);
void mao_lark_event(mao_lark_t *l, lark_event_t ev, uint32_t now);
/* 1 = fresh, down to ~0.05 for the state that just played. */
float mao_lark_freshness(const mao_lark_t *l, int state);

/* Generator building blocks (mao_lark_gen.c). */
void lark_gen_begin(lark_gen_t *g);
int lark_gen_track(lark_gen_t *g, uint8_t ch);
void lark_gen_key(lark_gen_t *g, int track, uint32_t t_ms, float v, uint8_t ease);
void lark_gen_end(lark_gen_t *g, uint32_t length_ms);
float lark_rand(float lo, float hi);
bool lark_chance(float p);
/* idle: the behaviour model may pick states; gain: layer weight target.
 * Writes the channel offsets. */
void mao_lark_update(mao_lark_t *l, uint32_t now, bool idle, bool sleepy, float gain, float out[CH_COUNT]);
