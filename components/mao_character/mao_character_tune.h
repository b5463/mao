/*
 * MAO character tuning. ALL visual proportions, motion gains and timings of
 * the character live here, so it can be art-directed numerically without
 * touching animation code. Units: pixels, seconds, detents/second.
 */
#pragma once

#include <stdint.h>
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
    float pupil_w;       /* 0 = solid eyes (no pupils, lids, covers or shine) */
    float pupil_h;
    float motion_scale;  /* face travel / orbit radius multiplier */
    uint32_t eye_color;
    uint32_t pupil_color;  /* iris colour (the "pupil" object) */
    float shine;         /* catchlight size, 0 = none */
    float core;          /* pupil core size / iris size, 0 = none */
    uint32_t core_color;
} mao_look_t;

/* "maomao" (default): a STARBOY-style animation system built around Maomao
 * (The Apothecary Diaries), after her official anime design: white eyes with
 * big blue-violet irises, a navy pupil, a white catchlight above and a cyan
 * reflection below, and her flat upper lid resting on the iris.
 * "orb" is the same rig in MAO's monochrome palette. The older solid looks
 * remain for comparison. */
#define MAO_LOOKS {                                                                                                   { "maomao", 86.0f, 104.0f, 94.0f, -4.0f, 56.0f, 70.0f, 0.40f, 0xF1ECE2, 0x4B55D2, 1.0f, 0.46f, 0x151842 },        { "orb",    86.0f, 104.0f, 94.0f, -4.0f, 34.0f, 42.0f, 0.40f, 0xF1ECE2, 0x0B0B0E, 0.0f, 0.0f,  0 },               { "slit",   18.0f,  41.0f, 56.0f, -5.0f,  0.0f,  0.0f, 1.00f, 0xF1ECE2, 0, 0.0f, 0.0f, 0 },                        { "pill",   18.0f,  26.0f, 52.0f,  0.0f,  0.0f,  0.0f, 1.00f, 0xF1ECE2, 0, 0.0f, 0.0f, 0 },                        { "bead",   22.0f,  22.0f, 58.0f, -3.0f,  0.0f,  0.0f, 1.00f, 0xF1ECE2, 0, 0.0f, 0.0f, 0 },                    }
#define MAO_LOOK_DEFAULT 0

/* Pupil looks (pupil_w > 0). Gaze moves the pupils; the eyeballs follow a
 * little; the lids are background-coloured with a flat lower edge. */
#define MAO_LID_COLOR        0x08080A   /* = the UI background */
#define MAO_PUPIL_GAIN       1.2f    /* pupil px per gaze px (the head turns too) */
#define MAO_SCLERA_FOLLOW    0.0f    /* eyeball px per gaze px (the head carries them now) */
/* Pseudo-3D head (after STARBOY / Lark): the eyes sit on a sphere, so a look
 * is a head turn - the eyes slide, the one nearer the edge foreshortens and,
 * turned far enough, tucks behind the near one. The head follows the gaze
 * on a slower spring: the pupils lead, the head follows. */
#define MAO_HEAD_R           118.0f  /* sphere radius, px (sets the eyes' angles) */
#define MAO_HEAD_TRAVEL_R    80.0f   /* radius for sideways travel: turns stay inside the screen */
#define MAO_HEAD_EDGE        112.0f  /* an eye's outer edge never goes past this */
#define MAO_HEAD_YAW_GAIN    0.042f  /* rad of head turn per gaze px */
#define MAO_HEAD_PITCH_GAIN  0.030f
#define MAO_HEAD_PITCH_SHIFT 0.70f   /* vertical travel per unit of sin(pitch) x R */
#define MAO_HEAD_MIN_FORE    0.10f   /* an eye this foreshortened is behind the limb */
#define MAO_P_HEAD           ((mao_spring_profile_t){ .k = 120.0f, .zeta = 0.82f })   /* ~350 ms turns */
#define MAO_LID_ANGLE_PX     0.55f   /* lid slope px per px of eye width at angle 1 */
#define MAO_BLINK_DIP        3.0f    /* px the eyes sink as they close */
#define MAO_BLINK_SQUASH     0.10f   /* height lost at a full blink */
#define MAO_PUPIL_MARGIN     3.0f    /* pupil keeps this far inside the eyeball */
#define MAO_PUPIL_ORBIT      1.10f   /* pupils roll around the eyeball while orbiting */
#define MAO_PUPIL_WOBBLE     2.6f    /* pupils drift apart when dizzy, x wobble */
#define MAO_PUPIL_CONSTRICT  0.35f   /* pupil shrink per unit of openness above 1 */
#define MAO_PUPIL_MIN_H      5.0f    /* hidden below this (blink) */
#define MAO_LID_NARROW       1.00f   /* lid depth per unit of narrow */
#define MAO_LID_SLEEP        0.50f   /* lid depth at sleep 1 */
#define MAO_LID_SQUINT       0.36f   /* extra lid depth on the squinting eye */
#define MAO_LID_MAX          0.85f
#define MAO_LID_DEADZONE     0.08f   /* lid values below this show no lid */
#define MAO_LID_RADIUS       10      /* rounded corners of the flat lid edge */
#define MAO_TINT_PUPIL_MAX   0.90f   /* pupils go cobalt / yellow / red almost fully */
#define MAO_TINT_GOLD_COLOR  0xFFB000   /* her poison-greed gold: saturated, on the iris */
#define MAO_TINT_RED_COLOR   0xE5484D   /* red: mad / failure, as an event only */
/* Covers: round background-coloured lids that come down over each eye and
 * leave a thin lower crescent when closed (STARBOY's blink / happy / sleep). */
#define MAO_CRESCENT_PX      5.0f    /* crescent thickness when fully closed */
#define MAO_SHINE_COLOR      0xF6F3EC
#define MAO_SHINE2_COLOR     0x7FE6F2   /* the cyan reflection low in her iris */
#define MAO_SHINE_SIZE       0.26f   /* catchlight diameter / iris width */
#define MAO_SHINE_SMALL      0.50f   /* the lower (cyan) reflection / first (0 = none) */
#define MAO_PUPIL_DILATE     0.45f   /* pupil scale per unit of the dilation channel */

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
#define MAO_EYE_MIN_H        4.0f    /* closed eye = thin line ("- -") */
/* Maomao: resting lids slightly lowered - calm, a little unimpressed. */
#define MAO_REST_NARROW      0.26f
#define MAO_SQUINT_MAX       0.55f   /* one-eye narrowing (skeptical inspection) */
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

#define MAO_GAZE_MIN         7.0f    /* gaze for a single slow detent */
#define MAO_GAZE_MAX         14.0f   /* gaze at the top of the lateral range */
#define MAO_GAZE_LAG_ORBIT   -3.0f   /* eyes trail the motion while orbiting */
#define MAO_FACE_MIN         4.0f
#define MAO_FACE_MAX         20.0f
#define MAO_TILT_MAX         5.0f
#define MAO_LEAN_GAIN        0.012f  /* tilt per detent/s^2 of acceleration */
#define MAO_LEAN_MAX         4.5f
#define MAO_LATERAL_RAMP     0.30f   /* intensity where lateral response saturates */
#define MAO_ORBIT_START      0.38f   /* intensity where the orbit begins to take over */
#define MAO_ORBIT_FULL       0.85f   /* intensity where the orbit is fully in charge */
#define MAO_ORBIT_RADIUS     52.0f   /* orbit radius once the orbit is fully in charge */
#define MAO_ORBIT_RIM_EXTRA  40.0f   /* extra radius near full speed: eyes clip the circle */
#define MAO_ORBIT_RIM_START  0.85f   /* intensity where the rim push begins */
#define MAO_MOTION_STRETCH   0.12f   /* eyes widen/flatten slightly at speed */
#define MAO_DETENTS_PER_REV  30.0f

/* Reversal disturbance: accumulates per reversal, decays over time. */
#define MAO_REV_WINDOW_S     0.70f   /* a sign change within this counts as a reversal */
#define MAO_REV_KICK         52.0f   /* px/s lateral kick per reversal, x (1 + disturbance) */
#define MAO_REV_DECAY_S      0.90f
#define MAO_REV_WOBBLE_START 1.6f    /* disturbance where the eyes start losing coordination */
#define MAO_REV_WOBBLE_GAIN  2.4f    /* px per unit above the start */
#define MAO_REV_WOBBLE_MAX   8.0f
#define MAO_REV_DIZZY        2.2f    /* disturbance reported as DIZZY (logs) */

/* ---------------------------------------------------------------------- */
/* Reactions                                                              */
/* ---------------------------------------------------------------------- */

#define MAO_NOTICE_OPEN      1.18f
#define MAO_NOTICE_S         0.35f
#define MAO_SURPRISE_OPEN    1.38f
#define MAO_SURPRISE_S       0.09f   /* widen before the drop starts */
#define MAO_LEAVE_Y          210.0f  /* below the circle, even for the big eyes */
#define MAO_LEAVE_SQUASH     0.35f   /* folds while it drops */
#define MAO_LEAVE_OPEN       0.15f   /* ...and the eyes close to lines, so they never cross the words */
#define MAO_WARM_NARROW      0.40f
#define MAO_WARM_LIFT        -3.0f
#define MAO_WARM_S           1.20f
#define MAO_WARM_TINT_S      0.40f
#define MAO_ATTEND_S         1.00f
/* Maomao: sudden keen interest when touched after a quiet spell. */
#define MAO_SPARK_AFTER_S    3.0f    /* quiet time that makes the next turn 'interesting' */
#define MAO_SPARK_OPEN       1.28f
#define MAO_SPARK_S          0.22f
#define MAO_APPEAR_OFFSET    20.0f   /* first encounter: eyes appear displaced towards the turn */

/* ---------------------------------------------------------------------- */
/* Idle                                                                   */
/* ---------------------------------------------------------------------- */

#define MAO_IDLE_FIRST_MIN   2.5f    /* s after interaction before the first idle event */
#define MAO_IDLE_FIRST_MAX   5.0f
#define MAO_IDLE_GAP_MIN     1.2f
#define MAO_IDLE_GAP_MAX     3.4f
#define MAO_IDLE_QUIET_CHANCE 20     /* % of gaps that are long quiet stretches */
#define MAO_IDLE_QUIET_MIN   5.0f
#define MAO_IDLE_QUIET_MAX   11.0f
#define MAO_BLINK_S          0.24f   /* covers: close, a beat shut, open */

/* Event weights (sum is arbitrary). */
#define MAO_W_BLINK          34
#define MAO_W_GLANCE_SMALL   26
#define MAO_W_MICRO          10
#define MAO_W_GLANCE_LONG     9
#define MAO_W_REPOSITION      9
#define MAO_W_DOUBLE_BLINK    5
#define MAO_W_EDGE            4
#define MAO_W_HOP             3
#define MAO_W_INSPECT         6    /* Maomao: lean to the rim with one eye narrowed */
#define MAO_IDLE_AMP          1.4f /* scales idle glance/shift amplitudes */

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
#define MAO_SLIT_MIN         0.18f   /* core width left at a full slit */
#define MAO_STAR_COLOR       0xF5D24A   /* the greedy gold star */
#define MAO_SLIT_MIN         0.18f   /* core width left at a full slit */
#define MAO_SMILE_MAX        0.92f   /* how far the lower lids can rise, x eye height */
#define MAO_P_SHAPE          ((mao_spring_profile_t){ .k = 160.0f, .zeta = 0.75f })
#define MAO_P_CLOSE          ((mao_spring_profile_t){ .k = 420.0f, .zeta = 0.85f })   /* lids: quick, no bounce */
