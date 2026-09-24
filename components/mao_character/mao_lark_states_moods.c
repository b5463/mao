/*
 * MAO's Lark-style library, part 2: moods. STARBOY's documented behaviours
 * (anxious in noise, shivering in the cold, dizzy and then mad when shaken,
 * sad when flicked off, happy, purring, excited, startled, asleep) plus
 * Maomao's own (tipsy - she loves strong drink; her rare, bright smile),
 * all played through her eyes. Units: see mao_lark_author.h.
 */
#include "mao_lark_author.h"

/* purr: content, eyes squeezed into smiling slits, a tiny vibration. */
static const lark_key_t pu_sm[] = { K(0, 0.55f, LINEAR) };
static const lark_key_t pu_cl[] = { K(0, 0.45f, LINEAR), K(1600, 0.52f, IN_OUT), K(3200, 0.45f, IN_OUT) };
static const lark_key_t pu_fx[] = { K(0, 0, LINEAR), K(45, 1.6f, LINEAR), K(90, -1.6f, LINEAR), K(135, 0, LINEAR) };
static const lark_key_t pu_fy[] = { K(0, 2.0f, LINEAR) };
static const lark_track_t purr[] = { TRACK(CH_SMILE, pu_sm), TRACK(CH_CLOSE, pu_cl), TRACK(CH_FACE_X, pu_fx),
                                     TRACK(CH_FACE_Y, pu_fy) };

/* wink (one-shot). */
static const lark_key_t wk_w[] = { K(0, 0, LINEAR), K(120, 1.0f, OUT), K(420, 1.0f, LINEAR), K(620, 0, IN_OUT) };
static const lark_key_t wk_sm[] = { K(0, 0, LINEAR), K(120, 0.3f, OUT), K(420, 0.3f, LINEAR), K(620, 0, IN_OUT) };
static const lark_key_t wk_tilt[] = { K(0, 0, LINEAR), K(160, -3.0f, OUT), K(620, 0, IN_OUT) };
static const lark_track_t wink[] = { TRACK(CH_WINK, wk_w), TRACK(CH_SMILE, wk_sm), TRACK(CH_TILT, wk_tilt) };

/* excited: wide, bouncing, irises darting. */
static const lark_key_t ec_lid[] = { K(0, -0.26f, LINEAR) };
static const lark_key_t ec_open[] = { K(0, 0.12f, LINEAR) };
static const lark_key_t ec_pu[] = { K(0, 0.35f, LINEAR) };
static const lark_key_t ec_fy[] = { K(0, 0, LINEAR), K(130, -12.0f, OUT), K(270, 0, IN), K(400, -12.0f, OUT), K(540, 0, IN),
                                    K(1080, 0, LINEAR) };
static const lark_key_t ec_gx[] = { K(0, -6.0f, LINEAR), K(540, -6.0f, LINEAR), K(620, 6.0f, OUT), K(1080, 6.0f, LINEAR) };
static const lark_key_t ec_sq[] = { K(0, 0, LINEAR), K(270, 0.2f, OUT), K(330, 0, IN_OUT), K(540, 0.2f, OUT),
                                    K(600, 0, IN_OUT) };
static const lark_track_t excited[] = { TRACK(CH_NARROW, ec_lid), TRACK(CH_OPEN, ec_open), TRACK(CH_PUPIL, ec_pu),
                                        TRACK(CH_FACE_Y, ec_fy), TRACK(CH_GAZE_X, ec_gx), TRACK(CH_SQUASH, ec_sq) };

/* shiver (STARBOY in the cold): squinting, fast small shivers. */
static const lark_key_t sh_lid[] = { K(0, 0.16f, LINEAR) };
static const lark_key_t sh_sm[] = { K(0, 0.25f, LINEAR) };
static const lark_key_t sh_fx[] = { K(0, 0, LINEAR), K(40, 2.2f, LINEAR), K(80, -2.2f, LINEAR), K(120, 0, LINEAR) };
static const lark_key_t sh_sq[] = { K(0, 0.12f, LINEAR) };
static const lark_key_t sh_fy[] = { K(0, 4.0f, LINEAR) };
static const lark_track_t shiver[] = { TRACK(CH_NARROW, sh_lid), TRACK(CH_SMILE, sh_sm), TRACK(CH_FACE_X, sh_fx),
                                       TRACK(CH_SQUASH, sh_sq), TRACK(CH_FACE_Y, sh_fy) };

/* tipsy: Maomao likes a strong drink. Heavy dreamy lids, a soft smile, a
 * slow sway, the eyes drifting slightly out of step. */
static const lark_key_t ti_lid[] = { K(0, 0.30f, LINEAR) };
static const lark_key_t ti_sm[] = { K(0, 0.22f, LINEAR) };
static const lark_key_t ti_tilt[] = { K(0, -4.0f, LINEAR), K(1600, 4.0f, IN_OUT), K(3200, -4.0f, IN_OUT) };
static const lark_key_t ti_fx[] = { K(0, -12.0f, LINEAR), K(1600, 12.0f, IN_OUT), K(3200, -12.0f, IN_OUT) };
static const lark_key_t ti_fy[] = { K(0, 4.0f, LINEAR), K(800, 8.0f, IN_OUT), K(1600, 4.0f, IN_OUT), K(2400, 8.0f, IN_OUT),
                                    K(3200, 4.0f, IN_OUT) };
static const lark_key_t ti_wob[] = { K(0, 2.5f, LINEAR) };
static const lark_key_t ti_gy[] = { K(0, 3.0f, LINEAR) };
static const lark_track_t tipsy[] = { TRACK(CH_NARROW, ti_lid), TRACK(CH_SMILE, ti_sm), TRACK(CH_TILT, ti_tilt),
                                      TRACK(CH_FACE_X, ti_fx), TRACK(CH_FACE_Y, ti_fy), TRACK(CH_WOBBLE, ti_wob),
                                      TRACK(CH_GAZE_Y, ti_gy) };

/* sad (STARBOY flicked off): drooping lids, big wet irises looking down. */
static const lark_key_t sa_lid[] = { K(0, 0.28f, LINEAR) };
static const lark_key_t sa_pu[] = { K(0, 0.5f, LINEAR) };
static const lark_key_t sa_sh[] = { K(0, 0.6f, LINEAR) };
static const lark_key_t sa_gy[] = { K(0, 8.0f, LINEAR), K(2000, 9.0f, IN_OUT), K(4000, 8.0f, IN_OUT) };
static const lark_key_t sa_fy[] = { K(0, 14.0f, LINEAR), K(2000, 17.0f, IN_OUT), K(4000, 14.0f, IN_OUT) };
static const lark_key_t sa_tilt[] = { K(0, 2.0f, LINEAR) };
static const lark_track_t sad[] = { TRACK(CH_NARROW, sa_lid), TRACK(CH_PUPIL, sa_pu), TRACK(CH_SHINE, sa_sh),
                                    TRACK(CH_GAZE_Y, sa_gy), TRACK(CH_FACE_Y, sa_fy), TRACK(CH_TILT, sa_tilt) };

/* asleep: closed to crescents (the sleep channel does the rest). */
static const lark_key_t as_cl[] = { K(0, 0.95f, LINEAR) };
static const lark_track_t asleep[] = { TRACK(CH_CLOSE, as_cl) };

/* ======================================================================== */

const lark_state_t kLarkMoods[] = {
    /*     name          length  in   curve   flags         base bored agit aff  next    tracks */
    GEN("happy", 120, OUT, EVT, 0, 0, 0, 0, "purr", lark_gen_happy),
    GEN("pleased", 200, OUT, ONE, 0, 0, 0, 14, NULL, lark_gen_pleased),
    STATE("purr",         3200, 400,  IN_OUT, 0,             0,   0,  0, 10, NULL,    purr),
    STATE("wink",          620, 100,  OUT,    ONE,           0,   0,  0,  5, NULL,    wink),
    STATE("excited",      1080, 200,  OUT,    0,             1,   0,  2,  8, NULL,    excited),
    GEN("flustered", 200, OUT, LARK_REGEN, 0, 0, 14, 2, NULL, lark_gen_flustered),
    GEN("anxious", 200, OUT, LARK_REGEN, 0, 0, 16, 0, NULL, lark_gen_anxious),
    STATE("shiver",        120, 300,  OUT,    0,             1,   2,  0,  0, NULL,    shiver),
    STATE("tipsy",        3200, 900,  IN_OUT, 0,             1,   3,  0,  3, NULL,    tipsy),
    GEN("dizzy", 200, OUT, EVT, 0, 0, 0, 0, NULL, lark_gen_dizzy),
    GEN("dizzy_mad", 200, OUT, EVT, 0, 0, 0, 0, "mad", lark_gen_dizzy),
    GEN("mad", 100, OUT, ONE, 0, 0, 4, 0, NULL, lark_gen_mad),
    STATE("sad",          4000, 900,  IN_OUT, 0,             0,   4,  0,  0, NULL,    sad),
    GEN("startled", 60, OUT, EVT, 0, 0, 0, 0, NULL, lark_gen_startle),
    STATE("asleep",       5600, 1500, IN_OUT, LARK_NO_PICK,  0,   0,  0,  0, NULL,    asleep),
};
const int kLarkMoodsCount = (int)(sizeof(kLarkMoods) / sizeof(kLarkMoods[0]));
