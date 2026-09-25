/*
 * MAO's Lark-style library, part 5: more of Maomao - the small, specific
 * things she does. Reading a label, counting herbs, weighing a doubt,
 * listening in, calculating, zoning out, nodding off and catching herself,
 * a sneeze and a hiccup, a shy glance, being proud of herself, crossing her
 * eyes at a silly thought, a flurry of disbelieving blinks, judging you head
 * to toe, relief, worry, a small gold glint, waking up properly.
 * Every play is varied further by the engine (mirror, tempo, amplitude,
 * per-key jitter). Units: see mao_lark_author.h.
 */
#include "mao_lark_author.h"

/* read: lines of text - left to right sweeps, stepping down. */
static const lark_key_t rd_gx[] = { K(0, -9.0f, LINEAR), K(700, 9.0f, LINEAR), K(820, -9.0f, OUT), K(1520, 9.0f, LINEAR),
                                    K(1640, -9.0f, OUT), K(2340, 9.0f, LINEAR), K(2460, -9.0f, OUT), K(3160, 8.0f, LINEAR),
                                    K(3600, 0, IN_OUT) };
static const lark_key_t rd_gy[] = { K(0, 1.0f, LINEAR), K(700, 1.0f, LINEAR), K(820, 4.0f, OUT), K(1520, 4.0f, LINEAR),
                                    K(1640, 7.0f, OUT), K(2340, 7.0f, LINEAR), K(2460, 10.0f, OUT), K(3160, 10.0f, LINEAR),
                                    K(3600, 0, IN_OUT) };
static const lark_key_t rd_lid[] = { K(0, 0.12f, LINEAR) };
static const lark_key_t rd_fy[] = { K(0, 6.0f, LINEAR) };
static const lark_track_t read_[] = { TRACK(CH_GAZE_X, rd_gx), TRACK(CH_GAZE_Y, rd_gy), TRACK(CH_NARROW, rd_lid),
                                      TRACK(CH_FACE_Y, rd_fy) };

/* count: herbs on a tray - small steps, one fixation per item. */
static const lark_key_t cn_gx[] = { K(0, -10.0f, LINEAR), K(300, -10.0f, LINEAR), K(360, -6.0f, OUT), K(660, -6.0f, LINEAR),
                                    K(720, -2.0f, OUT), K(1020, -2.0f, LINEAR), K(1080, 2.0f, OUT), K(1380, 2.0f, LINEAR),
                                    K(1440, 6.0f, OUT), K(1740, 6.0f, LINEAR), K(1800, 10.0f, OUT), K(2300, 10.0f, LINEAR),
                                    K(2700, 0, IN_OUT) };
static const lark_key_t cn_gy[] = { K(0, 7.0f, LINEAR), K(2300, 7.0f, LINEAR), K(2700, 0, IN_OUT) };
static const lark_key_t cn_lid[] = { K(0, 0.1f, LINEAR) };
static const lark_key_t cn_fy[] = { K(0, 8.0f, LINEAR), K(2300, 8.0f, LINEAR), K(2700, 0, IN_OUT) };
static const lark_track_t count_[] = { TRACK(CH_GAZE_X, cn_gx), TRACK(CH_GAZE_Y, cn_gy), TRACK(CH_NARROW, cn_lid),
                                       TRACK(CH_FACE_Y, cn_fy) };

/* doubt (one-shot): one eye narrows, the other opens - "really?" */
static const lark_key_t db_sq[] = { K(0, 0, LINEAR), K(250, 0.7f, OUT), K(1600, 0.7f, LINEAR), K(2000, 0, IN_OUT) };
static const lark_key_t db_ew[] = { K(0, 0, LINEAR), K(250, 0.05f, OUT), K(1600, 0.05f, LINEAR), K(2000, 0, IN_OUT) };
static const lark_key_t db_tilt[] = { K(0, 0, LINEAR), K(300, 3.5f, OUT), K(1600, 3.5f, LINEAR), K(2000, 0, IN_OUT) };
static const lark_key_t db_gx[] = { K(0, 0, LINEAR), K(300, -4.0f, OUT), K(1600, -4.0f, LINEAR), K(2000, 0, IN_OUT) };
static const lark_key_t doubt_la[] = { K(0, 0.20f, LINEAR) };
static const lark_track_t doubt[] = { TRACK(CH_LID_ANGLE, doubt_la), TRACK(CH_SQUINT, db_sq), TRACK(CH_EYE_W, db_ew), TRACK(CH_TILT, db_tilt),
                                      TRACK(CH_GAZE_X, db_gx) };

/* listen: eavesdropping - eyes still, off to the side, lids a touch low,
 * a slow lean towards the sound. */
static const lark_key_t li_gx[] = { K(0, 10.0f, LINEAR) };
static const lark_key_t li_gy[] = { K(0, 2.0f, LINEAR) };
static const lark_key_t li_fx[] = { K(0, 0, LINEAR), K(2000, 16.0f, IN_OUT), K(4000, 16.0f, LINEAR) };
static const lark_key_t li_tilt[] = { K(0, 3.0f, LINEAR) };
static const lark_key_t li_lid[] = { K(0, 0.14f, LINEAR) };
static const lark_track_t listen[] = { TRACK(CH_GAZE_X, li_gx), TRACK(CH_GAZE_Y, li_gy), TRACK(CH_FACE_X, li_fx),
                                       TRACK(CH_TILT, li_tilt), TRACK(CH_NARROW, li_lid) };

/* calculate: quick small saccades up and to the side while the lids
 * narrow - working out a dose. */
static const lark_key_t ca_gx[] = { K(0, 6.0f, LINEAR), K(180, 9.0f, OUT), K(360, 5.0f, OUT), K(540, 10.0f, OUT),
                                    K(720, 7.0f, OUT), K(900, 11.0f, OUT), K(1080, 6.0f, OUT), K(1260, 9.0f, OUT),
                                    K(1440, 6.0f, OUT) };
static const lark_key_t ca_gy[] = { K(0, -8.0f, LINEAR), K(180, -10.0f, OUT), K(360, -7.0f, OUT), K(540, -11.0f, OUT),
                                    K(720, -8.0f, OUT), K(900, -10.0f, OUT), K(1080, -7.0f, OUT), K(1260, -9.0f, OUT),
                                    K(1440, -8.0f, OUT) };
static const lark_key_t ca_lid[] = { K(0, 0.22f, LINEAR) };
static const lark_key_t ca_pu[] = { K(0, -0.2f, LINEAR) };
static const lark_key_t calculate_la[] = { K(0, 0.15f, LINEAR) };
static const lark_track_t calculate[] = { TRACK(CH_LID_ANGLE, calculate_la), TRACK(CH_GAZE_X, ca_gx), TRACK(CH_GAZE_Y, ca_gy), TRACK(CH_NARROW, ca_lid),
                                          TRACK(CH_PUPIL, ca_pu) };

/* zoned: gone - a fixed stare into nothing, no micro-movements, the
 * catchlights dimmed, lids a bit heavy. */
static const lark_key_t zo_lid[] = { K(0, 0.2f, LINEAR) };
static const lark_key_t zo_sh[] = { K(0, -0.5f, LINEAR) };
static const lark_key_t zo_pu[] = { K(0, 0.25f, LINEAR) };
static const lark_key_t zo_gy[] = { K(0, 2.0f, LINEAR) };
static const lark_key_t zo_dk[] = { K(0, 0.2f, LINEAR) };
static const lark_track_t zoned[] = { TRACK(CH_NARROW, zo_lid), TRACK(CH_SHINE, zo_sh), TRACK(CH_PUPIL, zo_pu),
                                      TRACK(CH_GAZE_Y, zo_gy), TRACK(CH_DARK, zo_dk) };

/* nodoff (one-shot): lids sink, the head drops - and she catches herself,
 * wide awake, looking around as if nothing happened. */
static const lark_key_t no_cl[] = { K(0, 0, LINEAR), K(1600, 0.85f, IN), K(1700, 0, OUT), K(3200, 0, LINEAR) };
static const lark_key_t no_fy[] = { K(0, 0, LINEAR), K(1600, 18.0f, IN), K(1720, -10.0f, OUT), K(2200, 0, IN_OUT) };
static const lark_key_t no_open[] = { K(0, 0, LINEAR), K(1650, 0, LINEAR), K(1720, 0.2f, OUT), K(2400, 0, IN_OUT) };
static const lark_key_t no_gx[] = { K(0, 0, LINEAR), K(1800, 0, LINEAR), K(2000, -9.0f, OUT), K(2400, -9.0f, LINEAR),
                                    K(2600, 8.0f, OUT), K(2900, 8.0f, LINEAR), K(3200, 0, IN_OUT) };
static const lark_track_t nodoff[] = { TRACK(CH_CLOSE, no_cl), TRACK(CH_FACE_Y, no_fy), TRACK(CH_OPEN, no_open),
                                       TRACK(CH_GAZE_X, no_gx) };

/* sneeze (one-shot): the build-up (eyes squeeze, lift), the burst, a
 * dazed blink. */
static const lark_key_t sz_cl[] = { K(0, 0, LINEAR), K(300, 0.3f, IN), K(600, 0.6f, IN), K(800, 0.95f, IN), K(900, 0.95f, LINEAR),
                                    K(1300, 0, OUT), K(1600, 0, LINEAR) };
static const lark_key_t sz_fy[] = { K(0, 0, LINEAR), K(800, -12.0f, IN), K(880, 18.0f, OUT), K(1200, 2.0f, IN_OUT), K(1600, 0, IN_OUT) };
static const lark_key_t sz_sq[] = { K(0, 0, LINEAR), K(800, -0.2f, IN), K(880, 0.4f, OUT), K(1100, 0, BACK) };
static const lark_key_t sz_wob[] = { K(0, 0, LINEAR), K(900, 0, LINEAR), K(1000, 3.0f, OUT), K(1600, 0, IN_OUT) };
static const lark_track_t sneeze[] = { TRACK(CH_CLOSE, sz_cl), TRACK(CH_FACE_Y, sz_fy), TRACK(CH_SQUASH, sz_sq),
                                       TRACK(CH_WOBBLE, sz_wob) };

/* hiccup: a sudden little jump every so often, a surprised blink after. */
static const lark_key_t hi_fy[] = { K(0, 0, LINEAR), K(900, 0, LINEAR), K(960, -14.0f, OUT), K(1200, 0, IN), K(2100, 0, LINEAR),
                                    K(2160, -12.0f, OUT), K(2400, 0, IN), K(3000, 0, LINEAR) };
static const lark_key_t hi_open[] = { K(0, 0, LINEAR), K(900, 0, LINEAR), K(960, 0.15f, OUT), K(1300, 0, IN_OUT),
                                      K(2100, 0, LINEAR), K(2160, 0.12f, OUT), K(2500, 0, IN_OUT), K(3000, 0, LINEAR) };
static const lark_key_t hi_lid[] = { K(0, 0.16f, LINEAR) };
static const lark_track_t hiccup[] = { TRACK(CH_FACE_Y, hi_fy), TRACK(CH_OPEN, hi_open), TRACK(CH_NARROW, hi_lid) };

/* shy (one-shot): a glance at you, caught, eyes away with a small smile. */
static const lark_key_t sy_gx[] = { K(0, 8.0f, LINEAR), K(400, 0, OUT), K(900, 0, LINEAR), K(1000, 11.0f, OUT),
                                    K(2200, 11.0f, LINEAR), K(2600, 0, IN_OUT) };
static const lark_key_t sy_gy[] = { K(0, 4.0f, LINEAR), K(400, 0, OUT), K(900, 0, LINEAR), K(1000, 6.0f, OUT),
                                    K(2200, 6.0f, LINEAR), K(2600, 0, IN_OUT) };
static const lark_key_t sy_sm[] = { K(0, 0, LINEAR), K(1000, 0.35f, OUT), K(2200, 0.35f, LINEAR), K(2600, 0, IN_OUT) };
static const lark_key_t sy_fy[] = { K(0, 0, LINEAR), K(1000, 6.0f, OUT), K(2200, 6.0f, LINEAR), K(2600, 0, IN_OUT) };
static const lark_key_t shy_la[] = { K(0, -0.25f, LINEAR) };
static const lark_track_t shy[] = { TRACK(CH_LID_ANGLE, shy_la), TRACK(CH_GAZE_X, sy_gx), TRACK(CH_GAZE_Y, sy_gy), TRACK(CH_SMILE, sy_sm),
                                    TRACK(CH_FACE_Y, sy_fy) };

/* proud: she got it right - chin up, lids content, a smile. */
static const lark_key_t pr_fy[] = { K(0, -10.0f, LINEAR) };
static const lark_key_t pr_lid[] = { K(0, 0.22f, LINEAR) };
static const lark_key_t pr_sm[] = { K(0, 0.3f, LINEAR) };
static const lark_key_t pr_gy[] = { K(0, 3.0f, LINEAR) };
static const lark_key_t pr_sh[] = { K(0, 0.3f, LINEAR) };
static const lark_key_t pr_tilt[] = { K(0, -2.0f, LINEAR), K(1500, 2.0f, IN_OUT), K(3000, -2.0f, IN_OUT) };
static const lark_track_t proud[] = { TRACK(CH_FACE_Y, pr_fy), TRACK(CH_NARROW, pr_lid), TRACK(CH_SMILE, pr_sm),
                                      TRACK(CH_GAZE_Y, pr_gy), TRACK(CH_SHINE, pr_sh), TRACK(CH_TILT, pr_tilt) };

/* crosseyed (one-shot): a silly thought - the eyes cross, then snap back. */
static const lark_key_t ce_x[] = { K(0, 0, LINEAR), K(300, 22.0f, IN_OUT), K(1300, 22.0f, LINEAR), K(1450, 0, OUT) };
static const lark_key_t ce_gy[] = { K(0, 0, LINEAR), K(300, 3.0f, IN_OUT), K(1300, 3.0f, LINEAR), K(1450, 0, OUT) };
static const lark_key_t ce_blink[] = { K(0, 0, LINEAR), K(1450, 0, LINEAR), K(1550, 0.9f, OUT), K(1750, 0, IN_OUT) };
static const lark_key_t ce_pu[] = { K(0, 0, LINEAR), K(300, -0.55f, IN_OUT), K(1300, -0.55f, LINEAR), K(1450, 0, OUT) };
static const lark_track_t crosseyed[] = { TRACK(CH_CROSS, ce_x), TRACK(CH_PUPIL, ce_pu), TRACK(CH_GAZE_Y, ce_gy),
                                          TRACK(CH_CLOSE, ce_blink) };

/* blinkflurry (one-shot): disbelief - a rapid run of blinks. */
static const lark_key_t bf_cl[] = { K(0, 0, LINEAR), K(70, 0.95f, IN), K(150, 0, OUT), K(260, 0, LINEAR), K(330, 0.95f, IN),
                                    K(410, 0, OUT), K(500, 0, LINEAR), K(570, 0.95f, IN), K(650, 0, OUT), K(1000, 0, LINEAR) };
static const lark_key_t bf_open[] = { K(0, 0.1f, LINEAR), K(700, 0.1f, LINEAR), K(1000, 0, IN_OUT) };
static const lark_key_t bf_lid[] = { K(0, -0.2f, LINEAR), K(700, -0.2f, LINEAR), K(1000, 0, IN_OUT) };
static const lark_track_t blinkflurry[] = { TRACK(CH_CLOSE, bf_cl), TRACK(CH_OPEN, bf_open), TRACK(CH_NARROW, bf_lid) };

/* judge (one-shot): sizing you up - a slow look from top to bottom and
 * back, lids flat. */
static const lark_key_t ju_gy[] = { K(0, -6.0f, LINEAR), K(900, 11.0f, IN_OUT), K(1300, 11.0f, LINEAR), K(2100, -2.0f, IN_OUT),
                                    K(2600, 0, IN_OUT) };
static const lark_key_t ju_lid[] = { K(0, 0.3f, LINEAR), K(2100, 0.3f, LINEAR), K(2600, 0, IN_OUT) };
static const lark_key_t ju_fy[] = { K(0, -6.0f, LINEAR), K(2600, 0, IN_OUT) };
static const lark_key_t ju_pu[] = { K(0, -0.2f, LINEAR) };
static const lark_key_t judge_la[] = { K(0, 0.25f, LINEAR) };
static const lark_track_t judge[] = { TRACK(CH_LID_ANGLE, judge_la), TRACK(CH_GAZE_Y, ju_gy), TRACK(CH_NARROW, ju_lid), TRACK(CH_FACE_Y, ju_fy),
                                      TRACK(CH_PUPIL, ju_pu) };

/* relief (one-shot): the tension goes - lids drop in a long exhale, then
 * a soft smile. */
static const lark_key_t rl_cl[] = { K(0, 0, LINEAR), K(500, 0.6f, IN_OUT), K(1200, 0.6f, LINEAR), K(1700, 0, IN_OUT) };
static const lark_key_t rl_fy[] = { K(0, 0, LINEAR), K(300, -6.0f, IN_OUT), K(1000, 12.0f, IN_OUT), K(2200, 0, IN_OUT) };
static const lark_key_t rl_sm[] = { K(0, 0, LINEAR), K(1200, 0, LINEAR), K(1600, 0.3f, OUT), K(2200, 0, IN_OUT) };
static const lark_key_t relief_la[] = { K(0, -0.20f, LINEAR) };
static const lark_track_t relief[] = { TRACK(CH_LID_ANGLE, relief_la), TRACK(CH_CLOSE, rl_cl), TRACK(CH_FACE_Y, rl_fy), TRACK(CH_SMILE, rl_sm) };

/* worry: small darting looks, wide pupils, lids up, a slight tremble. */
static const lark_key_t wo_gx[] = { K(0, -5.0f, LINEAR), K(400, -5.0f, LINEAR), K(480, 6.0f, OUT), K(1000, 6.0f, LINEAR),
                                    K(1080, -2.0f, OUT), K(1600, -2.0f, LINEAR), K(1700, -5.0f, OUT), K(2200, -5.0f, LINEAR) };
static const lark_key_t wo_pu[] = { K(0, 0.45f, LINEAR) };
static const lark_key_t wo_lid[] = { K(0, -0.18f, LINEAR) };
static const lark_key_t wo_fx[] = { K(0, 0, LINEAR), K(70, 1.2f, LINEAR), K(140, -1.2f, LINEAR), K(210, 0, LINEAR) };
static const lark_key_t wo_cl[] = { K(0, 0.15f, LINEAR) };
static const lark_key_t worry_la[] = { K(0, -0.60f, LINEAR) };
static const lark_track_t worry[] = { TRACK(CH_LID_ANGLE, worry_la), TRACK(CH_GAZE_X, wo_gx), TRACK(CH_PUPIL, wo_pu), TRACK(CH_NARROW, wo_lid),
                                      TRACK(CH_FACE_X, wo_fx), TRACK(CH_CLOSE, wo_cl) };

/* glint (one-shot): a flicker of gold - she has an idea she likes. */
static const lark_key_t gt_gold[] = { K(0, 0, LINEAR), K(200, 0.6f, OUT), K(700, 0.6f, LINEAR), K(1200, 0, IN_OUT) };
static const lark_key_t gt_sh[] = { K(0, 0, LINEAR), K(200, 0.9f, OUT), K(1200, 0, IN_OUT) };
static const lark_key_t gt_lid[] = { K(0, 0.2f, LINEAR), K(1200, 0, IN_OUT) };
static const lark_key_t gt_sm[] = { K(0, 0, LINEAR), K(300, 0.25f, OUT), K(1000, 0.25f, LINEAR), K(1200, 0, IN_OUT) };
static const lark_key_t gt_gx[] = { K(0, 0, LINEAR), K(200, 7.0f, OUT), K(1000, 7.0f, LINEAR), K(1200, 0, IN_OUT) };
static const lark_track_t glint[] = { TRACK(CH_TINT_WARM, gt_gold), TRACK(CH_SHINE, gt_sh), TRACK(CH_NARROW, gt_lid),
                                      TRACK(CH_SMILE, gt_sm), TRACK(CH_GAZE_X, gt_gx) };

/* wakeup (one-shot): properly awake - two heavy blinks, then eyes open
 * wide for a moment, a look around. */
static const lark_key_t wu_cl[] = { K(0, 0.5f, LINEAR), K(300, 0.9f, IN), K(500, 0.2f, OUT), K(800, 0.85f, IN), K(1000, 0, OUT),
                                    K(2400, 0, LINEAR) };
static const lark_key_t wu_open[] = { K(0, 0, LINEAR), K(1000, 0, LINEAR), K(1150, 0.16f, OUT), K(1800, 0, IN_OUT) };
static const lark_key_t wu_gx[] = { K(0, 0, LINEAR), K(1300, 0, LINEAR), K(1500, -8.0f, OUT), K(1800, -8.0f, LINEAR),
                                    K(2000, 7.0f, OUT), K(2200, 7.0f, LINEAR), K(2400, 0, IN_OUT) };
static const lark_key_t wu_fy[] = { K(0, 8.0f, LINEAR), K(1000, 8.0f, LINEAR), K(1200, -6.0f, OUT), K(2400, 0, IN_OUT) };
static const lark_track_t wakeup[] = { TRACK(CH_CLOSE, wu_cl), TRACK(CH_OPEN, wu_open), TRACK(CH_GAZE_X, wu_gx),
                                       TRACK(CH_FACE_Y, wu_fy) };

/* sidelong: a long, slow side-eye held, then a slower return. */
static const lark_key_t sl_gx[] = { K(0, 0, LINEAR), K(700, 12.0f, IN_OUT), K(3000, 12.0f, LINEAR), K(4200, 0, IN_OUT) };
static const lark_key_t sl_lid[] = { K(0, 0.18f, LINEAR), K(3000, 0.28f, IN_OUT), K(4200, 0.1f, IN_OUT) };
static const lark_key_t sl_fx[] = { K(0, 0, LINEAR), K(4200, -6.0f, IN_OUT) };
static const lark_key_t sidelong_la[] = { K(0, 0.15f, LINEAR) };
static const lark_track_t sidelong[] = { TRACK(CH_LID_ANGLE, sidelong_la), TRACK(CH_GAZE_X, sl_gx), TRACK(CH_NARROW, sl_lid), TRACK(CH_FACE_X, sl_fx) };

/* focus: squinting hard at something small, leaning in. */
static const lark_key_t fo_lid[] = { K(0, 0.28f, LINEAR) };
static const lark_key_t fo_sm[] = { K(0, 0.25f, LINEAR) };
static const lark_key_t fo_fy[] = { K(0, 0, LINEAR), K(800, 14.0f, IN_OUT), K(3000, 16.0f, IN_OUT) };
static const lark_key_t fo_gy[] = { K(0, 5.0f, LINEAR) };
static const lark_key_t fo_pu[] = { K(0, -0.3f, LINEAR) };
static const lark_key_t fo_gx[] = { K(0, 0, LINEAR), K(1200, 2.0f, IN_OUT), K(2000, -2.0f, IN_OUT), K(3000, 1.0f, IN_OUT) };
static const lark_key_t focus_la[] = { K(0, 0.20f, LINEAR) };
static const lark_track_t focus[] = { TRACK(CH_LID_ANGLE, focus_la), TRACK(CH_NARROW, fo_lid), TRACK(CH_SMILE, fo_sm), TRACK(CH_FACE_Y, fo_fy),
                                      TRACK(CH_GAZE_Y, fo_gy), TRACK(CH_PUPIL, fo_pu), TRACK(CH_GAZE_X, fo_gx) };

/* smallsurprise (one-shot): "oh" - a quick widening, then back. */
static const lark_key_t ss_open[] = { K(0, 0, LINEAR), K(90, 0.14f, OUT), K(600, 0, IN_OUT) };
static const lark_key_t ss_lid[] = { K(0, 0, LINEAR), K(90, -0.2f, OUT), K(700, 0, IN_OUT) };
static const lark_key_t ss_fy[] = { K(0, 0, LINEAR), K(100, -7.0f, OUT), K(700, 0, IN_OUT) };
static const lark_track_t smallsurprise[] = { TRACK(CH_OPEN, ss_open), TRACK(CH_NARROW, ss_lid), TRACK(CH_FACE_Y, ss_fy) };

/* wander: slow drifting looks with drifting lids - the mind elsewhere. */
static const lark_key_t wa_gx[] = { K(0, -7.0f, LINEAR), K(1700, 3.0f, IN_OUT), K(3100, 9.0f, IN_OUT), K(4600, -2.0f, IN_OUT),
                                    K(6000, -7.0f, IN_OUT) };
static const lark_key_t wa_gy[] = { K(0, -3.0f, LINEAR), K(1700, -6.0f, IN_OUT), K(3100, 1.0f, IN_OUT), K(4600, -4.0f, IN_OUT),
                                    K(6000, -3.0f, IN_OUT) };
static const lark_key_t wa_lid[] = { K(0, 0.08f, LINEAR), K(2200, 0.2f, IN_OUT), K(4000, 0.04f, IN_OUT), K(6000, 0.08f, IN_OUT) };
static const lark_track_t wander[] = { TRACK(CH_GAZE_X, wa_gx), TRACK(CH_GAZE_Y, wa_gy), TRACK(CH_NARROW, wa_lid) };

/* grumpywake (one-shot): woken and not pleased - squint, a slow blink, a
 * heavy-lidded look at you. */
static const lark_key_t gw_lid[] = { K(0, 0.45f, LINEAR), K(1800, 0.32f, IN_OUT), K(2600, 0, IN_OUT) };
static const lark_key_t gw_cl[] = { K(0, 0, LINEAR), K(700, 0.9f, IN_OUT), K(1100, 0, IN_OUT) };
static const lark_key_t gw_pu[] = { K(0, -0.3f, LINEAR), K(2600, 0, IN_OUT) };
static const lark_key_t gw_fy[] = { K(0, 8.0f, LINEAR), K(2600, 0, IN_OUT) };
static const lark_key_t grumpywake_la[] = { K(0, 0.40f, LINEAR) };
static const lark_track_t grumpywake[] = { TRACK(CH_LID_ANGLE, grumpywake_la), TRACK(CH_NARROW, gw_lid), TRACK(CH_CLOSE, gw_cl), TRACK(CH_PUPIL, gw_pu),
                                           TRACK(CH_FACE_Y, gw_fy) };

/* giggle (one-shot): smiling eyes that shake a little. */
static const lark_key_t gi_sm[] = { K(0, 0, LINEAR), K(150, 0.6f, OUT), K(1300, 0.6f, LINEAR), K(1700, 0, IN_OUT) };
static const lark_key_t gi_fy[] = { K(0, 0, LINEAR), K(120, -4.0f, OUT), K(240, 0, IN), K(360, -4.0f, OUT), K(480, 0, IN),
                                    K(600, -3.0f, OUT), K(720, 0, IN), K(840, -2.0f, OUT), K(960, 0, IN), K(1700, 0, LINEAR) };
static const lark_key_t gi_cl[] = { K(0, 0.25f, LINEAR), K(1300, 0.25f, LINEAR), K(1700, 0, IN_OUT) };
static const lark_track_t giggle[] = { TRACK(CH_SMILE, gi_sm), TRACK(CH_FACE_Y, gi_fy), TRACK(CH_CLOSE, gi_cl) };

/* stare: an unblinking, flat, patient stare straight at you. */
static const lark_key_t st_lid[] = { K(0, 0.24f, LINEAR) };
static const lark_key_t st_pu[] = { K(0, -0.15f, LINEAR) };
static const lark_key_t st_fy[] = { K(0, 0, LINEAR), K(3000, 6.0f, IN_OUT), K(6000, 0, IN_OUT) };
static const lark_key_t stare_la[] = { K(0, 0.10f, LINEAR) };
static const lark_track_t stare[] = { TRACK(CH_LID_ANGLE, stare_la), TRACK(CH_NARROW, st_lid), TRACK(CH_PUPIL, st_pu), TRACK(CH_FACE_Y, st_fy) };

const lark_state_t kLarkMore[] = {
    /*     name             length  in  curve   flags  base bored agit aff  next    tracks */
    STATE("read",            3600, 400, IN_OUT, 0,      5,   2,   0,  0, NULL,   read_),
    STATE("count",           2700, 300, IN_OUT, ONE,    4,   1,   0,  0, NULL,   count_),
    STATE("doubt",           2000, 200, OUT,    ONE,    3,   2,   3,  0, NULL,   doubt),
    STATE("listen",          4000, 600, IN_OUT, 0,      4,   2,   0,  0, NULL,   listen),
    STATE("calculate",       1440, 250, OUT,    0,      4,   0,   0,  0, NULL,   calculate),
    STATE("zoned",           5000, 900, IN_OUT, 0,      1,   8,   0,  0, NULL,   zoned),
    STATE("nodoff",          3200, 300, IN,     ONE,    0,  10,   0,  0, NULL,   nodoff),
    STATE("sneeze",          1600, 100, IN,     ONE,    1,   0,   0,  0, NULL,   sneeze),
    STATE("hiccup",          3000, 200, OUT,    0,      1,   1,   0,  0, NULL,   hiccup),
    STATE("shy",             2600, 200, OUT,    ONE,    0,   0,   0,  8, NULL,   shy),
    STATE("proud",           3000, 500, IN_OUT, 0,      2,   0,   0,  5, NULL,   proud),
    STATE("crosseyed",       1750, 200, IN_OUT, ONE,    1,   2,   0,  1, NULL,   crosseyed),
    STATE("blinkflurry",     1000, 100, OUT,    ONE,    1,   0,   3,  0, NULL,   blinkflurry),
    STATE("judge",           2600, 300, IN_OUT, ONE,    2,   1,   5,  0, NULL,   judge),
    STATE("relief",          2200, 300, IN_OUT, ONE,    1,   0,   2,  2, NULL,   relief),
    STATE("worry",           2200, 300, OUT,    0,      0,   0,   8,  0, NULL,   worry),
    STATE("glint",           1200, 120, OUT,    ONE,    2,   0,   0,  3, NULL,   glint),
    STATE("wakeup",          2400, 200, OUT,    EVT,    0,   0,   0,  0, NULL,   wakeup),
    STATE("sidelong",        4200, 500, IN_OUT, ONE,    3,   3,   2,  0, NULL,   sidelong),
    STATE("focus",           3000, 500, IN_OUT, 0,      4,   0,   0,  0, NULL,   focus),
    STATE("smallsurprise",    700,  60, OUT,    ONE,    2,   0,   1,  0, NULL,   smallsurprise),
    STATE("wander",          6000, 900, IN_OUT, 0,      5,   5,   0,  0, NULL,   wander),
    STATE("grumpywake",      2600, 200, IN_OUT, EVT,    0,   0,   0,  0, NULL,   grumpywake),
    STATE("giggle",          1700, 150, OUT,    ONE,    0,   0,   0,  6, NULL,   giggle),
    STATE("stare",           6000, 700, IN_OUT, 0,      3,   2,   2,  0, NULL,   stare),
};
const int kLarkMoreCount = (int)(sizeof(kLarkMore) / sizeof(kLarkMore[0]));
