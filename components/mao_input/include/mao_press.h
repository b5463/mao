/*
 * The press gesture behind mao_input, as pure logic (host-tested: tests/press).
 * It takes the debounced switch level, the dial's detents and the time, and
 * says what MAO hears: PRESS / RELEASE / CLICK / DOUBLE_CLICK / LONG_PRESS,
 * and which detents count as turns. mao_input owns the GPIOs, the debounce
 * and the event bus.
 *
 * The turn guard (A1, P3 whole-top press): turning the wheel pushes on the
 * same top that clicks, so a firm thumb can close the switch mid-turn. A
 * press that starts within turn_guard_ms of a detent is part of the turn.
 * It is not heard, and the detents keep counting as plain turns, so it can
 * neither click nor open the hold options. If the turning stops and the
 * press is still held turn_settle_ms after the last detent, it becomes a
 * press from then on. A press with no turn just before it is unchanged,
 * press-then-turn (hold-and-turn) included. turn_guard_ms 0 turns it off
 * (the LCDkit's EC11 knob is turned from the side).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mao_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_PRESS_LONG_MS        900   /* M4.1: a slow press is still a press, not the options */
#define MAO_PRESS_DOUBLE_MS      350
#define MAO_PRESS_JIGGLE_MS      200   /* pushing an EC11 nudges it: in this window ... */
#define MAO_PRESS_JIGGLE_DETENTS 2     /* ... a turn counts only from two detents */

typedef struct {
    uint16_t turn_guard_ms;    /* 0: off */
    uint16_t turn_settle_ms;   /* held still this long after the last detent: a press */
} mao_press_cfg_t;

typedef void (*mao_press_emit_t)(mao_event_type_t type, int32_t value, void *ctx);

typedef struct {
    mao_press_cfg_t cfg;
    mao_press_emit_t emit;
    void *ctx;
    bool pressed;              /* debounced switch level */
    bool heard;                /* the press was reported (PRESS sent) */
    bool long_fired;           /* no CLICK / LONG_PRESS for this press any more */
    bool turned_early;         /* turned deliberately while the press was still debouncing */
    int64_t pressed_at_us;     /* when the press was heard */
    int64_t last_click_us;
    int64_t last_detent_us;    /* INT64_MIN: none yet */
    int32_t jiggle;            /* detents held back at the start of a press */
    uint32_t turn_presses;     /* presses taken as part of a turn (never heard) */
} mao_press_t;

void mao_press_init(mao_press_t *p, const mao_press_cfg_t *cfg, bool pressed_now,
                    mao_press_emit_t emit, void *ctx);

/* The debounced switch level settled (only on a change). */
void mao_press_level(mao_press_t *p, int64_t now_us, bool pressed);

/* Detents from the dial (signed, as decoded). `pressing`: the switch is
 * closed but its press is still debouncing. Returns the detents that count
 * as a turn (0: held back as a nudge). */
int32_t mao_press_detents(mao_press_t *p, int64_t now_us, int32_t detents, bool pressing);

/* Timers: long press, and a held turn press settling into a press. */
void mao_press_tick(mao_press_t *p, int64_t now_us);

/* The next time mao_press_tick() has work, or INT64_MAX. */
int64_t mao_press_deadline(const mao_press_t *p);

#ifdef __cplusplus
}
#endif
