/*
 * MAO's Lark-style library, part 6: controller feedback. MAO is the
 * universal controller for ODD JOBS products; the character is its face, so
 * it reports what the controller is doing: an acknowledgement when a command
 * is taken, a working look while something is in progress, success, a clear
 * (red) failure, going back, and devices arriving or leaving. These are
 * played by mao_character_react() at full strength, even while the dial is
 * in use, and are never chosen by the idle behaviour. Units: see
 * mao_lark_author.h.
 */
#include "mao_lark_author.h"

/* ack: "got it" - a quick firm nod, lids up for a beat. */
static const lark_key_t ak_fy[] = { K(0, 0, LINEAR), K(90, 9.0f, OUT), K(230, -4.0f, IN_OUT), K(420, 0, BACK) };
static const lark_key_t ak_lid[] = { K(0, 0, LINEAR), K(90, -0.18f, OUT), K(420, -0.18f, LINEAR), K(650, 0, IN_OUT) };
static const lark_key_t ak_sq[] = { K(0, 0, LINEAR), K(90, 0.14f, OUT), K(230, -0.06f, IN_OUT), K(420, 0, IN_OUT) };
static const lark_track_t ack[] = { TRACK(CH_FACE_Y, ak_fy), TRACK(CH_NARROW, ak_lid), TRACK(CH_SQUASH, ak_sq) };

/* busy: working on it - focused, eyes flicking between two points as if
 * following progress; looped until the work ends. */
static const lark_key_t bz_gx[] = { K(0, -5.0f, LINEAR), K(420, -5.0f, LINEAR), K(520, 5.0f, OUT), K(940, 5.0f, LINEAR),
                                    K(1040, -5.0f, OUT) };
static const lark_key_t bz_gy[] = { K(0, 2.0f, LINEAR) };
static const lark_key_t bz_lid[] = { K(0, 0.18f, LINEAR) };
static const lark_key_t bz_pu[] = { K(0, -0.2f, LINEAR) };
static const lark_track_t busy[] = { TRACK(CH_GAZE_X, bz_gx), TRACK(CH_GAZE_Y, bz_gy), TRACK(CH_NARROW, bz_lid),
                                     TRACK(CH_PUPIL, bz_pu) };

/* done: it worked - a small satisfied smile and a nod. */
static const lark_key_t dn_sm[] = { K(0, 0, LINEAR), K(160, 0.45f, OUT), K(900, 0.45f, LINEAR), K(1300, 0, IN_OUT) };
static const lark_key_t dn_fy[] = { K(0, 0, LINEAR), K(120, -8.0f, OUT), K(300, 3.0f, IN_OUT), K(500, 0, BACK) };
static const lark_key_t dn_sh[] = { K(0, 0, LINEAR), K(160, 0.4f, OUT), K(1300, 0, IN_OUT) };
static const lark_track_t done[] = { TRACK(CH_SMILE, dn_sm), TRACK(CH_FACE_Y, dn_fy), TRACK(CH_SHINE, dn_sh) };

/* fail: it didn't work - a wince (eyes squeezed, red glint), a short head
 * shake, then an apologetic droop. */
static const lark_key_t fl_cl[] = { K(0, 0, LINEAR), K(80, 0.55f, OUT), K(380, 0.55f, LINEAR), K(560, 0, IN_OUT) };
static const lark_key_t fl_red[] = { K(0, 0, LINEAR), K(80, 0.9f, OUT), K(900, 0.6f, IN_OUT), K(1400, 0, IN_OUT) };
static const lark_key_t fl_fx[] = { K(0, 0, LINEAR), K(420, 0, LINEAR), K(520, -12.0f, OUT), K(640, 10.0f, IN_OUT),
                                    K(760, -6.0f, IN_OUT), K(900, 0, BACK) };
static const lark_key_t fl_la[] = { K(0, 0, LINEAR), K(500, -0.45f, IN_OUT), K(1200, -0.45f, LINEAR), K(1600, 0, IN_OUT) };
static const lark_key_t fl_fy[] = { K(0, 0, LINEAR), K(80, -6.0f, OUT), K(500, 6.0f, IN_OUT), K(1200, 6.0f, LINEAR),
                                    K(1600, 0, IN_OUT) };
static const lark_track_t fail[] = { TRACK(CH_CLOSE, fl_cl), TRACK(CH_TINT_RED, fl_red), TRACK(CH_FACE_X, fl_fx),
                                     TRACK(CH_LID_ANGLE, fl_la), TRACK(CH_FACE_Y, fl_fy) };

/* back: stepping back - a glance over the shoulder, then settle. */
static const lark_key_t bk_gx[] = { K(0, 0, LINEAR), K(160, -9.0f, OUT), K(420, -9.0f, LINEAR), K(700, 0, IN_OUT) };
static const lark_key_t bk_fx[] = { K(0, 0, LINEAR), K(200, -6.0f, OUT), K(700, 0, IN_OUT) };
static const lark_track_t back[] = { TRACK(CH_GAZE_X, bk_gx), TRACK(CH_FACE_X, bk_fx) };

/* device_on: a device showed up - eyes widen, look to the rim where it
 * "is", a small hop, then back to you. */
static const lark_key_t do_gx[] = { K(0, 0, LINEAR), K(140, 11.0f, OUT), K(900, 11.0f, LINEAR), K(1150, 0, IN_OUT) };
static const lark_key_t do_gy[] = { K(0, 0, LINEAR), K(140, -5.0f, OUT), K(900, -5.0f, LINEAR), K(1150, 0, IN_OUT) };
static const lark_key_t do_open[] = { K(0, 0, LINEAR), K(90, 0.16f, OUT), K(700, 0.08f, IN_OUT), K(1200, 0, IN_OUT) };
static const lark_key_t do_fy[] = { K(0, 0, LINEAR), K(160, -10.0f, OUT), K(360, 0, IN), K(1200, 0, LINEAR) };
static const lark_key_t do_pu[] = { K(0, 0, LINEAR), K(140, 0.35f, OUT), K(1200, 0, IN_OUT) };
static const lark_track_t device_on[] = { TRACK(CH_GAZE_X, do_gx), TRACK(CH_GAZE_Y, do_gy), TRACK(CH_OPEN, do_open),
                                          TRACK(CH_FACE_Y, do_fy), TRACK(CH_PUPIL, do_pu) };

/* device_off: a device went away - a look after it, a small droop. */
static const lark_key_t df_gx[] = { K(0, 0, LINEAR), K(200, 10.0f, OUT), K(1100, 12.0f, IN_OUT), K(1500, 0, IN_OUT) };
static const lark_key_t df_gy[] = { K(0, 0, LINEAR), K(200, 3.0f, OUT), K(1500, 0, IN_OUT) };
static const lark_key_t df_la[] = { K(0, 0, LINEAR), K(300, -0.35f, IN_OUT), K(1100, -0.35f, LINEAR), K(1500, 0, IN_OUT) };
static const lark_key_t df_fy[] = { K(0, 0, LINEAR), K(400, 6.0f, IN_OUT), K(1500, 0, IN_OUT) };
static const lark_track_t device_off[] = { TRACK(CH_GAZE_X, df_gx), TRACK(CH_GAZE_Y, df_gy), TRACK(CH_LID_ANGLE, df_la),
                                           TRACK(CH_FACE_Y, df_fy) };

#define CTRL (LARK_ONESHOT | LARK_NO_PICK | LARK_NO_MIRROR)

const lark_state_t kLarkCtrl[] = {
    /*     name          length  in  curve   flags                 base bored agit aff  next   tracks */
    STATE("ack",           650,  40, OUT,    LARK_ONESHOT | LARK_NO_PICK, 0, 0, 0, 0, NULL, ack),
    STATE("busy",         1040, 150, OUT,    LARK_NO_PICK,          0,   0,   0,  0,  NULL,  busy),
    STATE("done",         1300,  60, OUT,    LARK_ONESHOT | LARK_NO_PICK, 0, 0, 0, 0, NULL, done),
    STATE("fail",         1600,  40, OUT,    CTRL,                  0,   0,   0,  0,  NULL,  fail),
    STATE("back",          700,  60, OUT,    CTRL,                  0,   0,   0,  0,  NULL,  back),
    STATE("device_on",    1200,  60, OUT,    LARK_ONESHOT | LARK_NO_PICK, 0, 0, 0, 0, NULL, device_on),
    STATE("device_off",   1500, 100, IN_OUT, LARK_ONESHOT | LARK_NO_PICK, 0, 0, 0, 0, NULL, device_off),
};
const int kLarkCtrlCount = (int)(sizeof(kLarkCtrl) / sizeof(kLarkCtrl[0]));
