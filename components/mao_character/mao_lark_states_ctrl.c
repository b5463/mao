/*
 * MAO's Lark-style library, part 6: controller feedback. MAO is the
 * universal controller for ODD JOBS products; the character is its face, so
 * it reports what the controller is doing. The tone is Maomao's: dry,
 * restrained, hard to impress. ACK means "understood", not celebration;
 * BUSY reads "of course it's busy"; DONE is quiet satisfaction; FAIL begins
 * as analysis (the mind runs an evaluation first) and ends aimed at the
 * device, never at the user. Played by mao_character_react() at full
 * strength, even while the dial is in use, and never chosen by the idle
 * behaviour. Units: see mao_lark_author.h.
 */
#include "mao_lark_author.h"

/* ack: "understood." - a small focus, tiny lid soften, settle. */
static const lark_key_t ak_fy[] = { K(0, 0, LINEAR), K(110, 4.0f, OUT), K(300, -1.5f, IN_OUT), K(480, 0, IN_OUT) };
static const lark_key_t ak_lid[] = { K(0, 0, LINEAR), K(90, -0.10f, OUT), K(320, -0.10f, LINEAR), K(520, 0, IN_OUT) };
static const lark_key_t ak_pu[] = { K(0, 0, LINEAR), K(90, 0.16f, OUT), K(520, 0, IN_OUT) };
static const lark_track_t ack[] = { TRACK(CH_FACE_Y, ak_fy), TRACK(CH_NARROW, ak_lid), TRACK(CH_PUPIL, ak_pu) };

/* busy: "of course it's busy." - a dry side glance held on the work, the
 * lids a touch flat and angled (skeptical), an occasional slow check back;
 * looped until the work ends. No panic. */
static const lark_key_t bz_gx[] = { K(0, 6.0f, LINEAR), K(700, 6.0f, LINEAR), K(880, 2.0f, IN_OUT),
                                    K(1150, 6.0f, IN_OUT), K(1650, 6.0f, LINEAR) };
static const lark_key_t bz_lid[] = { K(0, 0.22f, LINEAR) };
static const lark_key_t bz_la[] = { K(0, 0.16f, LINEAR) };
static const lark_key_t bz_pu[] = { K(0, -0.12f, LINEAR) };
static const lark_track_t busy[] = { TRACK(CH_GAZE_X, bz_gx), TRACK(CH_NARROW, bz_lid), TRACK(CH_LID_ANGLE, bz_la),
                                     TRACK(CH_PUPIL, bz_pu) };

/* done: quietly satisfied - the lower lids rise a little, one small soft
 * blink, a tiny vertical settle. Nothing to celebrate; it merely worked. */
static const lark_key_t dn_sm[] = { K(0, 0, LINEAR), K(180, 0.24f, OUT), K(650, 0.24f, LINEAR), K(1000, 0, IN_OUT) };
static const lark_key_t dn_cl[] = { K(0, 0, LINEAR), K(140, 0.55f, OUT), K(300, 0, IN_OUT) };
static const lark_key_t dn_fy[] = { K(0, 0, LINEAR), K(150, -4.0f, OUT), K(360, 1.5f, IN_OUT), K(560, 0, IN_OUT) };
static const lark_track_t done[] = { TRACK(CH_SMILE, dn_sm), TRACK(CH_CLOSE, dn_cl), TRACK(CH_FACE_Y, dn_fy) };

/* fail: the verdict after the analysis (mao_life ran the EVALUATE phase
 * before this plays): lids flatten, a restrained small head shake, then a
 * flat "apparently not" look. No red - an unavailable device is not a
 * catastrophe - and the look is aimed at the device's side, not the user. */
static const lark_key_t fl_lid[] = { K(0, 0, LINEAR), K(140, 0.30f, OUT), K(950, 0.30f, LINEAR), K(1500, 0, IN_OUT) };
static const lark_key_t fl_la[] = { K(0, 0, LINEAR), K(140, 0.22f, OUT), K(950, 0.16f, LINEAR), K(1500, 0, IN_OUT) };
static const lark_key_t fl_fx[] = { K(0, 0, LINEAR), K(260, 0, LINEAR), K(350, -6.0f, OUT), K(470, 5.0f, IN_OUT),
                                    K(590, -3.0f, IN_OUT), K(720, 0, BACK) };
static const lark_key_t fl_gx[] = { K(0, 6.0f, LINEAR), K(700, 6.0f, LINEAR), K(1150, 3.0f, IN_OUT), K(1500, 0, IN_OUT) };
static const lark_key_t fl_pu[] = { K(0, -0.15f, LINEAR), K(1500, 0, IN_OUT) };
static const lark_track_t fail[] = { TRACK(CH_NARROW, fl_lid), TRACK(CH_LID_ANGLE, fl_la), TRACK(CH_FACE_X, fl_fx),
                                     TRACK(CH_GAZE_X, fl_gx), TRACK(CH_PUPIL, fl_pu) };

/* back: disengaging - eyes leave the target, a small backwards drift, home. */
static const lark_key_t bk_gx[] = { K(0, 0, LINEAR), K(160, -9.0f, OUT), K(420, -9.0f, LINEAR), K(700, 0, IN_OUT) };
static const lark_key_t bk_fx[] = { K(0, 0, LINEAR), K(200, -6.0f, OUT), K(700, 0, IN_OUT) };
static const lark_track_t back[] = { TRACK(CH_GAZE_X, bk_gx), TRACK(CH_FACE_X, bk_fx) };

/* device_on: a new device - analytical curiosity, not a party. The eyes
 * orient to the rim and hold there, open a touch, pupils engaged; the mind's
 * EVALUATE phase and the later second look do the rest. */
static const lark_key_t do_gx[] = { K(0, 0, LINEAR), K(150, 11.0f, OUT), K(1050, 11.0f, LINEAR), K(1350, 0, IN_OUT) };
static const lark_key_t do_gy[] = { K(0, 0, LINEAR), K(150, -4.0f, OUT), K(1050, -4.0f, LINEAR), K(1350, 0, IN_OUT) };
static const lark_key_t do_open[] = { K(0, 0, LINEAR), K(110, 0.12f, OUT), K(800, 0.06f, IN_OUT), K(1350, 0, IN_OUT) };
static const lark_key_t do_fy[] = { K(0, 0, LINEAR), K(170, -6.0f, OUT), K(400, 0, IN), K(1350, 0, LINEAR) };
static const lark_key_t do_pu[] = { K(0, 0, LINEAR), K(150, 0.35f, OUT), K(1350, 0.1f, IN_OUT) };
static const lark_track_t device_on[] = { TRACK(CH_GAZE_X, do_gx), TRACK(CH_GAZE_Y, do_gy), TRACK(CH_OPEN, do_open),
                                          TRACK(CH_FACE_Y, do_fy), TRACK(CH_PUPIL, do_pu) };

/* device_off: not sadness - a look toward where it was, a brief
 * confirmation, move on. */
static const lark_key_t df_gx[] = { K(0, 0, LINEAR), K(200, 10.0f, OUT), K(850, 10.0f, IN_OUT), K(1200, 0, IN_OUT) };
static const lark_key_t df_gy[] = { K(0, 0, LINEAR), K(200, 2.0f, OUT), K(1200, 0, IN_OUT) };
static const lark_key_t df_la[] = { K(0, 0, LINEAR), K(320, -0.14f, IN_OUT), K(850, -0.14f, LINEAR), K(1200, 0, IN_OUT) };
static const lark_key_t df_cl[] = { K(0, 0, LINEAR), K(560, 0.5f, IN_OUT), K(760, 0, IN_OUT) };   /* one slow half-blink */
static const lark_track_t device_off[] = { TRACK(CH_GAZE_X, df_gx), TRACK(CH_GAZE_Y, df_gy), TRACK(CH_LID_ANGLE, df_la),
                                           TRACK(CH_CLOSE, df_cl) };

#define CTRL (LARK_ONESHOT | LARK_NO_PICK | LARK_NO_MIRROR)

const lark_state_t kLarkCtrl[] = {
    /*     name          length  in  curve   flags                 base bored agit aff  next   tracks */
    STATE("ack",           520,  50, OUT,    LARK_ONESHOT | LARK_NO_PICK, 0, 0, 0, 0, NULL, ack),
    STATE("busy",         1650, 200, OUT,    LARK_NO_PICK | LARK_NO_MIRROR, 0, 0, 0, 0, NULL, busy),
    STATE("done",         1000,  70, OUT,    LARK_ONESHOT | LARK_NO_PICK, 0, 0, 0, 0, NULL, done),
    STATE("fail",         1500,  60, OUT,    CTRL,                  0,   0,   0,  0,  NULL,  fail),
    STATE("back",          700,  60, OUT,    CTRL,                  0,   0,   0,  0,  NULL,  back),
    STATE("device_on",    1350,  70, OUT,    CTRL,                  0,   0,   0,  0,  NULL,  device_on),
    STATE("device_off",   1200, 110, IN_OUT, CTRL,                  0,   0,   0,  0,  NULL,  device_off),
};
const int kLarkCtrlCount = (int)(sizeof(kLarkCtrl) / sizeof(kLarkCtrl[0]));
