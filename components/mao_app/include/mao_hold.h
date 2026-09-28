/*
 * The hold gesture on a device page (M4.1), as pure logic (host-tested:
 * tests/hold). A long press, or turning while held, brings MAO's options up:
 * BACK in the middle, the right option when turned right, the left one when
 * turned left - a three-position switch, one detent per step, that stops at
 * its ends. Letting go chooses. A long press not framed by a press is plain
 * BACK.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mao_events.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_HOLD_PASS = 0,   /* not the hold's: the page handles the event */
    MAO_HOLD_EATEN,      /* the hold took it (the options moved): redraw */
    MAO_HOLD_BACK,       /* let go in the middle */
    MAO_HOLD_RIGHT,      /* let go on the right option */
    MAO_HOLD_LEFT,       /* let go on the left option */
} mao_hold_t;

typedef struct {
    bool down;           /* the button is held */
    bool menu;           /* the options are up */
    int32_t acc;         /* -1 left / 0 middle / +1 right */
    int8_t sel;          /* -1 BACK, 0 right, 1 left */
} mao_hold_state_t;

void mao_hold_reset(mao_hold_state_t *h);

/* One input event. *moved (optional) is set when what is shown changed -
 * the options came up or the chosen one changed (the caller ticks). */
mao_hold_t mao_hold_step(mao_hold_state_t *h, mao_event_type_t type, bool has_right, bool has_left, bool *moved);

#ifdef __cplusplus
}
#endif
