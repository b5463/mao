/* Private to mao_character: motion model and drawing. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#include "mao_spring.h"
#include "mao_character_tune.h"

/* Pose channels, each a spring (see mao_spring.h / mao_character_tune.h). */
typedef enum {
    CH_FACE_X = 0,   /* face offset from its rest position */
    CH_FACE_Y,
    CH_GAZE_X,       /* eyes within the face: leads the face */
    CH_GAZE_Y,
    CH_OPEN,         /* 0 closed, 1 normal, > 1 wide */
    CH_SQUASH,       /* 0 none .. 1 compressed (press, fold) */
    CH_TILT,         /* + = right eye lower */
    CH_NARROW,       /* 0..1 soft narrowing (rest deadpan, warm acknowledgement) */
    CH_SQUINT,       /* + narrows the left eye, - the right (skeptical look) */
    CH_ORBIT_R,      /* orbit radius around the screen centre */
    CH_ORBIT_A,      /* orbit angle, rad, 0 = top, + = clockwise */
    CH_AWAY,         /* vertical travel out of the circle (view transitions) */
    CH_PRESS,        /* press depth in px */
    CH_SPREAD,       /* extra px per eye outward */
    CH_WOBBLE,       /* loss of eye coordination, px */
    CH_SLEEP,        /* 0 awake .. 1 sleepy (very slow) */
    CH_TINT_MOVE,    /* 0..1 cobalt tint */
    CH_TINT_WARM,    /* 0..1 yellow tint */
    CH_TINT_RED,     /* 0..1 red tint (mad) */
    CH_CLOSE,        /* 0 open .. 1 closed to a crescent (pupil looks) */
    CH_PUPIL,        /* pupil dilation, 0 rest, + wide, - pinpoint */
    CH_SHINE,        /* catchlight gain, 0 rest (look's size), + sparkle */
    CH_WINK,         /* + closes the left eye, - the right */
    CH_SMILE,        /* 0..1 lower lids rise: smiling eyes (pupil looks) */
    CH_SLIT,         /* 0..1 pupil core narrows to a cat's slit */
    CH_EYE_W,        /* eye shape: width scale offset (+0.2 = 20 % wider) */
    CH_EYE_H,        /* eye shape: height scale offset */
    CH_STAR,         /* 0..1 a gold four-point star in each iris (greed) */
    CH_DARK,         /* 0..1 irises go dark and blank (shock, horror) */
    CH_CROSS,        /* px the pupils converge (cross-eyed), - diverge */
    CH_HEAD_YAW,     /* rad, added head turn (+ = towards screen right) */
    CH_LID_ANGLE,    /* upper lid slope: + inner corners down (cross), - outer down (sad) */
    CH_HEAD_PITCH,   /* rad, added head nod (+ = looking down) */
    CH_EXIT_X,       /* horizontal travel out of the circle (transfer); bypasses the head clamp */
    CH_COUNT,
} mao_channel_t;

typedef enum {
    MAO_MOUTH_NONE = 0,
    MAO_MOUTH_O,     /* tiny ring: surprise, only for an instant */
} mao_mouth_t;

typedef struct {
    mao_spring_t ch[CH_COUNT];
    mao_look_t look;

    uint32_t blink_start_ms;
    uint16_t blink_len_ms;
    uint8_t blinks_left;
    bool blinking;

    float wobble_phase_l, wobble_phase_r;
    float breath_phase;
    float layer[CH_COUNT];   /* additive expression layer (mao_lark.c) */
    mao_spring_t head_yaw, head_pitch;   /* the head follows the gaze, slower */
} mao_motion_t;

typedef struct {
    float lx, ly, rx, ry;    /* eye centres, px from screen centre */
    float lw, lh, rw, rh;    /* eye sizes (may differ when coordination is lost) */
    float mx, my;
    mao_mouth_t mouth;
    uint32_t color;          /* eye (eyeball) colour, 0xRRGGBB */
    /* Pupil looks only (has_pupils). */
    bool has_pupils;
    float plx, ply, prx, pry;    /* pupil centres */
    float pw, plh, prh;          /* pupil sizes */
    float lid_l, lid_r;          /* lid lower edge, px from screen centre; <= eye top = open */
    float cover_l, cover_r;      /* round cover centre y; covers are eye-sized */
    float low_l, low_r;          /* lower lid centre y (eye-sized, rising from below) */
    bool low_on;
    float cw, ch_l, ch_r;        /* pupil core size (inside the iris); 0 = none */
    float star;                  /* star pupil size, px (0 = none) */
    uint32_t core_color;
    bool cover_on;
    float sx[2], sy[2], ss;      /* catchlight centres and diameter (0 = none) */
    uint32_t pupil_color;
    float fore[2];               /* per-eye horizontal foreshortening (1 = facing) */
    float lid_tilt[2];           /* px the lid's inner end sits below its outer end */
    uint8_t front;               /* eye drawn on top (the nearer one) */
} mao_pose_t;

void mao_motion_init(mao_motion_t *m, const mao_look_t *look);
static inline void mao_motion_set(mao_motion_t *m, mao_channel_t c, float target) { m->ch[c].target = target; }
static inline void mao_motion_kick(mao_motion_t *m, mao_channel_t c, float v) { m->ch[c].v += v; }
static inline float mao_motion_get(const mao_motion_t *m, mao_channel_t c) { return m->ch[c].x; }
void mao_motion_profile(mao_motion_t *m, mao_channel_t c, mao_spring_profile_t p);
void mao_motion_blink(mao_motion_t *m, uint32_t now_ms, uint16_t len_ms, uint8_t count);
void mao_motion_step(mao_motion_t *m, float dt_s, uint32_t now_ms);
void mao_motion_pose(const mao_motion_t *m, mao_mouth_t mouth, uint32_t now_ms, mao_pose_t *out);

/* ---------------------------------------------------------------------- */
/* Idle behaviour (mao_character_idle.c)                                  */
/* ---------------------------------------------------------------------- */

typedef struct {
    float base_x, base_y;      /* resting offset, drifts via repositions */
    uint32_t next_ms;          /* next idle event */
    uint32_t glance_until;     /* 0 = no temporary look active */
} mao_idle_t;

/* Plan the next idle event. after_interaction: longer first pause. */
void mao_idle_schedule(mao_idle_t *s, uint32_t now_ms, bool sleepy, bool after_interaction);
/* Run the due event and/or end a temporary glance. Only call at idle priority. */
void mao_idle_update(mao_idle_t *s, mao_motion_t *m, uint32_t now_ms, bool sleepy);
/* Yield immediately to anything more important. */
void mao_idle_cancel(mao_idle_t *s, mao_motion_t *m);

float mao_frand(float lo, float hi);

/* ---------------------------------------------------------------------- */

typedef struct {
    int16_t x, y, w, h;
    bool hidden;
} mao_box_t;

typedef struct {
    lv_obj_t *eye[2];
    lv_obj_t *pupil[2];
    lv_obj_t *lid[2];
    lv_obj_t *cover[2];
    lv_obj_t *lower[2];
    lv_point_precise_t lid_pts[2][4];
    lv_point_precise_t low_pts[2][14];
    lv_obj_t *core[2];
    lv_obj_t *shine[4];          /* big + small catchlight per eye */
    lv_obj_t *mouth;
    lv_obj_t *star[2];           /* custom-drawn star pupils */
    lv_obj_t *mark[3];           /* impact ticks at the rim (transfer failure) */
    mao_box_t last_star[2];
    mao_box_t last_eye[2];
    mao_box_t last_pupil[2];
    mao_box_t last_lid[2];
    mao_box_t last_cover[2];
    mao_box_t last_lower[2];
    mao_box_t last_core[2];
    uint32_t last_core_color;
    mao_box_t last_shine[4];
    mao_box_t last_mouth;
    mao_mouth_t last_mouth_kind;
    uint32_t last_color;
    uint32_t last_pupil_color;
    uint8_t front;
} mao_char_draw_t;

esp_err_t mao_char_draw_create(mao_char_draw_t *d, lv_obj_t *parent);
void mao_char_draw_apply(mao_char_draw_t *d, const mao_pose_t *pose);
void mao_char_draw_hide(mao_char_draw_t *d);
void mao_char_draw_star(mao_char_draw_t *d, const mao_pose_t *pose);
/* Impact marks: 3 short off-white radial ticks at the rim in direction
 * (dx, dy). show = false hides them (they live ~150 ms after a hard hit). */
void mao_char_draw_marks(mao_char_draw_t *d, int dx, int dy, bool show);

/* ---------------------------------------------------------------------- */
/* Transfer (mao_character_transfer.c): the physical connection sequence.  */
/* MAO exits through the screen edge into a connected device, or tries to  */
/* and finds the glass very much still there. Owns the pose while active.  */
/* ---------------------------------------------------------------------- */

typedef enum {
    MAO_TR_NONE = 0,
    MAO_TR_SEARCH,   /* looking at the chosen edge, listening (connecting) */
    MAO_TR_EXIT,     /* purposeful directional exit */
    MAO_TR_GONE,     /* off screen (away on the other device) */
    MAO_TR_ENTER,    /* coming back in from the edge */
    MAO_TR_BASH,     /* three escalating attempts against the glass */
} mao_transfer_phase_t;

typedef struct {
    uint8_t phase;        /* mao_transfer_phase_t */
    int8_t dx, dy;        /* logical direction, exactly one of them non-zero */
    uint32_t t0;
    uint8_t step;         /* next scripted action index */
    uint32_t marks_until; /* impact marks visible until (0 = hidden) */
    bool marks_on;
} mao_transfer_t;

/* Enter a phase (SEARCH / EXIT / ENTER / BASH; NONE aborts and restores). */
void mao_transfer_begin(mao_transfer_t *t, mao_motion_t *m, uint8_t phase, int dx, int dy, uint32_t now);
/* Run the script. Returns true while the transfer owns the pose. */
bool mao_transfer_tick(mao_transfer_t *t, mao_motion_t *m, mao_char_draw_t *d, uint32_t now);

