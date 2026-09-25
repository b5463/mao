/*
 * MAO's Lark-style library, part 4: impulses and social states - the things
 * the inner life (mao_life.c) does on its own, and how Maomao answers people:
 * tracking something only she can see, her "sniffing rampage", suddenly
 * remembering a poison, humming, "hmph", greeting you back, asking for
 * attention, being lonely, peeking up from below, looking smug, pouting.
 * Units: see mao_lark_author.h.
 */
#include "mao_lark_author.h"

/* track: something caught her eye (the life layer moves the gaze). */
static const lark_key_t tr_lid[] = { K(0, -0.14f, LINEAR) };
static const lark_key_t tr_pu[] = { K(0, 0.18f, LINEAR) };
static const lark_key_t tr_fy[] = { K(0, -4.0f, LINEAR) };
static const lark_track_t track[] = { TRACK(CH_NARROW, tr_lid), TRACK(CH_PUPIL, tr_pu), TRACK(CH_FACE_Y, tr_fy) };

/* remember (one-shot): musing... then it comes to her - a poison! Lids snap
 * open and the irises flash gold. */
static const lark_key_t rm_gx[] = { K(0, 0, LINEAR), K(400, 8.0f, IN_OUT), K(1300, 8.0f, LINEAR), K(1400, 0, OUT) };
static const lark_key_t rm_gy[] = { K(0, 0, LINEAR), K(400, -9.0f, IN_OUT), K(1300, -9.0f, LINEAR), K(1400, 0, OUT) };
static const lark_key_t rm_sq[] = { K(0, 0, LINEAR), K(400, -0.35f, IN_OUT), K(1300, -0.35f, LINEAR), K(1400, 0, OUT) };
static const lark_key_t rm_lid[] = { K(0, 0, LINEAR), K(1300, 0, LINEAR), K(1400, -0.26f, OUT), K(2300, -0.26f, LINEAR),
                                     K(2700, 0, IN_OUT) };
static const lark_key_t rm_gold[] = { K(0, 0, LINEAR), K(1350, 0, LINEAR), K(1500, 0.9f, OUT), K(2300, 0.9f, LINEAR),
                                      K(2700, 0, IN_OUT) };
static const lark_key_t rm_sh[] = { K(0, 0, LINEAR), K(1400, 0, LINEAR), K(1500, 1.2f, OUT), K(2300, 1.0f, LINEAR),
                                    K(2700, 0, IN_OUT) };
static const lark_key_t rm_fy[] = { K(0, 0, LINEAR), K(1400, 0, LINEAR), K(1550, -14.0f, OUT), K(2700, 0, IN_OUT) };
static const lark_key_t rm_pu[] = { K(0, 0, LINEAR), K(1400, 0, LINEAR), K(1500, 0.4f, OUT), K(2300, 0.4f, LINEAR),
                                    K(2700, 0, IN_OUT) };
static const lark_track_t remember[] = { TRACK(CH_GAZE_X, rm_gx), TRACK(CH_GAZE_Y, rm_gy), TRACK(CH_SQUINT, rm_sq),
                                         TRACK(CH_NARROW, rm_lid), TRACK(CH_TINT_WARM, rm_gold), TRACK(CH_SHINE, rm_sh),
                                         TRACK(CH_FACE_Y, rm_fy), TRACK(CH_PUPIL, rm_pu) };

/* hum: content, smiling eyes, a gentle sway to a tune only she hears. */
static const lark_key_t hu_sm[] = { K(0, 0.35f, LINEAR) };
static const lark_key_t hu_lid[] = { K(0, 0.08f, LINEAR) };
static const lark_key_t hu_tilt[] = { K(0, -2.5f, LINEAR), K(750, 2.5f, IN_OUT), K(1500, -2.5f, IN_OUT), K(2250, 2.5f, IN_OUT),
                                      K(3000, -2.5f, IN_OUT) };
static const lark_key_t hu_fx[] = { K(0, -6.0f, LINEAR), K(1500, 6.0f, IN_OUT), K(3000, -6.0f, IN_OUT) };
static const lark_key_t hu_fy[] = { K(0, 0, LINEAR), K(375, -3.0f, IN_OUT), K(750, 0, IN_OUT), K(1125, -3.0f, IN_OUT),
                                    K(1500, 0, IN_OUT), K(1875, -3.0f, IN_OUT), K(2250, 0, IN_OUT), K(2625, -3.0f, IN_OUT),
                                    K(3000, 0, IN_OUT) };
static const lark_track_t hum[] = { TRACK(CH_SMILE, hu_sm), TRACK(CH_NARROW, hu_lid), TRACK(CH_TILT, hu_tilt),
                                    TRACK(CH_FACE_X, hu_fx), TRACK(CH_FACE_Y, hu_fy) };

/* hmph (one-shot): turns away with her eyes shut, holds it, peeks back. */
static const lark_key_t hm_gx[] = { K(0, 0, LINEAR), K(200, -12.0f, OUT), K(1400, -12.0f, LINEAR), K(1500, -4.0f, OUT),
                                    K(1800, 0, IN_OUT) };
static const lark_key_t hm_fx[] = { K(0, 0, LINEAR), K(250, -22.0f, OUT), K(1400, -22.0f, LINEAR), K(1800, 0, IN_OUT) };
static const lark_key_t hm_cl[] = { K(0, 0, LINEAR), K(250, 0.8f, OUT), K(1200, 0.8f, LINEAR), K(1350, 0.2f, OUT),
                                    K(1800, 0, IN_OUT) };
static const lark_key_t hm_fy[] = { K(0, 0, LINEAR), K(250, -8.0f, OUT), K(1400, -8.0f, LINEAR), K(1800, 0, IN_OUT) };
static const lark_key_t hm_tilt[] = { K(0, 0, LINEAR), K(250, -3.0f, OUT), K(1400, -3.0f, LINEAR), K(1800, 0, IN_OUT) };
static const lark_key_t hmph_la[] = { K(0, 0.30f, LINEAR) };
static const lark_track_t hmph[] = { TRACK(CH_LID_ANGLE, hmph_la), TRACK(CH_GAZE_X, hm_gx), TRACK(CH_FACE_X, hm_fx), TRACK(CH_CLOSE, hm_cl),
                                     TRACK(CH_FACE_Y, hm_fy), TRACK(CH_TILT, hm_tilt) };

/* greet (event): you're back - a wide-eyed double take, then her rare
 * smile and two hops. */
static const lark_key_t gr_open[] = { K(0, 0, LINEAR), K(90, 0.22f, OUT), K(450, 0.2f, LINEAR), K(700, 0, IN_OUT) };
static const lark_key_t gr_lid[] = { K(0, -0.26f, LINEAR), K(700, -0.26f, LINEAR), K(900, -0.1f, IN_OUT) };
static const lark_key_t gr_pu[] = { K(0, 0, LINEAR), K(90, -0.35f, OUT), K(500, 0.3f, IN_OUT), K(2400, 0, IN_OUT) };
static const lark_key_t gr_sm[] = { K(0, 0, LINEAR), K(500, 0, LINEAR), K(700, 0.7f, OUT), K(1900, 0.7f, LINEAR),
                                    K(2400, 0, IN_OUT) };
static const lark_key_t gr_fy[] = { K(0, 0, LINEAR), K(90, -14.0f, OUT), K(500, -4.0f, IN_OUT), K(700, -16.0f, OUT),
                                    K(880, 0, IN), K(1040, -10.0f, OUT), K(1220, 0, IN), K(2400, 0, LINEAR) };
static const lark_key_t gr_sh[] = { K(0, 0, LINEAR), K(700, 0.6f, OUT), K(2400, 0, IN_OUT) };
static const lark_track_t greet[] = { TRACK(CH_OPEN, gr_open), TRACK(CH_NARROW, gr_lid), TRACK(CH_PUPIL, gr_pu),
                                      TRACK(CH_SMILE, gr_sm), TRACK(CH_FACE_Y, gr_fy), TRACK(CH_SHINE, gr_sh) };

/* seek (one-shot): looks right at you (the life layer centres the gaze),
 * pupils wide, a lean in, a slow blink, a little hop - "hey." */
static const lark_key_t se_lid[] = { K(0, -0.2f, LINEAR) };
static const lark_key_t se_pu[] = { K(0, 0.45f, LINEAR), K(2200, 0.45f, LINEAR), K(2600, 0, IN_OUT) };
static const lark_key_t se_fy[] = { K(0, 0, LINEAR), K(400, -8.0f, IN_OUT), K(2000, -8.0f, LINEAR), K(2150, -18.0f, OUT),
                                    K(2350, -4.0f, IN), K(2600, 0, IN_OUT) };
static const lark_key_t se_cl[] = { K(0, 0, LINEAR), K(1000, 0, LINEAR), K(1300, 0.95f, IN_OUT), K(1550, 0.95f, LINEAR),
                                    K(1850, 0, IN_OUT) };
static const lark_key_t se_sh[] = { K(0, 0.4f, LINEAR) };
static const lark_track_t seek[] = { TRACK(CH_NARROW, se_lid), TRACK(CH_PUPIL, se_pu), TRACK(CH_FACE_Y, se_fy),
                                     TRACK(CH_CLOSE, se_cl), TRACK(CH_SHINE, se_sh) };

/* lonely: drooping, looking down, now and then a hopeful glance up. */
static const lark_key_t lo_lid[] = { K(0, 0.26f, LINEAR), K(2800, 0.26f, LINEAR), K(3100, 0.05f, IN_OUT), K(3800, 0.05f, LINEAR),
                                     K(4200, 0.26f, IN_OUT), K(4500, 0.26f, LINEAR) };
static const lark_key_t lo_gy[] = { K(0, 8.0f, LINEAR), K(2800, 8.0f, LINEAR), K(3100, -3.0f, IN_OUT), K(3800, -3.0f, LINEAR),
                                    K(4200, 8.0f, IN_OUT), K(4500, 8.0f, LINEAR) };
static const lark_key_t lo_gx[] = { K(0, -5.0f, LINEAR), K(2800, -5.0f, LINEAR), K(3100, 0, IN_OUT), K(3800, 0, LINEAR),
                                    K(4200, -5.0f, IN_OUT), K(4500, -5.0f, LINEAR) };
static const lark_key_t lo_fy[] = { K(0, 12.0f, LINEAR), K(2250, 15.0f, IN_OUT), K(4500, 12.0f, IN_OUT) };
static const lark_key_t lo_pu[] = { K(0, 0.35f, LINEAR) };
static const lark_key_t lonely_la[] = { K(0, -0.50f, LINEAR) };
static const lark_track_t lonely[] = { TRACK(CH_LID_ANGLE, lonely_la), TRACK(CH_NARROW, lo_lid), TRACK(CH_GAZE_Y, lo_gy), TRACK(CH_GAZE_X, lo_gx),
                                       TRACK(CH_FACE_Y, lo_fy), TRACK(CH_PUPIL, lo_pu) };

/* peek (one-shot): sinks out of sight below the circle, then slowly rises
 * just far enough to peek, looks left and right, and pops back up. */
static const lark_key_t pk_aw[] = { K(0, 0, LINEAR), K(500, 190.0f, IN), K(1300, 190.0f, LINEAR), K(2200, 70.0f, IN_OUT),
                                    K(3300, 70.0f, LINEAR), K(3600, 0, BACK) };
static const lark_key_t pk_gx[] = { K(0, 0, LINEAR), K(2300, 0, LINEAR), K(2500, -10.0f, OUT), K(2900, -10.0f, LINEAR),
                                    K(3100, 10.0f, OUT), K(3300, 10.0f, LINEAR), K(3500, 0, OUT) };
static const lark_key_t pk_gy[] = { K(0, 0, LINEAR), K(1300, 0, LINEAR), K(2200, -8.0f, IN_OUT), K(3300, -8.0f, LINEAR),
                                    K(3600, 0, IN_OUT) };
static const lark_key_t pk_lid[] = { K(0, 0, LINEAR), K(1800, 0, LINEAR), K(2200, 0.15f, IN_OUT), K(3300, 0.15f, LINEAR),
                                     K(3500, -0.2f, OUT), K(3600, 0, IN_OUT) };
static const lark_track_t peek[] = { TRACK(CH_AWAY, pk_aw), TRACK(CH_GAZE_X, pk_gx), TRACK(CH_GAZE_Y, pk_gy),
                                     TRACK(CH_NARROW, pk_lid) };

/* smug: looking down her nose at you, lids heavy, a small smile. */
static const lark_key_t sm_lid[] = { K(0, 0.30f, LINEAR) };
static const lark_key_t sm_sm[] = { K(0, 0.35f, LINEAR) };
static const lark_key_t sm_fy[] = { K(0, -8.0f, LINEAR) };
static const lark_key_t sm_gy[] = { K(0, 5.0f, LINEAR) };
static const lark_key_t sm_tilt[] = { K(0, 2.0f, LINEAR), K(1500, 3.0f, IN_OUT), K(3000, 2.0f, IN_OUT) };
static const lark_track_t smug[] = { TRACK(CH_NARROW, sm_lid), TRACK(CH_SMILE, sm_sm), TRACK(CH_FACE_Y, sm_fy),
                                     TRACK(CH_GAZE_Y, sm_gy), TRACK(CH_TILT, sm_tilt) };

/* pout: puffed up, looking away and down, a glance back to check. */
static const lark_key_t po_lid[] = { K(0, 0.18f, LINEAR) };
static const lark_key_t po_gx[] = { K(0, -10.0f, LINEAR), K(2200, -10.0f, LINEAR), K(2400, 4.0f, OUT), K(2900, 4.0f, LINEAR),
                                    K(3100, -10.0f, OUT), K(3500, -10.0f, LINEAR) };
static const lark_key_t po_gy[] = { K(0, 5.0f, LINEAR) };
static const lark_key_t po_sq[] = { K(0, 0.12f, LINEAR) };
static const lark_key_t po_fx[] = { K(0, -12.0f, LINEAR) };
static const lark_key_t po_ew[] = { K(0, 0.06f, LINEAR) };
static const lark_key_t pout_la[] = { K(0, 0.30f, LINEAR) };
static const lark_track_t pout[] = { TRACK(CH_LID_ANGLE, pout_la), TRACK(CH_NARROW, po_lid), TRACK(CH_GAZE_X, po_gx), TRACK(CH_GAZE_Y, po_gy),
                                     TRACK(CH_SQUASH, po_sq), TRACK(CH_FACE_X, po_fx), TRACK(CH_EYE_W, po_ew) };

const lark_state_t kLarkLife[] = {
    /*     name          length  in  curve   flags          base bored agit aff  next    tracks */
    STATE("track",        2000, 150, OUT,    LARK_NO_PICK,   0,   0,  0,  0, NULL,    track),
    GEN("sniff", 200, IN_OUT, EVT, 0, 0, 0, 0, NULL, lark_gen_sniff),
    STATE("remember",     2700, 400, IN_OUT, EVT,            0,   0,  0,  0, NULL,    remember),
    STATE("hum",          3000, 600, IN_OUT, 0,              2,   0,  0,  4, NULL,    hum),
    STATE("hmph",         1800, 150, OUT,    EVT,            0,   0,  0,  0, NULL,    hmph),
    STATE("greet",        2400,  80, OUT,    EVT,            0,   0,  0,  0, NULL,    greet),
    STATE("seek",         2600, 300, OUT,    EVT,            0,   0,  0,  0, NULL,    seek),
    STATE("lonely",       4500, 900, IN_OUT, LARK_NO_PICK,   0,   0,  0,  0, NULL,    lonely),
    STATE("peek",         3600, 150, IN,     EVT,            0,   0,  0,  0, NULL,    peek),
    STATE("smug",         3000, 600, IN_OUT, 0,              2,   2,  0,  2, NULL,    smug),
    STATE("pout",         3500, 500, IN_OUT, 0,              0,   2,  4,  0, NULL,    pout),
};
const int kLarkLifeCount = (int)(sizeof(kLarkLife) / sizeof(kLarkLife[0]));
