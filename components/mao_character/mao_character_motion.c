/*
 * Character motion model: channels are springs (profiles in
 * mao_character_tune.h); this file composes them into a pose. Behaviour lives
 * in mao_character.c; geometry numbers live in mao_character_tune.h.
 */
#include <math.h>
#include <string.h>
#include "mao_character_priv.h"

#define TWO_PI 6.28318531f

static const mao_spring_profile_t kProfile[CH_COUNT] = {
    [CH_FACE_X] = MAO_P_FACE,     [CH_FACE_Y] = MAO_P_FACE,
    [CH_GAZE_X] = MAO_P_GAZE,     [CH_GAZE_Y] = MAO_P_GAZE,
    [CH_OPEN] = MAO_P_OPEN,       [CH_SQUASH] = MAO_P_SQUASH,
    [CH_TILT] = MAO_P_TILT,       [CH_NARROW] = MAO_P_NARROW,
    [CH_ORBIT_R] = MAO_P_ORBIT_R, [CH_ORBIT_A] = MAO_P_ORBIT_A,
    [CH_AWAY] = MAO_P_AWAY,       [CH_PRESS] = MAO_P_PRESS,
    [CH_SPREAD] = MAO_P_PRESS,    [CH_WOBBLE] = MAO_SPRING_SOFT,
    [CH_SLEEP] = MAO_SLEEP_PROFILE,
    [CH_TINT_MOVE] = MAO_P_TINT,  [CH_TINT_WARM] = MAO_P_TINT,
};

void mao_motion_init(mao_motion_t *m, const mao_look_t *look)
{
    memset(m, 0, sizeof(*m));
    m->look = *look;
    for (int i = 0; i < CH_COUNT; i++) {
        mao_spring_init(&m->ch[i], 0.0f, kProfile[i]);
    }
    /* Eyes start closed; the owner opens them (appear / wake). */
}

void mao_motion_profile(mao_motion_t *m, mao_channel_t c, mao_spring_profile_t p)
{
    m->ch[c].p = p;
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

void mao_motion_step(mao_motion_t *m, float dt, uint32_t now_ms)
{
    for (int i = 0; i < CH_COUNT; i++) {
        mao_spring_step(&m->ch[i], dt);
    }
    /* Two independent wobble phases: the eyes drift apart, not in sync. */
    m->wobble_phase_l = fmodf(m->wobble_phase_l + dt * TWO_PI * 1.35f, TWO_PI);
    m->wobble_phase_r = fmodf(m->wobble_phase_r + dt * TWO_PI * 1.95f, TWO_PI);
    m->breath_phase = fmodf(m->breath_phase + dt * TWO_PI / MAO_SLEEP_BREATH_S, TWO_PI);

    if (m->blinking && (int32_t)(now_ms - m->blink_start_ms) >= (int32_t)m->blink_len_ms) {
        if (m->blinks_left > 1) {
            m->blinks_left--;
            m->blink_start_ms = now_ms + 70;   /* gap inside a double blink */
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
    if (t < 0.35f) {
        return 1.0f - t / 0.35f;           /* close fast */
    }
    return t < 1.0f ? (t - 0.35f) / 0.65f : 1.0f;   /* open slower */
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int s = 0; s <= 16; s += 8) {
        const float ca = (float)((a >> s) & 0xFF);
        const float cb = (float)((b >> s) & 0xFF);
        out |= (uint32_t)lrintf(ca + (cb - ca) * t) << s;
    }
    return out;
}

void mao_motion_pose(const mao_motion_t *m, mao_mouth_t mouth, uint32_t now_ms, mao_pose_t *out)
{
    float v[CH_COUNT];
    for (int i = 0; i < CH_COUNT; i++) {
        v[i] = m->ch[i].x;
    }
    const mao_look_t *L = &m->look;
    const float sleep = clampf(v[CH_SLEEP], 0.0f, 1.0f);

    /* Openness: base x blink x sleep x narrowing. */
    float open = v[CH_OPEN] * blink_envelope(m, now_ms) * (1.0f - MAO_SLEEP_OPEN_LOSS * sleep);
    open = open < 0.0f ? 0.0f : open;
    const float squash = clampf(v[CH_SQUASH], -0.4f, 1.0f);
    const float narrow = clampf(v[CH_NARROW], 0.0f, 1.0f);

    const float widen = open > 1.0f ? 1.0f + (open - 1.0f) * MAO_WIDE_GROW : 1.0f;
    const float w = L->eye_w * widen * (1.0f + MAO_SQUASH_WIDEN * squash);
    float h = L->eye_h * open * (1.0f - MAO_SQUASH_FLATTEN * squash) * (1.0f - MAO_NARROW_MAX * narrow);

    /* Face centre: rest + offset + orbit around the screen centre + travel. */
    const float r = v[CH_ORBIT_R];
    const float a = v[CH_ORBIT_A];
    float fx = v[CH_FACE_X] + r * sinf(a);
    float fy = L->rest_y + v[CH_FACE_Y] - r * cosf(a) + v[CH_AWAY] + v[CH_PRESS]
             + MAO_SLEEP_DROP * sleep + MAO_SLEEP_BREATH * sleep * sinf(m->breath_phase);

    /* Lost coordination: each eye drifts on its own phase. */
    const float wob = clampf(v[CH_WOBBLE], 0.0f, MAO_REV_WOBBLE_MAX);
    const float wlx = wob * cosf(m->wobble_phase_l), wly = wob * 0.6f * sinf(m->wobble_phase_l);
    const float wrx = wob * cosf(m->wobble_phase_r + 1.3f), wry = wob * 0.6f * sinf(m->wobble_phase_r);

    const float gx = v[CH_GAZE_X];
    const float gy = v[CH_GAZE_Y] + MAO_SLEEP_GAZE_DOWN * sleep;
    const float half = L->eye_gap * 0.5f + v[CH_SPREAD];
    const float tilt = v[CH_TILT];

    out->lx = fx + gx - half + wlx;
    out->ly = fy + gy - tilt + wly;
    out->rx = fx + gx + half + wrx;
    out->ry = fy + gy + tilt + wry;
    out->lw = out->rw = w;
    /* When coordination is lost the eyes also disagree slightly in openness. */
    out->lh = h * (1.0f - 0.04f * wob);
    out->rh = h * (1.0f - 0.04f * wob * sinf(m->wobble_phase_r));
    if (out->lh < MAO_EYE_MIN_H) out->lh = MAO_EYE_MIN_H;
    if (out->rh < MAO_EYE_MIN_H) out->rh = MAO_EYE_MIN_H;

    out->mouth = mouth;
    out->mx = fx + gx * 0.5f;
    out->my = fy + gy * 0.5f + L->eye_h * 0.5f + 12.0f;

    uint32_t c = mix(MAO_EYE_COLOR, MAO_TINT_MOVE_COLOR, MAO_TINT_MOVE_MAX * v[CH_TINT_MOVE]);
    out->color = mix(c, MAO_TINT_WARM_COLOR, MAO_TINT_WARM_MAX * v[CH_TINT_WARM]);
}
