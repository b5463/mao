/*
 * MAO's inner life: what makes it act on its own.
 *
 * Four slow DRIVES change with time and with what people do:
 *   energy     - drains while awake, refills while asleep;
 *   curiosity  - builds up while nothing happens, spent on investigating;
 *   social     - the need for attention, builds while alone, met by touch;
 *   irritation - pestering, spinning, being woken; fades.
 * The drives schedule IMPULSES - things MAO decides to do with nobody
 * touching it. The most common one is to IMPROVISE (mao_life_improv.c): a
 * newly composed phrase of looks, lid moods, blinks and micro-expressions,
 * never the same twice. Others: notice and track an imaginary fly (and
 * sometimes pounce on it), sniff around, suddenly remember a poison, look at
 * you for attention, grumble, doze off, startle at nothing, peek, hum, and
 * very rarely turn into a cat for a while (Maomao's cat gag: slit pupils,
 * almond eyes). Recently done impulses are avoided. Impulses play Lark
 * states and steer an ATTENTION target that the eyes follow with real
 * saccades - quick jumps and fixations, never a smooth slide - plus constant
 * micro-saccades so the eyes are never dead still.
 *
 * Human events go through here too, so the same touch lands differently
 * depending on how MAO feels (lonely: delighted; irritated: "hmph";
 * asleep: startled; long gone: welcome back). Private to mao_character.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mao_character_priv.h"
#include "mao_lark.h"

typedef enum {
    LIFE_EV_FIRST_TOUCH = 0,   /* first input after a quiet spell */
    LIFE_EV_TOUCH,             /* a press */
    LIFE_EV_INPUT,             /* any dial or press */
    LIFE_EV_REVERSAL,
    LIFE_EV_DIZZY,
    LIFE_EV_WARM,              /* long press */
    LIFE_EV_WOKEN,             /* woken from sleep by the user */
} life_event_t;

/* Improvisation (mao_life_improv.c): a freshly composed phrase of beats. */
#define MAO_IMPROV_BEATS 7
#define MAO_IMPROV_CHANNELS 7

typedef struct {
    uint16_t dur_ms;
    float gx, gy;                       /* where to look */
    float v[MAO_IMPROV_CHANNELS];       /* lid, smile, squint, open, tilt, face y, pupil */
    uint8_t blink;                      /* 0 none, 1 blink, 2 double, 3 slow */
} mao_improv_beat_t;

typedef struct {
    bool active;
    uint8_t n, b, started;
    uint32_t beat_t0;
    mao_improv_beat_t beat[MAO_IMPROV_BEATS];
    float from[MAO_IMPROV_CHANNELS], cur[MAO_IMPROV_CHANNELS];
} mao_improv_t;

#define MAO_LIFE_HISTORY 6

typedef struct {
    float energy, curiosity, social, irritation;
    uint32_t last_ms, next_impulse_ms, last_input_ms;

    /* Cat mode. */
    uint32_t cat_until;
    mao_spring_t catness;

    /* Attention and saccades (gaze px). */
    uint8_t att;
    uint32_t att_t0, att_until;
    float ph[4];
    float tx, ty;               /* target */
    float gx, gy;               /* commanded gaze (jumps) */
    uint32_t next_sacc_ms;
    uint8_t after;              /* what follows the attention (pounce...) */

    mao_spring_t gain;          /* backs off while the user is in charge */
    const char *doing;          /* last impulse, for logs */

    mao_improv_t improv;
    int8_t history[MAO_LIFE_HISTORY];   /* recent impulse kinds, newest first */
} mao_life_t;

void mao_life_init(mao_life_t *l, uint32_t now);
void mao_life_event(mao_life_t *l, mao_lark_t *lark, life_event_t ev, uint32_t now);
/* idle: MAO is free to act. Adds attention gaze and cat traits to add[]. */
void mao_life_update(mao_life_t *l, mao_lark_t *lark, mao_motion_t *m, uint32_t now, bool idle, bool sleepy,
                     float add[CH_COUNT]);
bool mao_life_is_cat(const mao_life_t *l, uint32_t now);

/* Improvisation (mao_life_improv.c). */
void mao_improv_start(mao_life_t *l, uint32_t now);
bool mao_improv_update(mao_life_t *l, mao_motion_t *m, uint32_t now, float *gx, float *gy, float add[CH_COUNT]);
void mao_improv_cancel(mao_life_t *l);
