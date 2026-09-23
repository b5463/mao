/*
 * MAO character tuning. ALL visual proportions, motion gains and timings of
 * the character live here, so it can be art-directed numerically without
 * touching animation code. Units: pixels, seconds, detents/second.
 */
#pragma once

#include "mao_spring.h"

/* ---------------------------------------------------------------------- */
/* Look presets (switch at runtime in dev builds: "mao look <n>")         */
/* ---------------------------------------------------------------------- */

typedef struct {
    const char *name;
    float eye_w;         /* eye width at rest */
    float eye_h;         /* eye height at rest (openness 1) */
    float eye_gap;       /* centre-to-centre distance between eyes */
    float rest_y;        /* face centre, + = below screen centre */
} mao_look_t;

#define MAO_LOOKS {                                                     \
    { "slit",  15.0f, 34.0f, 46.0f, -4.0f },  /* default: tall, close */ \
    { "pill",  18.0f, 26.0f, 52.0f,  0.0f },  /* M1 proportions */                   \
    { "bead",  18.0f, 18.0f, 48.0f, -2.0f },  /* round, quieter */       \
}
#define MAO_LOOK_DEFAULT 0

/* Colour: off-white at rest; colour only as an event. */
#define MAO_EYE_COLOR        0xF1ECE2
#define MAO_TINT_MOVE_COLOR  0x3D63FF   /* cobalt: fast motion */
#define MAO_TINT_WARM_COLOR  0xF2C94C   /* yellow: acknowledged attention */
#define MAO_TINT_MOVE_MAX    0.45f      /* at most 45 % cobalt at full spin */
#define MAO_TINT_WARM_MAX    0.45f      /* at most 45 % yellow on a long press */

/* ---------------------------------------------------------------------- */
/* Eye geometry responses                                                 */
/* ---------------------------------------------------------------------- */

#define MAO_SQUASH_WIDEN     0.28f   /* width gain at full squash */
#define MAO_SQUASH_FLATTEN   0.55f   /* height loss at full squash */
#define MAO_WIDE_GROW        0.45f   /* width gain per unit of openness above 1 */
#define MAO_NARROW_MAX       0.55f   /* height loss at full narrow (warm) */
#define MAO_EYE_MIN_H        2.0f    /* closed eye = thin line */
#define MAO_PRESS_SQUASH     0.50f   /* held press */
#define MAO_PRESS_DROP       2.0f    /* px the face sinks while pressed */
#define MAO_PRESS_SPREAD     1.5f    /* px each eye moves outward while squashed */
#define MAO_RELEASE_KICK     -48.0f  /* px/s upward kick on release */
#define MAO_RELEASE_SQUASH_KICK -5.0f /* squash velocity on release (brief stretch) */

/* ---------------------------------------------------------------------- */
/* Dial response (continuous in speed)                                    */
/* ---------------------------------------------------------------------- */

#define MAO_DIAL_FULL_DPS    60.0f   /* speed mapped to intensity 1 */
#define MAO_DIAL_TAU_UP      0.07f   /* s: speed estimate rise */
#define MAO_DIAL_TAU_DOWN    0.30f   /* s: speed estimate decay */
#define MAO_DIAL_ENGAGE_S    0.45f   /* s after the last detent the face keeps its pose */
#define MAO_GAZE_HOLD_S      0.20f   /* extra time the eyes keep looking after the face settles */

#define MAO_GAZE_MIN         5.0f    /* gaze for a single slow detent */
#define MAO_GAZE_MAX         10.0f   /* gaze at the top of the lateral range */
#define MAO_GAZE_LAG_ORBIT   -3.0f   /* eyes trail the motion while orbiting */
#define MAO_FACE_MIN         2.5f
#define MAO_FACE_MAX         13.0f
#define MAO_TILT_MAX         3.0f
#define MAO_LEAN_GAIN        0.012f  /* tilt per detent/s^2 of acceleration */
#define MAO_LEAN_MAX         3.0f
#define MAO_LATERAL_RAMP     0.30f   /* intensity where lateral response saturates */
#define MAO_ORBIT_START      0.38f   /* intensity where the orbit begins to take over */
#define MAO_ORBIT_FULL       0.85f   /* intensity where the orbit is fully in charge */
#define MAO_ORBIT_RADIUS     44.0f   /* orbit radius once the orbit is fully in charge */
#define MAO_ORBIT_RIM_EXTRA  40.0f   /* extra radius near full speed: eyes clip the circle */
#define MAO_ORBIT_RIM_START  0.85f   /* intensity where the rim push begins */
#define MAO_MOTION_STRETCH   0.12f   /* eyes widen/flatten slightly at speed */
#define MAO_DETENTS_PER_REV  30.0f

/* Reversal disturbance: accumulates per reversal, decays over time. */
#define MAO_REV_WINDOW_S     0.70f   /* a sign change within this counts as a reversal */
#define MAO_REV_KICK         38.0f   /* px/s lateral kick per reversal, x (1 + disturbance) */
#define MAO_REV_DECAY_S      0.90f
#define MAO_REV_WOBBLE_START 1.6f    /* disturbance where the eyes start losing coordination */
#define MAO_REV_WOBBLE_GAIN  2.4f    /* px per unit above the start */
#define MAO_REV_WOBBLE_MAX   6.0f
#define MAO_REV_DIZZY        2.2f    /* disturbance reported as DIZZY (logs) */

/* ---------------------------------------------------------------------- */
/* Reactions                                                              */
/* ---------------------------------------------------------------------- */

#define MAO_NOTICE_OPEN      1.18f
#define MAO_NOTICE_S         0.35f
#define MAO_SURPRISE_OPEN    1.32f
#define MAO_SURPRISE_S       0.09f   /* widen before the drop starts */
#define MAO_LEAVE_Y          150.0f  /* below the circle */
#define MAO_LEAVE_SQUASH     0.35f   /* folds while it drops */
#define MAO_WARM_NARROW      0.40f
#define MAO_WARM_LIFT        -3.0f
#define MAO_WARM_S           1.20f
#define MAO_WARM_TINT_S      0.40f
#define MAO_ATTEND_S         1.00f
#define MAO_APPEAR_OFFSET    14.0f   /* first encounter: eyes appear displaced towards the turn */

/* ---------------------------------------------------------------------- */
/* Idle                                                                   */
/* ---------------------------------------------------------------------- */

#define MAO_IDLE_FIRST_MIN   2.5f    /* s after interaction before the first idle event */
#define MAO_IDLE_FIRST_MAX   5.0f
#define MAO_IDLE_GAP_MIN     1.6f
#define MAO_IDLE_GAP_MAX     4.2f
#define MAO_IDLE_QUIET_CHANCE 30     /* % of gaps that are long quiet stretches */
#define MAO_IDLE_QUIET_MIN   5.0f
#define MAO_IDLE_QUIET_MAX   11.0f
#define MAO_BLINK_S          0.15f

/* Event weights (sum is arbitrary). */
#define MAO_W_BLINK          34
#define MAO_W_GLANCE_SMALL   26
#define MAO_W_MICRO          10
#define MAO_W_GLANCE_LONG     9
#define MAO_W_REPOSITION      9
#define MAO_W_DOUBLE_BLINK    5
#define MAO_W_EDGE            4
#define MAO_W_HOP             3

/* Sleepy: a slow global "sleep" level scales everything down. */
#define MAO_SLEEP_OPEN_LOSS  0.68f   /* openness lost at sleep 1 */
#define MAO_SLEEP_DROP       8.0f
#define MAO_SLEEP_GAZE_DOWN  3.0f
#define MAO_SLEEP_BREATH     1.5f    /* px */
#define MAO_SLEEP_BREATH_S   4.5f    /* period */
#define MAO_SLEEP_PROFILE    ((mao_spring_profile_t){ .k = 3.5f, .zeta = 1.0f })   /* ~3 s fall */
#define MAO_SLEEP_GAP_MIN    4.0f
#define MAO_SLEEP_GAP_MAX    10.0f
#define MAO_SLEEP_BLINK_S    0.52f

/* Spring profile per channel (see mao_spring.h). */
#define MAO_P_FACE           MAO_SPRING_SOFT
#define MAO_P_GAZE           ((mao_spring_profile_t){ .k = 340.0f, .zeta = 0.60f })   /* leads the face */
#define MAO_P_OPEN           MAO_SPRING_SNAP
#define MAO_P_SQUASH         ((mao_spring_profile_t){ .k = 560.0f, .zeta = 0.30f })   /* the bouncy one */
#define MAO_P_TILT           MAO_SPRING_SOFT
#define MAO_P_NARROW         MAO_SPRING_SOFT
#define MAO_P_ORBIT_R        ((mao_spring_profile_t){ .k = 75.0f, .zeta = 0.62f })    /* overshoot on stop */
#define MAO_P_ORBIT_A        ((mao_spring_profile_t){ .k = 120.0f, .zeta = 0.78f })   /* lags the dial */
#define MAO_P_AWAY           MAO_SPRING_HEAVY
#define MAO_P_PRESS          MAO_SPRING_SNAP
#define MAO_P_TINT           MAO_SPRING_SOFT
