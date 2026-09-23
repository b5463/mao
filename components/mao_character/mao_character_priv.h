/* Private to mao_character: motion model and drawing. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

/* ---------------------------------------------------------------------- */
/* Motion: damped springs driving a small set of pose channels.           */
/* ---------------------------------------------------------------------- */

typedef struct {
    float x;   /* position */
    float v;   /* velocity (units / s) */
} mao_spring_t;

typedef enum {
    MAO_MOUTH_NONE = 0,
    MAO_MOUTH_O,       /* small ring: surprise */
    MAO_MOUTH_FLAT,    /* short bar: dizzy */
} mao_mouth_t;

/* Channels, each a spring towards a target. Units: pixels unless noted. */
typedef enum {
    CH_FACE_X = 0,   /* face offset from screen centre */
    CH_FACE_Y,
    CH_AWAY_Y,       /* extra offset used to leave / return (menu) */
    CH_GAZE_X,       /* eyes within the face */
    CH_GAZE_Y,
    CH_OPEN,         /* eye openness: 0 closed, 1 normal, >1 wide */
    CH_SQUASH,       /* 0 none .. 1 fully compressed (press) */
    CH_TILT,         /* + = right eye lower */
    CH_HAPPY,        /* 0..1 smiling squint */
    CH_ORBIT_R,      /* orbit radius around the screen centre */
    CH_ORBIT_A,      /* orbit angle (radians, 0 = top, + = clockwise) */
    CH_WOBBLE,       /* dizzy wobble amplitude */
    CH_COUNT,
} mao_channel_t;

typedef struct {
    mao_spring_t ch[CH_COUNT];
    float target[CH_COUNT];

    /* blink envelope */
    uint32_t blink_start_ms;
    uint16_t blink_len_ms;
    uint8_t blinks_left;
    bool blinking;

    float wobble_phase;   /* radians */
    float breath_phase;   /* radians */
    bool breathing;
} mao_motion_t;

/* Final pose handed to the renderer. */
typedef struct {
    float left_x, left_y, right_x, right_y;   /* eye centres (px from centre) */
    float eye_w, eye_h;
    float mouth_x, mouth_y;
    mao_mouth_t mouth;
} mao_pose_t;

void mao_motion_init(mao_motion_t *m);
void mao_motion_set(mao_motion_t *m, mao_channel_t ch, float target);
void mao_motion_kick(mao_motion_t *m, mao_channel_t ch, float velocity);
void mao_motion_blink(mao_motion_t *m, uint32_t now_ms, uint16_t len_ms, uint8_t count);
void mao_motion_step(mao_motion_t *m, float dt_s, uint32_t now_ms);
void mao_motion_pose(const mao_motion_t *m, mao_mouth_t mouth, mao_pose_t *out);

/* ---------------------------------------------------------------------- */
/* Drawing: two eyes + mouth as plain LVGL objects.                       */
/* ---------------------------------------------------------------------- */

typedef struct {
    int16_t x, y, w, h;
    bool hidden;
} mao_box_t;

typedef struct {
    lv_obj_t *eye[2];
    lv_obj_t *mouth;
    mao_box_t last_eye[2];
    mao_box_t last_mouth;
    mao_mouth_t last_mouth_kind;
} mao_char_draw_t;

/* Parts start hidden. */
esp_err_t mao_char_draw_create(mao_char_draw_t *d, lv_obj_t *parent);
void mao_char_draw_apply(mao_char_draw_t *d, const mao_pose_t *pose);
void mao_char_draw_hide(mao_char_draw_t *d);
