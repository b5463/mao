/*
 * Character motion model: every pose channel is a damped spring chasing a
 * target. Reactions only set targets or kick velocities; the springs supply
 * the physical feel (ease, slight overshoot, settle). No LVGL here.
 */
#include <math.h>
#include <string.h>
#include "mao_character_priv.h"

#define TWO_PI 6.28318531f

/* Geometry of the face (px). */
#define EYE_W          18.0f
#define EYE_H          26.0f
#define EYE_HALF_GAP   26.0f
#define MOUTH_DY       26.0f

typedef struct {
    float k;   /* stiffness (1/s^2) */
    float c;   /* damping = 2 * zeta * sqrt(k) */
} spring_param_t;

/* zeta < 1 overshoots slightly: that is the "physical" character. */
static spring_param_t s_param[CH_COUNT];

static void param(mao_channel_t ch, float k, float zeta)
{
    s_param[ch].k = k;
    s_param[ch].c = 2.0f * zeta * sqrtf(k);
}

void mao_motion_init(mao_motion_t *m)
{
    memset(m, 0, sizeof(*m));
    param(CH_FACE_X,   160.0f, 0.55f);
    param(CH_FACE_Y,   160.0f, 0.55f);
    param(CH_AWAY_Y,    95.0f, 0.85f);
    param(CH_GAZE_X,   260.0f, 0.70f);
    param(CH_GAZE_Y,   260.0f, 0.70f);
    param(CH_OPEN,     420.0f, 0.45f);
    param(CH_SQUASH,   520.0f, 0.30f);
    param(CH_TILT,     200.0f, 0.60f);
    param(CH_HAPPY,    220.0f, 0.70f);
    param(CH_ORBIT_R,   70.0f, 0.70f);
    param(CH_ORBIT_A,  160.0f, 0.90f);
    param(CH_WOBBLE,    60.0f, 1.00f);
    /* Start with eyes closed; the owner opens them (wake / appear). */
    m->ch[CH_OPEN].x = 0.0f;
    m->target[CH_OPEN] = 0.0f;
}

void mao_motion_set(mao_motion_t *m, mao_channel_t ch, float target)
{
    m->target[ch] = target;
}

void mao_motion_kick(mao_motion_t *m, mao_channel_t ch, float velocity)
{
    m->ch[ch].v += velocity;
}

void mao_motion_blink(mao_motion_t *m, uint32_t now_ms, uint16_t len_ms, uint8_t count)
{
    if (m->blinking) {
        return;
    }
    m->blinking = true;
    m->blink_start_ms = now_ms;
    m->blink_len_ms = len_ms;
    m->blinks_left = count;
}

void mao_motion_step(mao_motion_t *m, float dt_s, uint32_t now_ms)
{
    /* Semi-implicit Euler in <= 12 ms sub-steps (stable for the stiffest
     * channel at any tick rate). */
    int steps = (int)ceilf(dt_s / 0.012f);
    if (steps < 1) {
        steps = 1;
    }
    const float h = dt_s / (float)steps;
    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < CH_COUNT; i++) {
            mao_spring_t *sp = &m->ch[i];
            const float a = s_param[i].k * (m->target[i] - sp->x) - s_param[i].c * sp->v;
            sp->v += a * h;
            sp->x += sp->v * h;
        }
    }

    m->wobble_phase += dt_s * TWO_PI * 1.6f;
    if (m->wobble_phase > TWO_PI) {
        m->wobble_phase -= TWO_PI;
    }
    if (m->breathing) {
        m->breath_phase += dt_s * TWO_PI / 4.5f;
        if (m->breath_phase > TWO_PI) {
            m->breath_phase -= TWO_PI;
        }
    }

    if (m->blinking && (int32_t)(now_ms - m->blink_start_ms) >= (int32_t)m->blink_len_ms) {
        if (m->blinks_left > 1) {
            m->blinks_left--;
            m->blink_start_ms = now_ms + 70;   /* short gap between double blinks */
        } else {
            m->blinking = false;
        }
    }
}

static float blink_envelope(const mao_motion_t *m, uint32_t now_ms)
{
    if (!m->blinking || (int32_t)(now_ms - m->blink_start_ms) < 0) {
        return 1.0f;
    }
    const float t = (float)(now_ms - m->blink_start_ms) / (float)m->blink_len_ms;
    /* Close quickly (35 %), reopen more slowly (65 %). */
    if (t < 0.35f) {
        return 1.0f - t / 0.35f;
    }
    if (t < 1.0f) {
        return (t - 0.35f) / 0.65f;
    }
    return 1.0f;
}

void mao_motion_pose(const mao_motion_t *m, mao_mouth_t mouth, mao_pose_t *out)
{
    float v[CH_COUNT];
    for (int i = 0; i < CH_COUNT; i++) {
        v[i] = m->ch[i].x;
    }

    const uint32_t now_ms = lv_tick_get();
    float open = v[CH_OPEN] * blink_envelope(m, now_ms);
    if (open < 0.0f) {
        open = 0.0f;
    }
    float squash = v[CH_SQUASH];
    if (squash < -0.4f) {
        squash = -0.4f;
    }
    float happy = v[CH_HAPPY];
    if (happy < 0.0f) {
        happy = 0.0f;
    }

    /* Wide eyes grow a little in width too; squash trades height for width. */
    const float widen = open > 1.0f ? 1.0f + (open - 1.0f) * 0.5f : 1.0f;
    float w = EYE_W * widen * (1.0f + 0.30f * squash);
    float hgt = EYE_H * open * (1.0f - 0.60f * squash);
    hgt = hgt + (6.0f - hgt) * (happy > 1.0f ? 1.0f : happy);   /* squint towards 6 px */
    if (hgt < 2.0f) {
        hgt = 2.0f;                                              /* closed = thin line */
    }

    /* Face centre: base offset + orbit around the screen centre + away. */
    const float r = v[CH_ORBIT_R];
    const float a = v[CH_ORBIT_A];
    float fx = v[CH_FACE_X] + r * sinf(a);
    float fy = v[CH_FACE_Y] - r * cosf(a) + v[CH_AWAY_Y] - 3.0f * happy;
    if (m->breathing) {
        fy += 1.5f * sinf(m->breath_phase);
    }

    const float wob = v[CH_WOBBLE];
    const float wx = wob * cosf(m->wobble_phase);
    const float wy = wob * sinf(m->wobble_phase);

    const float gx = v[CH_GAZE_X];
    const float gy = v[CH_GAZE_Y];
    const float tilt = v[CH_TILT];

    out->eye_w = w;
    out->eye_h = hgt;
    out->left_x = fx + gx - EYE_HALF_GAP - wx;
    out->left_y = fy + gy - tilt - wy;
    out->right_x = fx + gx + EYE_HALF_GAP + wx;
    out->right_y = fy + gy + tilt + wy;
    out->mouth = mouth;
    out->mouth_x = fx + gx * 0.5f;
    out->mouth_y = fy + gy * 0.5f + MOUTH_DY;
}
