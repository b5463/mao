/*
 * Private to the mao_character core (command intake, dial physics,
 * attention, reactions). One context owns every piece of character state;
 * the stages of the pipeline receive it explicitly. Nothing here is visible
 * outside the component, and nothing here knows about ODD, devices or audio.
 */
#pragma once

#include <math.h>
#include "mao_character.h"
#include "mao_character_priv.h"
#include "mao_lark.h"
#include "mao_life.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define PI_F           3.14159265f
#define TWO_PI_F       6.28318531f

typedef enum { PRIO_IDLE = 0, PRIO_SYSTEM, PRIO_DIAL, PRIO_PRESS, PRIO_NAV } prio_t;

typedef enum {
    CMD_DIAL, CMD_PRESS, CMD_REACT, CMD_APPEAR, CMD_SLEEPY, CMD_LEAVE, CMD_RETURN,
    CMD_PREVIEW, CMD_DEBUG_DIAL, CMD_LOOK, CMD_EXPRESSION, CMD_TRANSFER, CMD_MIND, CMD_PEEK,
} cmd_type_t;

typedef struct {
    uint8_t type;
    int8_t arg;
    bool flag;
    int32_t value;
    float f;
} cmd_t;

typedef struct {
    QueueHandle_t cmds;
    mao_motion_t m;
    mao_char_draw_t draw;
    mao_idle_t idle;
    mao_lark_t lark;
    mao_life_t life;
    mao_transfer_t transfer;
    int fb_pending;             /* controller feedback state waiting to play (-1 = none) */
    uint32_t fb_until;          /* feedback playing: full layer gain */
    uint32_t fb_play_at;        /* verdicts wait for the mind's EVALUATE phase */
    bool fb_hold;               /* a held feedback (busy) is on */
    bool dizzy_noted;
    uint32_t last_tick_ms;
    volatile mao_character_state_t state;
    volatile uint8_t detents_per_rev;   /* 0 = not configured: MAO_DETENTS_PER_REV */

    bool visible;               /* has appeared */
    bool present;               /* false while away (menu) */
    bool peek;                  /* compact presence on the DEVICE page */
    bool sleepy;
    bool pressed;
    mao_mouth_t mouth;

    /* Dial physics */
    int32_t tick_detents;       /* detents received this tick */
    int last_sign;
    uint32_t last_detent_ms;
    float speed;                /* smoothed signed detents/s */
    float prev_abs;
    float accel;                /* smoothed d|speed|/dt */
    float disturb;
    bool orbiting;
    float orbit_target;

    /* Timed reactions */
    uint32_t press_until;
    uint32_t react_until;       /* system reaction (notice/attend) */
    uint32_t warm_until;
    uint32_t warm_tint_until;
    uint32_t wide_until;
    uint32_t leave_drop_at;     /* leave: when the drop starts */
    uint32_t away_until;        /* away/transition in progress */
    uint32_t last_input_ms;     /* dial or press, for the curiosity spark */

    /* Development */
    uint32_t dbg_dial_until;
    float dbg_dps, dbg_acc;
    uint32_t dbg_release_at, dbg_return_at;
} mao_char_t;

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float smoothstep(float e0, float e1, float x)
{
    const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline uint8_t mao_char_detents_per_rev(const mao_char_t *mc)
{
    return mc->detents_per_rev ? mc->detents_per_rev : (uint8_t)MAO_DETENTS_PER_REV;
}

static inline bool before(uint32_t now, uint32_t t)
{
    return t && (int32_t)(t - now) > 0;
}

/* Dial physics (mao_character_dial.c) */
bool mao_char_dial_engaged(mao_char_t *mc, uint32_t now);
void mao_char_on_dial(mao_char_t *mc, int32_t n, uint32_t now);
void mao_char_dial_update(mao_char_t *mc, float dt, uint32_t now);

/* Priority and wake (mao_character_react.c) */
prio_t mao_char_current_prio(mao_char_t *mc, uint32_t now);
void mao_char_wake(mao_char_t *mc, uint32_t now);
