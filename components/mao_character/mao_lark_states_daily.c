/*
 * MAO's Lark-style library, part 1: Maomao's everyday faces.
 *
 * Built from a reference gallery of her official anime design and scenes.
 * Maomao (The Apothecary Diaries): stoic and deadpan most of the time, a
 * flat upper lid resting on her irises; sarcastic, pragmatic, hard to charm;
 * insatiably curious, so her face changes instantly for anything interesting
 * - and above all for poisons and rare herbs, when her eyes turn gold. She
 * gives pests the "looking at a bug" glare, side-eyes schemers, sulks,
 * dozes like a cat. Units: see mao_lark_author.h.
 */
#include "mao_lark_author.h"

/* ======================================================================== */
/* Resting                                                                  */
/* ======================================================================== */

/* neutral: calm, barely breathing. */
static const lark_key_t ne_y[] = { K(0, 0, LINEAR), K(1800, -3.0f, IN_OUT), K(3600, 0, IN_OUT) };
static const lark_track_t neutral[] = { TRACK(CH_FACE_Y, ne_y) };

/* deadpan: her default. Heavier lids, irises a touch smaller, absolutely
 * still apart from a slow sideways drift of the eyes. */
static const lark_key_t dp_lid[] = { K(0, 0.20f, LINEAR) };
static const lark_key_t dp_pu[] = { K(0, -0.12f, LINEAR) };
static const lark_key_t dp_gx[] = { K(0, 0, LINEAR), K(2500, 0, LINEAR), K(3300, 5.0f, IN_OUT), K(6000, 5.0f, LINEAR),
                                    K(6800, 0, IN_OUT), K(8000, 0, LINEAR) };
static const lark_key_t dp_gy[] = { K(0, 1.0f, LINEAR) };
static const lark_track_t deadpan[] = { TRACK(CH_NARROW, dp_lid), TRACK(CH_PUPIL, dp_pu), TRACK(CH_GAZE_X, dp_gx),
                                        TRACK(CH_GAZE_Y, dp_gy) };

/* lookaround: a slow, observant scan, pausing on each side. */
static const lark_key_t la_gx[] = { K(0, 0, LINEAR), K(500, -11.0f, IN_OUT), K(1700, -11.0f, LINEAR),
                                    K(2300, 11.0f, IN_OUT), K(3500, 11.0f, LINEAR), K(4100, 0, IN_OUT),
                                    K(5200, 0, LINEAR) };
static const lark_key_t la_gy[] = { K(0, 0, LINEAR), K(500, -2.0f, IN_OUT), K(1700, -2.0f, LINEAR),
                                    K(2300, 1.0f, IN_OUT), K(4100, 0, IN_OUT) };
static const lark_key_t la_fx[] = { K(0, 0, LINEAR), K(700, -8.0f, IN_OUT), K(1700, -8.0f, LINEAR),
                                    K(2500, 8.0f, IN_OUT), K(3500, 8.0f, LINEAR), K(4300, 0, IN_OUT) };
static const lark_track_t lookaround[] = { TRACK(CH_GAZE_X, la_gx), TRACK(CH_GAZE_Y, la_gy), TRACK(CH_FACE_X, la_fx) };

/* thinking: deducing - up and to the side, one lid lower. */
static const lark_key_t th_gx[] = { K(0, 8.0f, LINEAR), K(1500, 10.0f, IN_OUT), K(2600, 7.0f, IN_OUT), K(3600, 8.0f, IN_OUT) };
static const lark_key_t th_gy[] = { K(0, -9.0f, LINEAR), K(1500, -10.0f, IN_OUT), K(3600, -9.0f, IN_OUT) };
static const lark_key_t th_sq[] = { K(0, -0.35f, LINEAR) };
static const lark_key_t th_tilt[] = { K(0, -2.5f, LINEAR) };
static const lark_track_t thinking[] = { TRACK(CH_GAZE_X, th_gx), TRACK(CH_GAZE_Y, th_gy), TRACK(CH_SQUINT, th_sq),
                                         TRACK(CH_TILT, th_tilt) };

/* daydream: up and away, floating. */
static const lark_key_t dd_gx[] = { K(0, -6.0f, LINEAR), K(2500, -4.0f, IN_OUT), K(5000, -6.0f, IN_OUT) };
static const lark_key_t dd_gy[] = { K(0, -7.0f, LINEAR), K(2500, -8.0f, IN_OUT), K(5000, -7.0f, IN_OUT) };
static const lark_key_t dd_fy[] = { K(0, 0, LINEAR), K(2500, -8.0f, IN_OUT), K(5000, 0, IN_OUT) };
static const lark_key_t dd_lid[] = { K(0, 0.10f, LINEAR) };
static const lark_track_t daydream[] = { TRACK(CH_GAZE_X, dd_gx), TRACK(CH_GAZE_Y, dd_gy), TRACK(CH_FACE_Y, dd_fy),
                                         TRACK(CH_NARROW, dd_lid) };

/* ======================================================================== */
/* Curiosity                                                                */
/* ======================================================================== */

/* curious: lids up, head cocked, irises a little wide. */
static const lark_key_t cu_lid[] = { K(0, -0.26f, LINEAR) };
static const lark_key_t cu_open[] = { K(0, 0.06f, LINEAR) };
static const lark_key_t cu_gy[] = { K(0, -3.0f, LINEAR), K(1400, -4.0f, IN_OUT), K(2800, -3.0f, IN_OUT) };
static const lark_key_t cu_tilt[] = { K(0, 3.0f, LINEAR), K(1200, 3.0f, LINEAR), K(1500, -2.0f, BACK),
                                      K(2500, -2.0f, LINEAR), K(2800, 3.0f, BACK) };
static const lark_key_t cu_pu[] = { K(0, 0.2f, LINEAR) };
static const lark_track_t curious[] = { TRACK(CH_NARROW, cu_lid), TRACK(CH_OPEN, cu_open), TRACK(CH_GAZE_Y, cu_gy),
                                        TRACK(CH_TILT, cu_tilt), TRACK(CH_PUPIL, cu_pu) };

/* examine: the apothecary at work - leaning in, focused, lids open, a
 * slow careful look from one side of the thing to the other. */
static const lark_key_t ex_lid[] = { K(0, -0.18f, LINEAR) };
static const lark_key_t ex_fy[] = { K(0, 10.0f, LINEAR) };
static const lark_key_t ex_gy[] = { K(0, 6.0f, LINEAR) };
static const lark_key_t ex_gx[] = { K(0, -5.0f, LINEAR), K(1600, 5.0f, IN_OUT), K(2200, 5.0f, LINEAR), K(3800, -5.0f, IN_OUT),
                                    K(4400, -5.0f, LINEAR) };
static const lark_key_t ex_pu[] = { K(0, -0.15f, LINEAR) };
static const lark_track_t examine[] = { TRACK(CH_NARROW, ex_lid), TRACK(CH_FACE_Y, ex_fy), TRACK(CH_GAZE_Y, ex_gy),
                                        TRACK(CH_GAZE_X, ex_gx), TRACK(CH_PUPIL, ex_pu) };

/* poison: her eyes turn GOLD and glitter at a rare poison or herb -
 * wide open, irises swelling, big catchlights, trembling with greed. */
static const lark_key_t po_lid[] = { K(0, -0.26f, LINEAR) };
static const lark_key_t po_open[] = { K(0, 0.16f, LINEAR), K(450, 0.22f, IN_OUT), K(900, 0.16f, IN_OUT) };
static const lark_key_t po_pu[] = { K(0, 0.45f, LINEAR) };
static const lark_key_t po_sh[] = { K(0, 1.0f, LINEAR), K(300, 1.4f, IN_OUT), K(600, 1.0f, IN_OUT), K(900, 1.4f, IN_OUT) };
static const lark_key_t po_gold[] = { K(0, 0.95f, LINEAR) };
static const lark_key_t po_fx[] = { K(0, 0, LINEAR), K(45, 1.5f, LINEAR), K(90, -1.5f, LINEAR), K(135, 0, LINEAR),
                                    K(900, 0, LINEAR) };
static const lark_key_t po_fy[] = { K(0, -8.0f, LINEAR), K(450, -11.0f, IN_OUT), K(900, -8.0f, IN_OUT) };
static const lark_track_t poison[] = { TRACK(CH_NARROW, po_lid), TRACK(CH_OPEN, po_open), TRACK(CH_PUPIL, po_pu),
                                       TRACK(CH_SHINE, po_sh), TRACK(CH_TINT_WARM, po_gold), TRACK(CH_FACE_X, po_fx),
                                       TRACK(CH_FACE_Y, po_fy) };

/* ======================================================================== */
/* Attitude                                                                 */
/* ======================================================================== */

/* suspicious: the near eye narrowed, a searching sideways look. */
static const lark_key_t su_sq[] = { K(0, -0.85f, LINEAR) };
static const lark_key_t su_gx[] = { K(0, 9.0f, LINEAR), K(500, 11.0f, IN_OUT), K(900, 8.0f, IN_OUT), K(1700, 11.0f, IN_OUT),
                                    K(2400, 9.0f, IN_OUT) };
static const lark_key_t su_gy[] = { K(0, 0, LINEAR), K(900, 2.0f, IN_OUT), K(1700, -1.0f, IN_OUT), K(2400, 0, IN_OUT) };
static const lark_key_t su_fx[] = { K(0, 18.0f, LINEAR) };
static const lark_key_t su_tilt[] = { K(0, 2.5f, LINEAR) };
static const lark_key_t su_pu[] = { K(0, -0.25f, LINEAR) };
static const lark_key_t suspicious_la[] = { K(0, 0.25f, LINEAR) };
static const lark_track_t suspicious[] = { TRACK(CH_LID_ANGLE, suspicious_la), TRACK(CH_SQUINT, su_sq), TRACK(CH_GAZE_X, su_gx), TRACK(CH_GAZE_Y, su_gy),
                                           TRACK(CH_FACE_X, su_fx), TRACK(CH_TILT, su_tilt), TRACK(CH_PUPIL, su_pu) };

/* sly: half-lidded side-eye, the faintest smile under it. */
static const lark_key_t sl_lid[] = { K(0, 0.26f, LINEAR) };
static const lark_key_t sl_sm[] = { K(0, 0.22f, LINEAR) };
static const lark_key_t sl_gx[] = { K(0, 0, LINEAR), K(400, 12.0f, IN_OUT), K(3000, 12.0f, LINEAR) };
static const lark_key_t sl_gy[] = { K(0, 2.0f, LINEAR) };
static const lark_key_t sl_tilt[] = { K(0, -2.0f, LINEAR) };
static const lark_key_t sly_la[] = { K(0, 0.25f, LINEAR) };
static const lark_track_t sly[] = { TRACK(CH_LID_ANGLE, sly_la), TRACK(CH_NARROW, sl_lid), TRACK(CH_SMILE, sl_sm), TRACK(CH_GAZE_X, sl_gx),
                                    TRACK(CH_GAZE_Y, sl_gy), TRACK(CH_TILT, sl_tilt) };

/* sinister (her "villain arc" smile): heavy lids, lower lids up, looking
 * straight at you, dead still. */
static const lark_key_t si_lid[] = { K(0, 0.30f, LINEAR) };
static const lark_key_t si_sm[] = { K(0, 0.45f, LINEAR) };
static const lark_key_t si_pu[] = { K(0, -0.35f, LINEAR) };
static const lark_key_t si_fy[] = { K(0, 8.0f, LINEAR) };
static const lark_key_t sinister_la[] = { K(0, 0.45f, LINEAR) };
static const lark_track_t sinister[] = { TRACK(CH_LID_ANGLE, sinister_la), TRACK(CH_NARROW, si_lid), TRACK(CH_SMILE, si_sm), TRACK(CH_PUPIL, si_pu),
                                         TRACK(CH_FACE_Y, si_fy) };

/* glare (one-shot): the "horror" face - irises shrink to pinpoints under
 * heavy flat lids, she sinks slightly and holds it. */
static const lark_key_t gl_lid[] = { K(0, 0, LINEAR), K(300, 0.32f, IN_OUT), K(2600, 0.32f, LINEAR), K(3200, 0, IN_OUT) };
static const lark_key_t gl_pu[] = { K(0, 0, LINEAR), K(300, -0.75f, OUT), K(2600, -0.75f, LINEAR), K(3200, 0, IN_OUT) };
static const lark_key_t gl_fy[] = { K(0, 0, LINEAR), K(900, 10.0f, IN_OUT), K(2600, 10.0f, LINEAR), K(3200, 0, IN_OUT) };
static const lark_key_t gl_open[] = { K(0, 0, LINEAR), K(300, 0.06f, OUT), K(2600, 0.06f, LINEAR), K(3200, 0, IN_OUT) };
static const lark_key_t glare_la[] = { K(0, 0.60f, LINEAR) };
static const lark_track_t glare[] = { TRACK(CH_LID_ANGLE, glare_la), TRACK(CH_NARROW, gl_lid), TRACK(CH_PUPIL, gl_pu), TRACK(CH_FACE_Y, gl_fy),
                                      TRACK(CH_OPEN, gl_open) };

/* contempt (one-shot): the look she gives a bug - or a pestering noble.
 * Heavy lids, small irises looking down and aside, leaning back, a shudder. */
static const lark_key_t ct_lid[] = { K(0, 0.40f, LINEAR), K(2200, 0.40f, LINEAR), K(2800, 0, IN_OUT) };
static const lark_key_t ct_gx[] = { K(0, -7.0f, LINEAR), K(2200, -7.0f, LINEAR), K(2800, 0, IN_OUT) };
static const lark_key_t ct_gy[] = { K(0, 5.0f, LINEAR), K(2200, 5.0f, LINEAR), K(2800, 0, IN_OUT) };
static const lark_key_t ct_fy[] = { K(0, 0, LINEAR), K(300, -12.0f, OUT), K(2200, -10.0f, LINEAR), K(2800, 0, IN_OUT) };
static const lark_key_t ct_fx[] = { K(0, 0, LINEAR), K(300, 10.0f, OUT), K(700, 10.0f, LINEAR), K(760, 12.5f, LINEAR),
                                    K(820, 8.0f, LINEAR), K(880, 11.0f, LINEAR), K(2200, 10.0f, LINEAR), K(2800, 0, IN_OUT) };
static const lark_key_t ct_pu[] = { K(0, -0.5f, LINEAR), K(2200, -0.5f, LINEAR), K(2800, 0, IN_OUT) };
static const lark_key_t contempt_la[] = { K(0, 0.30f, LINEAR) };
static const lark_track_t contempt[] = { TRACK(CH_LID_ANGLE, contempt_la), TRACK(CH_NARROW, ct_lid), TRACK(CH_GAZE_X, ct_gx), TRACK(CH_GAZE_Y, ct_gy),
                                         TRACK(CH_FACE_Y, ct_fy), TRACK(CH_FACE_X, ct_fx), TRACK(CH_PUPIL, ct_pu) };

/* sulky: heavy lids, looking down and away, sunk a little. */
static const lark_key_t sk_lid[] = { K(0, 0.34f, LINEAR) };
static const lark_key_t sk_gx[] = { K(0, -6.0f, LINEAR), K(2500, -7.0f, IN_OUT), K(5000, -6.0f, IN_OUT) };
static const lark_key_t sk_gy[] = { K(0, 8.0f, LINEAR) };
static const lark_key_t sk_fy[] = { K(0, 9.0f, LINEAR), K(2500, 11.0f, IN_OUT), K(5000, 9.0f, IN_OUT) };
static const lark_key_t sk_tilt[] = { K(0, 2.0f, LINEAR) };
static const lark_key_t sulky_la[] = { K(0, -0.35f, LINEAR) };
static const lark_track_t sulky[] = { TRACK(CH_LID_ANGLE, sulky_la), TRACK(CH_NARROW, sk_lid), TRACK(CH_GAZE_X, sk_gx), TRACK(CH_GAZE_Y, sk_gy),
                                      TRACK(CH_FACE_Y, sk_fy), TRACK(CH_TILT, sk_tilt) };

/* ======================================================================== */
/* Tired / cat                                                              */
/* ======================================================================== */

/* bored: flat lids, looking down and away, a slow sag. */
static const lark_key_t bo_lid[] = { K(0, 0.24f, LINEAR), K(2500, 0.30f, IN_OUT), K(5000, 0.24f, IN_OUT) };
static const lark_key_t bo_gx[] = { K(0, -7.0f, LINEAR), K(3000, -9.0f, IN_OUT), K(5000, -7.0f, IN_OUT) };
static const lark_key_t bo_gy[] = { K(0, 5.0f, LINEAR) };
static const lark_key_t bo_fy[] = { K(0, 6.0f, LINEAR), K(2500, 9.0f, IN_OUT), K(5000, 6.0f, IN_OUT) };
static const lark_key_t bored_la[] = { K(0, -0.15f, LINEAR) };
static const lark_track_t bored[] = { TRACK(CH_LID_ANGLE, bored_la), TRACK(CH_NARROW, bo_lid), TRACK(CH_GAZE_X, bo_gx), TRACK(CH_GAZE_Y, bo_gy),
                                      TRACK(CH_FACE_Y, bo_fy) };

/* sigh (one-shot): lids drop, face sinks and rises. "haa..." */
static const lark_key_t sg_lid[] = { K(0, 0, LINEAR), K(500, 0.30f, IN_OUT), K(1300, 0.30f, LINEAR), K(1900, 0, IN_OUT) };
static const lark_key_t sg_fy[] = { K(0, 0, LINEAR), K(400, -8.0f, IN_OUT), K(1100, 14.0f, IN_OUT), K(1900, 0, IN_OUT) };
static const lark_key_t sg_sq[] = { K(0, 0, LINEAR), K(1100, 0.12f, IN_OUT), K(1900, 0, IN_OUT) };
static const lark_key_t sigh_la[] = { K(0, -0.30f, LINEAR) };
static const lark_track_t sigh[] = { TRACK(CH_LID_ANGLE, sigh_la), TRACK(CH_NARROW, sg_lid), TRACK(CH_FACE_Y, sg_fy), TRACK(CH_SQUASH, sg_sq) };

/* slowblink (one-shot): the long, trusting cat blink. */
static const lark_key_t sb_cl[] = { K(0, 0, LINEAR), K(420, 1.0f, IN_OUT), K(900, 1.0f, LINEAR), K(1400, 0, IN_OUT) };
static const lark_key_t sb_fy[] = { K(0, 0, LINEAR), K(420, 3.0f, IN_OUT), K(1400, 0, IN_OUT) };
static const lark_track_t slowblink[] = { TRACK(CH_CLOSE, sb_cl), TRACK(CH_FACE_Y, sb_fy) };

/* yawn (one-shot): eyes squeeze shut, face lifts, then settles heavy. */
static const lark_key_t yw_cl[] = { K(0, 0, LINEAR), K(500, 0.85f, IN_OUT), K(1500, 0.85f, LINEAR), K(2100, 0.2f, IN_OUT),
                                    K(2600, 0, IN_OUT) };
static const lark_key_t yw_ew[] = { K(0, 0, LINEAR), K(500, 0.14f, IN_OUT), K(1500, 0.14f, LINEAR), K(2100, 0, IN_OUT) };
static const lark_key_t yw_eh[] = { K(0, 0, LINEAR), K(500, -0.12f, IN_OUT), K(1500, -0.12f, LINEAR), K(2100, 0, IN_OUT) };
static const lark_key_t yw_tilt[] = { K(0, 0, LINEAR), K(700, 3.0f, IN_OUT), K(1500, 3.0f, LINEAR), K(2200, 0, IN_OUT) };
static const lark_key_t yw_fy[] = { K(0, 0, LINEAR), K(600, -12.0f, IN_OUT), K(1500, -12.0f, LINEAR), K(2200, 6.0f, IN_OUT),
                                    K(2600, 0, IN_OUT) };
static const lark_track_t yawn[] = { TRACK(CH_CLOSE, yw_cl), TRACK(CH_EYE_W, yw_ew), TRACK(CH_EYE_H, yw_eh), TRACK(CH_TILT, yw_tilt), TRACK(CH_FACE_Y, yw_fy) };

/* drowsy: lids creep down, catch themselves, creep again. */
static const lark_key_t dr_lid[] = { K(0, 0.18f, LINEAR), K(2200, 0.48f, IN), K(2500, 0.08f, OUT), K(4200, 0.18f, IN_OUT) };
static const lark_key_t dr_gy[] = { K(0, 2.0f, LINEAR), K(2200, 4.0f, IN), K(2500, 0, OUT), K(4200, 2.0f, IN_OUT) };
static const lark_key_t dr_fy[] = { K(0, 0, LINEAR), K(2200, 6.0f, IN), K(2500, -4.0f, OUT), K(4200, 0, IN_OUT) };
static const lark_key_t drowsy_la[] = { K(0, -0.10f, LINEAR) };
static const lark_track_t drowsy[] = { TRACK(CH_LID_ANGLE, drowsy_la), TRACK(CH_NARROW, dr_lid), TRACK(CH_GAZE_Y, dr_gy), TRACK(CH_FACE_Y, dr_fy) };

/* doze: closes to crescents, sinks, jerks awake, closes again. */
static const lark_key_t dz_cl[] = { K(0, 0.2f, LINEAR), K(2600, 1.0f, IN_OUT), K(4600, 1.0f, LINEAR), K(4750, 0, OUT),
                                    K(5600, 0.2f, IN_OUT) };
static const lark_key_t dz_fy[] = { K(0, 0, LINEAR), K(4600, 14.0f, IN_OUT), K(4750, -8.0f, OUT), K(5600, 0, IN_OUT) };
static const lark_key_t dz_op[] = { K(0, 0, LINEAR), K(4600, 0, LINEAR), K(4750, 0.2f, OUT), K(5600, 0, IN_OUT) };
static const lark_track_t doze[] = { TRACK(CH_CLOSE, dz_cl), TRACK(CH_FACE_Y, dz_fy), TRACK(CH_OPEN, dz_op) };

/* ======================================================================== */

const lark_state_t kLarkDaily[] = {
    /*     name          length  in  curve   flags  base bored agit aff  next      tracks */
    STATE("neutral",      3600, 600, IN_OUT, 0,     18, -8,   0,  0, NULL,     neutral),
    STATE("deadpan",      8000, 700, IN_OUT, 0,     22, 10,   2,  0, NULL,     deadpan),
    STATE("lookaround",   5200, 500, IN_OUT, 0,      8,  4,   2,  0, NULL,     lookaround),
    STATE("thinking",     3600, 500, IN_OUT, 0,      6,  4,   0,  0, NULL,     thinking),
    STATE("daydream",     5000, 900, IN_OUT, 0,      5,  8,   0,  0, NULL,     daydream),
    STATE("curious",      2800, 350, OUT,    0,      8, -4,   0,  4, NULL,     curious),
    STATE("examine",      4400, 500, IN_OUT, 0,      7,  0,   0,  2, NULL,     examine),
    GEN("keen", 90, OUT, EVT, 0, 0, 0, 0, NULL, lark_gen_keen),
    STATE("poison",        900, 150, OUT,    0,      2,  0,   0,  6, NULL,     poison),
    GEN("doubletake", 150, OUT, ONE, 2, 0, 0, 0, NULL, lark_gen_doubletake),
    STATE("suspicious",   2400, 450, IN_OUT, 0,      6,  6,   4,  0, NULL,     suspicious),
    STATE("sly",          3000, 500, IN_OUT, 0,      4,  3,   0,  4, NULL,     sly),
    STATE("sinister",     3000, 600, IN_OUT, 0,      1,  2,   3,  2, NULL,     sinister),
    STATE("glare",        3200, 250, IN_OUT, ONE,    1,  0,   8,  0, NULL,     glare),
    STATE("contempt",     2800, 250, OUT,    ONE,    1,  3,  10,  0, NULL,     contempt),
    GEN("eyeroll", 150, IN_OUT, ONE, 2, 6, 8, 0, NULL, lark_gen_eyeroll),
    GEN("tsk", 120, OUT, ONE, 2, 4, 6, 0, NULL, lark_gen_tsk),
    STATE("sulky",        5000, 800, IN_OUT, 0,      1,  6,   6,  0, NULL,     sulky),
    STATE("bored",        5000, 900, IN_OUT, 0,      0, 18,   0,  0, NULL,     bored),
    STATE("sigh",         1900, 200, IN_OUT, ONE,    2,  8,   2,  0, NULL,     sigh),
    STATE("slowblink",    1400, 200, IN_OUT, ONE,    2,  2,   0, 12, NULL,     slowblink),
    STATE("yawn",         2600, 200, IN_OUT, ONE,    0,  8,   0,  0, "drowsy", yawn),
    STATE("drowsy",       4200, 900, IN_OUT, 0,      0, 14,   0,  0, NULL,     drowsy),
    STATE("doze",         5600, 900, IN_OUT, 0,      0, 12,   0,  0, NULL,     doze),
};
const int kLarkDailyCount = (int)(sizeof(kLarkDaily) / sizeof(kLarkDaily[0]));
