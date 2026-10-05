/*
 * MAO's accent (M4.1): one colour for the whole screen - the eyes, the dot
 * UI and the ODD field - that is pink at rest and leans with the mood: red
 * when mad, rose when cross, blue when sad, lavender asleep, grey when blank. The event colours (cobalt spin,
 * yellow warmth, red failure) stay events on top of it.
 *
 * Pure: no LVGL, no RTOS - host-testable (tests/accent).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAO_ACCENT_BASE      0xF3A2C4u   /* MAO's pink (= the egg look, the dot colour) */
#define MAO_ACCENT_FADE_S    0.45f       /* time constant of a change into a mood */
#define MAO_ACCENT_CALM_S    1.40f       /* ... and of calming back to pink: the feeling lingers */

typedef struct {
    float r, g, b;                       /* the colour now, 0..255 */
    bool init;
    uint32_t landed;                     /* the exact colour once arrived; 0 = moving */
} mao_accent_t;

/* The colour a lark state leans to (MAO_ACCENT_BASE for most). */
uint32_t mao_accent_for_state(const char *state);

/* Where the accent is heading: the state's colour at `weight` (the
 * expression layer's gain, 0..1); asleep is lavender whatever plays. */
uint32_t mao_accent_target(const char *state, bool sleepy, float weight);

/* Move towards `target`: quickly into a mood (MAO_ACCENT_FADE_S), slowly
 * back to pink (MAO_ACCENT_CALM_S). */
void mao_accent_step(mao_accent_t *a, uint32_t target, float dt_s);

/* The colour now. Drawing quantises to the panel's RGB565, so a fade
 * redraws only when it visibly changes. */
uint32_t mao_accent_color(const mao_accent_t *a);

/* The states that lean (tests: each must exist in the library); NULL past the end. */
const char *mao_accent_mapped_state(int i);

/* The egg look's inner disc for an accent (the base keeps its own). */
uint32_t mao_accent_disc(uint32_t accent, uint32_t base_disc);
