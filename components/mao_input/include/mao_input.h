/*
 * MAO input: EC11 rotary encoder + push switch.
 *
 * Emits normalised events on the MAO event bus:
 *   MAO_EVENT_INPUT_CW / _CCW        value = detents (coalesced, >= 1)
 *   MAO_EVENT_INPUT_PRESS / _RELEASE
 *   MAO_EVENT_INPUT_CLICK            release before the long-press threshold
 *   MAO_EVENT_INPUT_LONG_PRESS       held for MAO_INPUT_LONG_PRESS_MS
 *   MAO_EVENT_INPUT_DOUBLE_CLICK     second click within MAO_INPUT_DOUBLE_CLICK_MS
 *   Turning while held (hold-and-turn) cancels that press's CLICK and LONG PRESS;
 *   PRESS / RELEASE still frame it. In the first 200 ms of a press a single
 *   detent is dropped (the nudge of pushing the knob).
 *                                    (emitted in addition to that CLICK)
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_INPUT_DEBOUNCE_MS      20
#define MAO_INPUT_LONG_PRESS_MS    600
#define MAO_INPUT_DOUBLE_CLICK_MS  350

/* Encoder decoder diagnostics since boot. */
typedef struct {
    uint32_t detents;              /* committed detents (both directions) */
    uint32_t invalid_transitions;  /* both lines changed between two ISRs */
    uint32_t recovered_detents;    /* detents committed with a missed edge */
    uint32_t rest_bounces;         /* partial moves that returned to rest */
} mao_input_stats_t;

/* Requires mao_board_init() and mao_system_init(). */
esp_err_t mao_input_init(void);

void mao_input_get_stats(mao_input_stats_t *out);

/* Mechanical detents per knob revolution (valid after init). */
uint8_t mao_input_detents_per_rev(void);

#ifdef __cplusplus
}
#endif
