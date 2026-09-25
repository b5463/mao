/*
 * MAO's Lark-style library, part 3: cat mode - Maomao's cat-ear gag (the
 * anime draws her with cat ears whenever her curiosity takes over). In cat
 * mode the eyes change shape: almond eyes and slit pupils that open up
 * round when hunting or startled - eyes only, no extras on screen.
 * The inner life (mao_life.c) turns cat mode on (very rarely) and plays these.
 * Units: see mao_lark_author.h.
 */
#include "mao_lark_author.h"


/* cat_curious: head cocked, pupils rounding. */
static const lark_key_t cc_tilt[] = { K(0, 0, LINEAR), K(300, 4.0f, BACK), K(1400, 4.0f, LINEAR), K(1700, -3.0f, BACK),
                                      K(2600, -3.0f, LINEAR) };
static const lark_key_t cc_slit[] = { K(0, -0.4f, LINEAR) };
static const lark_key_t cc_pu[] = { K(0, 0.2f, LINEAR) };
static const lark_key_t cc_lid[] = { K(0, -0.26f, LINEAR) };
static const lark_track_t cat_curious[] = { TRACK(CH_TILT, cc_tilt), TRACK(CH_SLIT, cc_slit), TRACK(CH_PUPIL, cc_pu),
                                            TRACK(CH_NARROW, cc_lid) };

/* cat_hunt: locked on (the life layer moves the gaze) - ears forward,
 * pupils wide and round, crouched low, eyes a touch taller. */
static const lark_key_t ch_slit[] = { K(0, -0.75f, LINEAR) };
static const lark_key_t ch_pu[] = { K(0, 0.55f, LINEAR) };
static const lark_key_t ch_fy[] = { K(0, 12.0f, LINEAR) };
static const lark_key_t ch_eh[] = { K(0, 0.08f, LINEAR) };
static const lark_key_t ch_lid[] = { K(0, -0.26f, LINEAR) };
static const lark_track_t cat_hunt[] = { TRACK(CH_SLIT, ch_slit), TRACK(CH_PUPIL, ch_pu), TRACK(CH_FACE_Y, ch_fy),
                                         TRACK(CH_EYE_H, ch_eh), TRACK(CH_NARROW, ch_lid) };

/* cat_pounce (one-shot): the wiggle, the leap, the landing. */
static const lark_key_t cp_fx[] = { K(0, 0, LINEAR), K(80, 3.0f, LINEAR), K(160, -3.0f, LINEAR), K(240, 3.0f, LINEAR),
                                    K(320, -3.0f, LINEAR), K(400, 3.0f, LINEAR), K(480, 0, LINEAR) };
static const lark_key_t cp_fy[] = { K(0, 12.0f, LINEAR), K(520, 16.0f, IN), K(680, -34.0f, OUT), K(900, 10.0f, IN),
                                    K(1300, 0, IN_OUT) };
static const lark_key_t cp_sq[] = { K(0, 0.1f, LINEAR), K(520, 0.3f, IN), K(620, -0.3f, OUT), K(900, 0.35f, IN), K(1100, 0, BACK) };
static const lark_key_t cp_slit[] = { K(0, -0.75f, LINEAR), K(1000, -0.75f, LINEAR), K(1300, 0, IN_OUT) };
static const lark_key_t cp_pu[] = { K(0, 0.6f, LINEAR), K(1000, 0.6f, LINEAR), K(1300, 0, IN_OUT) };
static const lark_key_t cp_gy[] = { K(0, 0, LINEAR), K(620, 10.0f, OUT), K(1300, 0, IN_OUT) };
static const lark_track_t cat_pounce[] = { TRACK(CH_FACE_X, cp_fx), TRACK(CH_FACE_Y, cp_fy),
                                           TRACK(CH_SQUASH, cp_sq), TRACK(CH_SLIT, cp_slit), TRACK(CH_PUPIL, cp_pu), TRACK(CH_GAZE_Y, cp_gy) };

/* cat_loaf: tucked in, content, eyes half shut and smiling, slow breaths. */
static const lark_key_t cl_cl[] = { K(0, 0.5f, LINEAR), K(3000, 0.6f, IN_OUT), K(6000, 0.5f, IN_OUT) };
static const lark_key_t cl_sm[] = { K(0, 0.3f, LINEAR) };
static const lark_key_t cl_fy[] = { K(0, 10.0f, LINEAR), K(3000, 13.0f, IN_OUT), K(6000, 10.0f, IN_OUT) };
static const lark_key_t cl_ew[] = { K(0, 0.1f, LINEAR) };
static const lark_track_t cat_loaf[] = { TRACK(CH_CLOSE, cl_cl), TRACK(CH_SMILE, cl_sm), TRACK(CH_FACE_Y, cl_fy),
                                         TRACK(CH_EYE_W, cl_ew) };

/* cat_groom (one-shot): eyes shut, little licking bobs, an ear flick. */
static const lark_key_t cg_cl[] = { K(0, 0, LINEAR), K(250, 0.92f, IN_OUT), K(2700, 0.92f, LINEAR), K(3000, 0, IN_OUT) };
static const lark_key_t cg_fy[] = { K(0, 0, LINEAR), K(300, 8.0f, IN_OUT), K(450, 3.0f, OUT), K(600, 8.0f, IN), K(750, 3.0f, OUT),
                                    K(900, 8.0f, IN), K(1050, 3.0f, OUT), K(1200, 8.0f, IN), K(1350, 3.0f, OUT),
                                    K(1500, 8.0f, IN), K(2600, 8.0f, LINEAR), K(3000, 0, IN_OUT) };
static const lark_key_t cg_tilt[] = { K(0, 0, LINEAR), K(300, 5.0f, IN_OUT), K(2600, 5.0f, LINEAR), K(3000, 0, IN_OUT) };
static const lark_track_t cat_groom[] = { TRACK(CH_CLOSE, cg_cl), TRACK(CH_FACE_Y, cg_fy),
                                          TRACK(CH_TILT, cg_tilt) };

/* cat_stretch (one-shot): ears back, eyes squeezed, low then long and up. */
static const lark_key_t cs_cl[] = { K(0, 0, LINEAR), K(500, 0.7f, IN_OUT), K(1900, 0.7f, LINEAR), K(2500, 0, IN_OUT) };
static const lark_key_t cs_sm[] = { K(0, 0, LINEAR), K(500, 0.45f, IN_OUT), K(1900, 0.45f, LINEAR), K(2500, 0, IN_OUT) };
static const lark_key_t cs_fy[] = { K(0, 0, LINEAR), K(700, 16.0f, IN_OUT), K(1600, -20.0f, IN_OUT), K(2600, 0, IN_OUT) };
static const lark_key_t cs_ew[] = { K(0, 0, LINEAR), K(700, 0.18f, IN_OUT), K(1600, 0.02f, IN_OUT), K(2600, 0, IN_OUT) };
static const lark_key_t cs_eh[] = { K(0, 0, LINEAR), K(700, -0.2f, IN_OUT), K(1600, 0.05f, IN_OUT), K(2600, 0, IN_OUT) };
static const lark_track_t cat_stretch[] = { TRACK(CH_CLOSE, cs_cl), TRACK(CH_SMILE, cs_sm), TRACK(CH_FACE_Y, cs_fy),
                                            TRACK(CH_EYE_W, cs_ew), TRACK(CH_EYE_H, cs_eh) };

/* cat_annoyed: ears pinned back, thin slits, a flat stare, a flick. */
static const lark_key_t ca_slit[] = { K(0, 0.25f, LINEAR) };
static const lark_key_t ca_lid[] = { K(0, 0.22f, LINEAR) };
static const lark_key_t ca_pu[] = { K(0, -0.25f, LINEAR) };
static const lark_key_t ca_eh[] = { K(0, -0.08f, LINEAR) };
static const lark_key_t cat_annoyed_la[] = { K(0, 0.50f, LINEAR) };
static const lark_track_t cat_annoyed[] = { TRACK(CH_LID_ANGLE, cat_annoyed_la), TRACK(CH_SLIT, ca_slit), TRACK(CH_NARROW, ca_lid), TRACK(CH_PUPIL, ca_pu),
                                            TRACK(CH_EYE_H, ca_eh) };

/* cat_startle (one-shot): ears flat, huge round pupils, a jump. */
static const lark_key_t cx_slit[] = { K(0, -0.75f, LINEAR), K(1100, -0.75f, LINEAR), K(1500, 0, IN_OUT) };
static const lark_key_t cx_pu[] = { K(0, 0.9f, LINEAR), K(1100, 0.9f, LINEAR), K(1500, 0, IN_OUT) };
static const lark_key_t cx_open[] = { K(0, 0.25f, LINEAR), K(1100, 0.2f, LINEAR), K(1500, 0, IN_OUT) };
static const lark_key_t cx_lid[] = { K(0, -0.26f, LINEAR), K(1100, -0.26f, LINEAR), K(1500, 0, IN_OUT) };
static const lark_key_t cx_fy[] = { K(0, 0, LINEAR), K(90, -26.0f, OUT), K(400, -18.0f, IN_OUT), K(1500, 0, IN_OUT) };
static const lark_key_t cx_eh[] = { K(0, 0.1f, LINEAR), K(1100, 0.1f, LINEAR), K(1500, 0, IN_OUT) };
static const lark_track_t cat_startle[] = { TRACK(CH_SLIT, cx_slit), TRACK(CH_PUPIL, cx_pu), TRACK(CH_OPEN, cx_open),
                                            TRACK(CH_NARROW, cx_lid), TRACK(CH_FACE_Y, cx_fy), TRACK(CH_EYE_H, cx_eh) };

/* cat_zoomies (one-shot): a sudden mad dash from side to side. */
static const lark_key_t cz_fx[] = { K(0, 0, LINEAR), K(180, 30.0f, OUT), K(420, -30.0f, IN_OUT), K(660, 26.0f, IN_OUT),
                                    K(900, -22.0f, IN_OUT), K(1200, 12.0f, IN_OUT), K(1600, 0, BACK), K(2400, 0, LINEAR) };
static const lark_key_t cz_gx[] = { K(0, 0, LINEAR), K(120, 12.0f, OUT), K(360, -12.0f, OUT), K(600, 12.0f, OUT),
                                    K(840, -12.0f, OUT), K(1140, 8.0f, OUT), K(1600, 0, IN_OUT) };
static const lark_key_t cz_pu[] = { K(0, 0.6f, LINEAR), K(1600, 0.6f, LINEAR), K(2400, 0, IN_OUT) };
static const lark_key_t cz_slit[] = { K(0, -0.6f, LINEAR), K(1600, -0.6f, LINEAR), K(2400, 0, IN_OUT) };
static const lark_track_t cat_zoomies[] = { TRACK(CH_FACE_X, cz_fx), TRACK(CH_GAZE_X, cz_gx), TRACK(CH_PUPIL, cz_pu),
                                            TRACK(CH_SLIT, cz_slit) };

/* cat_slowblink (one-shot): "I trust you", in cat. */
static const lark_key_t cb_cl[] = { K(0, 0, LINEAR), K(500, 0.95f, IN_OUT), K(1100, 0.95f, LINEAR), K(1800, 0, IN_OUT) };
static const lark_key_t cb_sm[] = { K(0, 0, LINEAR), K(500, 0.3f, IN_OUT), K(1100, 0.3f, LINEAR), K(1800, 0, IN_OUT) };
static const lark_track_t cat_slowblink[] = { TRACK(CH_CLOSE, cb_cl), TRACK(CH_SMILE, cb_sm) };

/* cat_purr: smiling slits, ears soft, a fine vibration. */
static const lark_key_t cr_sm[] = { K(0, 0.88f, LINEAR) };
static const lark_key_t cr_cl[] = { K(0, 0.0f, LINEAR) };
static const lark_key_t cr_fx[] = { K(0, 0, LINEAR), K(45, 1.6f, LINEAR), K(90, -1.6f, LINEAR), K(135, 0, LINEAR) };
static const lark_track_t cat_purr[] = { TRACK(CH_SMILE, cr_sm), TRACK(CH_CLOSE, cr_cl),
                                         TRACK(CH_FACE_X, cr_fx) };

/* The rest follow the anime's cat-mode frames directly. */

/* cat_deadpan: her most common cat face - heavy flat lids, small blue
 * irises with thin slits, ears straight up, not impressed. */
static const lark_key_t cd_lid[] = { K(0, 0.34f, LINEAR) };
static const lark_key_t cd_pu[] = { K(0, -0.3f, LINEAR) };
static const lark_key_t cd_slit[] = { K(0, 0.25f, LINEAR) };
static const lark_key_t cd_gx[] = { K(0, 0, LINEAR), K(2400, 0, LINEAR), K(2700, 6.0f, IN_OUT), K(4600, 6.0f, LINEAR),
                                    K(4900, 0, IN_OUT), K(6000, 0, LINEAR) };
static const lark_track_t cat_deadpan[] = { TRACK(CH_NARROW, cd_lid), TRACK(CH_PUPIL, cd_pu),
                                            TRACK(CH_SLIT, cd_slit), TRACK(CH_GAZE_X, cd_gx) };

/* cat_content ("=w="): eyes shut to flat lines, ears soft, a slow sway. */
static const lark_key_t co_open[] = { K(0, -0.96f, LINEAR) };
static const lark_key_t co_tilt[] = { K(0, -2.0f, LINEAR), K(1800, 2.0f, IN_OUT), K(3600, -2.0f, IN_OUT) };
static const lark_key_t co_fy[] = { K(0, 4.0f, LINEAR), K(1800, 6.0f, IN_OUT), K(3600, 4.0f, IN_OUT) };
static const lark_track_t cat_content[] = { TRACK(CH_OPEN, co_open), TRACK(CH_TILT, co_tilt), TRACK(CH_FACE_Y, co_fy) };

/* cat_blank (one-shot): caught out - huge dark blank irises, no light in
 * them, ears bolt upright, frozen stiff. */
static const lark_key_t cn_pu[] = { K(0, 0, LINEAR), K(120, 1.1f, OUT), K(2000, 1.1f, LINEAR), K(2400, 0, IN_OUT) };
static const lark_key_t cn_dk[] = { K(0, 0, LINEAR), K(120, 1.0f, OUT), K(2000, 1.0f, LINEAR), K(2400, 0, IN_OUT) };
static const lark_key_t cn_slit[] = { K(0, -0.75f, LINEAR) };
static const lark_key_t cn_lid[] = { K(0, -0.26f, LINEAR) };
static const lark_key_t cn_fy[] = { K(0, 0, LINEAR), K(100, -6.0f, OUT), K(2400, 0, IN_OUT) };
static const lark_track_t cat_blank[] = { TRACK(CH_PUPIL, cn_pu), TRACK(CH_DARK, cn_dk),
                                          TRACK(CH_SLIT, cn_slit), TRACK(CH_NARROW, cn_lid), TRACK(CH_FACE_Y, cn_fy) };

/* cat_greed: something precious - dark irises with a gold four-point star,
 * ears perked, little paw-bounces. */
static const lark_key_t cgr_dk[] = { K(0, 0.9f, LINEAR) };
static const lark_key_t cgr_st[] = { K(0, 0.9f, LINEAR), K(300, 1.15f, IN_OUT), K(600, 0.9f, IN_OUT), K(900, 1.15f, IN_OUT),
                                     K(1200, 0.9f, IN_OUT) };
static const lark_key_t cgr_pu[] = { K(0, 0.5f, LINEAR) };
static const lark_key_t cgr_slit[] = { K(0, -0.75f, LINEAR) };
static const lark_key_t cgr_lid[] = { K(0, -0.26f, LINEAR) };
static const lark_key_t cgr_fy[] = { K(0, -6.0f, LINEAR), K(150, -14.0f, OUT), K(300, -6.0f, IN), K(450, -12.0f, OUT),
                                     K(600, -6.0f, IN), K(1200, -6.0f, LINEAR) };
static const lark_track_t cat_greed[] = { TRACK(CH_DARK, cgr_dk), TRACK(CH_STAR, cgr_st),
                                          TRACK(CH_PUPIL, cgr_pu), TRACK(CH_SLIT, cgr_slit), TRACK(CH_NARROW, cgr_lid), TRACK(CH_FACE_Y, cgr_fy) };

/* cat_glare: hiding behind a sleeve and glaring - sunk low, narrowed, thin
 * slits, ears half back. */
static const lark_key_t cgl_lid[] = { K(0, 0.30f, LINEAR) };
static const lark_key_t cgl_slit[] = { K(0, 0.25f, LINEAR) };
static const lark_key_t cgl_pu[] = { K(0, -0.25f, LINEAR) };
static const lark_key_t cgl_fy[] = { K(0, 18.0f, LINEAR) };
static const lark_key_t cgl_sm[] = { K(0, 0.2f, LINEAR) };
static const lark_key_t cat_glare_la[] = { K(0, 0.50f, LINEAR) };
static const lark_track_t cat_glare[] = { TRACK(CH_LID_ANGLE, cat_glare_la), TRACK(CH_NARROW, cgl_lid), TRACK(CH_SLIT, cgl_slit),
                                          TRACK(CH_PUPIL, cgl_pu), TRACK(CH_FACE_Y, cgl_fy), TRACK(CH_SMILE, cgl_sm) };

/* cat_eat (one-shot): something tasty - eyes closed in soft curves, a
 * happy chewing bob. */
static const lark_key_t ce_cl[] = { K(0, 0, LINEAR), K(200, 0.93f, OUT), K(2400, 0.93f, LINEAR), K(2800, 0, IN_OUT) };
static const lark_key_t ce_fy[] = { K(0, 0, LINEAR), K(300, 4.0f, IN_OUT), K(500, 0, IN_OUT), K(700, 4.0f, IN_OUT),
                                    K(900, 0, IN_OUT), K(1100, 4.0f, IN_OUT), K(1300, 0, IN_OUT), K(1500, 4.0f, IN_OUT),
                                    K(1700, 0, IN_OUT), K(2800, 0, LINEAR) };
static const lark_key_t ce_tilt[] = { K(0, 0, LINEAR), K(300, 3.0f, OUT), K(2400, 3.0f, LINEAR), K(2800, 0, IN_OUT) };
static const lark_track_t cat_eat[] = { TRACK(CH_CLOSE, ce_cl), TRACK(CH_FACE_Y, ce_fy), TRACK(CH_TILT, ce_tilt) };

#define CAT LARK_NO_PICK                  /* cat states belong to cat mode */
#define CAT_ONE (LARK_ONESHOT | LARK_NO_PICK)

const lark_state_t kLarkCat[] = {
    /*     name             length  in  curve   flags    base bored agit aff  next   tracks */
    STATE("cat_curious",     2600, 200, OUT,    CAT_ONE,  0,   0,   0,  0, NULL,  cat_curious),
    STATE("cat_hunt",        2000, 250, OUT,    CAT,      0,   0,   0,  0, NULL,  cat_hunt),
    STATE("cat_pounce",      1300, 100, OUT,    CAT_ONE,  0,   0,   0,  0, NULL,  cat_pounce),
    STATE("cat_loaf",        6000, 900, IN_OUT, CAT,      0,   0,   0,  0, NULL,  cat_loaf),
    STATE("cat_groom",       3000, 300, IN_OUT, CAT_ONE,  0,   0,   0,  0, NULL,  cat_groom),
    STATE("cat_stretch",     2600, 300, IN_OUT, CAT_ONE,  0,   0,   0,  0, NULL,  cat_stretch),
    STATE("cat_annoyed",     2400, 200, OUT,    CAT,      0,   0,   0,  0, NULL,  cat_annoyed),
    STATE("cat_startle",     1500,  60, OUT,    CAT_ONE,  0,   0,   0,  0, NULL,  cat_startle),
    STATE("cat_zoomies",     2400, 100, OUT,    CAT_ONE,  0,   0,   0,  0, NULL,  cat_zoomies),
    STATE("cat_slowblink",   1800, 200, IN_OUT, CAT_ONE,  0,   0,   0,  0, NULL,  cat_slowblink),
    STATE("cat_purr",        3200, 300, IN_OUT, CAT,      0,   0,   0,  0, NULL,  cat_purr),
    STATE("cat_deadpan",     6000, 600, IN_OUT, CAT,      0,   0,   0,  0, NULL,  cat_deadpan),
    STATE("cat_content",     3600, 500, IN_OUT, CAT,      0,   0,   0,  0, NULL,  cat_content),
    STATE("cat_blank",       2400,  60, OUT,    CAT_ONE,  0,   0,   0,  0, NULL,  cat_blank),
    STATE("cat_greed",       1200, 150, OUT,    CAT,      0,   0,   0,  0, NULL,  cat_greed),
    STATE("cat_glare",       3000, 400, IN_OUT, CAT,      0,   0,   0,  0, NULL,  cat_glare),
    STATE("cat_eat",         2800, 200, OUT,    CAT_ONE,  0,   0,   0,  0, NULL,  cat_eat),
};
const int kLarkCatCount = (int)(sizeof(kLarkCat) / sizeof(kLarkCat[0]));
