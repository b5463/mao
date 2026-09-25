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
    [CH_SQUINT] = MAO_P_NARROW,
    [CH_ORBIT_R] = MAO_P_ORBIT_R, [CH_ORBIT_A] = MAO_P_ORBIT_A,
    [CH_AWAY] = MAO_P_AWAY,       [CH_PRESS] = MAO_P_PRESS,
    [CH_SPREAD] = MAO_P_PRESS,    [CH_WOBBLE] = MAO_SPRING_SOFT,
    [CH_SLEEP] = MAO_SLEEP_PROFILE,
    [CH_TINT_MOVE] = MAO_P_TINT,  [CH_TINT_WARM] = MAO_P_TINT,
    [CH_TINT_RED] = MAO_P_TINT,   [CH_CLOSE] = MAO_P_CLOSE,
    [CH_PUPIL] = MAO_SPRING_SOFT, [CH_SHINE] = MAO_SPRING_SOFT,
    [CH_WINK] = MAO_P_CLOSE,      [CH_SMILE] = MAO_P_CLOSE,
    [CH_SLIT] = MAO_P_SHAPE,      [CH_EYE_W] = MAO_P_SHAPE,     [CH_EYE_H] = MAO_P_SHAPE,
    [CH_STAR] = MAO_P_SHAPE,      [CH_DARK] = MAO_P_TINT,       [CH_CROSS] = MAO_P_GAZE,
    [CH_HEAD_YAW] = MAO_P_HEAD,   [CH_HEAD_PITCH] = MAO_P_HEAD,  [CH_LID_ANGLE] = MAO_P_NARROW,
};

void mao_motion_init(mao_motion_t *m, const mao_look_t *look)
{
    memset(m, 0, sizeof(*m));
    m->look = *look;
    for (int i = 0; i < CH_COUNT; i++) {
        mao_spring_init(&m->ch[i], 0.0f, kProfile[i]);
    }
    /* Eyes start closed; the owner opens them (appear / wake). */
    mao_spring_init(&m->ch[CH_NARROW], MAO_REST_NARROW, kProfile[CH_NARROW]);
    mao_spring_init(&m->head_yaw, 0.0f, MAO_P_HEAD);
    mao_spring_init(&m->head_pitch, 0.0f, MAO_P_HEAD);
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
    /* The head follows wherever the eyes look - from any source (dial,
     * expressions, the mind) - on a slower spring. */
    m->head_yaw.target = MAO_HEAD_YAW_GAIN * (m->ch[CH_GAZE_X].x + m->layer[CH_GAZE_X]);
    m->head_pitch.target = MAO_HEAD_PITCH_GAIN * (m->ch[CH_GAZE_Y].x + m->layer[CH_GAZE_Y]);
    mao_spring_step(&m->head_yaw, dt);
    mao_spring_step(&m->head_pitch, dt);
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
    if (t < 0.30f) {
        const float u = t / 0.30f;         /* close fast, easing in */
        return 1.0f - u * u;
    }
    if (t < 0.48f) {
        return 0.0f;                       /* a beat shut */
    }
    if (t < 1.0f) {
        const float u = (t - 0.48f) / 0.52f;   /* open slower, soft landing */
        return 1.0f - (1.0f - u) * (1.0f - u);
    }
    return 1.0f;
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

/* Keep a pupil offset inside the ellipse (rx, ry). */
static void clamp_ellipse(float *x, float *y, float rx, float ry)
{
    if (rx <= 0.0f || ry <= 0.0f) {
        *x = *y = 0.0f;
        return;
    }
    const float d = (*x * *x) / (rx * rx) + (*y * *y) / (ry * ry);
    if (d > 1.0f) {
        const float k = 1.0f / sqrtf(d);
        *x *= k;
        *y *= k;
    }
}

void mao_motion_pose(const mao_motion_t *m, mao_mouth_t mouth, uint32_t now_ms, mao_pose_t *out)
{
    float v[CH_COUNT];
    for (int i = 0; i < CH_COUNT; i++) {
        v[i] = m->ch[i].x + m->layer[i];
    }
    const mao_look_t *L = &m->look;
    const bool orb = L->pupil_w > 0.0f;
    const float ms = L->motion_scale;
    const float sleep = clampf(v[CH_SLEEP], 0.0f, 1.0f);

    /* Openness: base x blink x sleep (x narrowing for solid eyes; pupil
     * looks narrow with their lids instead). */
    const float env = blink_envelope(m, now_ms);
    float open = v[CH_OPEN] * (orb ? 1.0f : env) * (1.0f - (orb ? 0.15f : MAO_SLEEP_OPEN_LOSS) * sleep);
    open = open < 0.0f ? 0.0f : open;
    const float squash = clampf(v[CH_SQUASH], -0.4f, 1.0f);
    const float narrow = clampf(v[CH_NARROW], 0.0f, 1.0f);
    const float sq = clampf(v[CH_SQUINT], -1.0f, 1.0f);

    const float widen = open > 1.0f ? 1.0f + (open - 1.0f) * MAO_WIDE_GROW * (orb ? 0.35f : 1.0f) : 1.0f;
    /* Eye shape: states may reshape the eyes (a cat's almond, a wide stare). */
    const float shape_w = orb ? clampf(1.0f + v[CH_EYE_W], 0.6f, 1.4f) : 1.0f;
    const float shape_h = orb ? clampf(1.0f + v[CH_EYE_H], 0.5f, 1.3f) : 1.0f;
    const float w = L->eye_w * shape_w * widen * (1.0f + MAO_SQUASH_WIDEN * squash);
    float h = L->eye_h * shape_h * open * (1.0f - MAO_SQUASH_FLATTEN * squash);
    const float blink = orb ? 1.0f - env : 0.0f;
    h *= 1.0f - MAO_BLINK_SQUASH * blink;
    if (!orb) {
        h *= 1.0f - MAO_NARROW_MAX * narrow;
    }

    /* Face centre: rest + offset + orbit around the screen centre + travel. */
    const float r = v[CH_ORBIT_R];
    const float a = v[CH_ORBIT_A];
    float fx = (v[CH_FACE_X] + r * sinf(a)) * ms;
    float fy = L->rest_y + (v[CH_FACE_Y] - r * cosf(a)) * ms + v[CH_AWAY] + v[CH_PRESS]
             + MAO_SLEEP_DROP * sleep + MAO_SLEEP_BREATH * sleep * sinf(m->breath_phase) + MAO_BLINK_DIP * blink;

    /* Lost coordination: each eye drifts on its own phase. */
    const float wob = clampf(v[CH_WOBBLE], 0.0f, MAO_REV_WOBBLE_MAX);
    const float wlx = wob * cosf(m->wobble_phase_l), wly = wob * 0.6f * sinf(m->wobble_phase_l);
    const float wrx = wob * cosf(m->wobble_phase_r + 1.3f), wry = wob * 0.6f * sinf(m->wobble_phase_r);

    const float gx = v[CH_GAZE_X];
    const float gy = v[CH_GAZE_Y] + MAO_SLEEP_GAZE_DOWN * sleep;
    const float half = L->eye_gap * 0.5f + v[CH_SPREAD];
    const float tilt = v[CH_TILT];
    const float follow = orb ? MAO_SCLERA_FOLLOW : 1.0f;
    const float ew = orb ? 0.35f : 1.0f;   /* eyeballs wobble less; their pupils roll instead */

    out->lx = fx + gx * follow - half + wlx * ew;
    out->ly = fy + gy * follow - tilt + wly * ew;
    out->rx = fx + gx * follow + half + wrx * ew;
    out->ry = fy + gy * follow + tilt + wry * ew;
    out->lw = out->rw = w;
    /* When coordination is lost the eyes also disagree slightly in openness. */
    out->lh = h * (1.0f - 0.04f * wob);
    out->rh = h * (1.0f - 0.04f * wob * sinf(m->wobble_phase_r));
    if (!orb) {
        out->lh *= 1.0f - MAO_SQUINT_MAX * (sq > 0.0f ? sq : 0.0f);
        out->rh *= 1.0f - MAO_SQUINT_MAX * (sq < 0.0f ? -sq : 0.0f);
    }
    if (out->lh < MAO_EYE_MIN_H) out->lh = MAO_EYE_MIN_H;
    if (out->rh < MAO_EYE_MIN_H) out->rh = MAO_EYE_MIN_H;

    /* Pseudo-3D head: place both eyes on a sphere turned by yaw / pitch. */
    out->fore[0] = out->fore[1] = 1.0f;
    out->front = 0;
    if (orb) {
        const float yaw = clampf(m->head_yaw.x + v[CH_HEAD_YAW], -1.3f, 1.3f);
        const float pitch = clampf(m->head_pitch.x + v[CH_HEAD_PITCH], -0.7f, 0.7f);
        const float a0 = asinf(clampf(half / MAO_HEAD_R, 0.0f, 0.95f));
        const float cp = cosf(pitch);
        const float dy = MAO_HEAD_R * sinf(pitch) * MAO_HEAD_PITCH_SHIFT;
        float th[2];
        for (int e = 0; e < 2; e++) {
            const float side = e ? 1.0f : -1.0f;
            th[e] = side * a0 + yaw;
            const float c = cosf(th[e]);
            const float shift = MAO_HEAD_TRAVEL_R * (sinf(th[e]) - side * sinf(a0));
            float *x = e ? &out->rx : &out->lx, *y = e ? &out->ry : &out->ly;
            float *ww = e ? &out->rw : &out->lw, *hh = e ? &out->rh : &out->lh;
            *x += shift;
            *y += dy * c;
            out->fore[e] = c < MAO_HEAD_MIN_FORE ? 0.0f : c;
            *ww *= out->fore[e];
            /* Keep the turned eye inside the circle, as STARBOY's do. */
            const float edge = fabsf(*x) + *ww * 0.5f;
            if (edge > MAO_HEAD_EDGE) {
                *x -= copysignf(edge - MAO_HEAD_EDGE, *x);
            }
            *hh *= 0.85f + 0.15f * cp;
        }
        out->front = fabsf(th[1]) < fabsf(th[0]) ? 1 : 0;
    }

    out->mouth = mouth;
    out->mx = fx + gx * 0.5f * follow;
    out->my = fy + gy * 0.5f * follow + L->eye_h * 0.5f + 12.0f;

    const float tm = v[CH_TINT_MOVE], tw = v[CH_TINT_WARM];
    out->has_pupils = orb;
    if (!orb) {
        const uint32_t c = mix(L->eye_color, MAO_TINT_MOVE_COLOR, MAO_TINT_MOVE_MAX * tm);
        out->color = mix(c, MAO_TINT_WARM_COLOR, MAO_TINT_WARM_MAX * tw);
        return;
    }

    /* Eyes keep their colour; colour events land on the pupils. */
    out->color = L->eye_color;
    uint32_t pc = mix(L->pupil_color, MAO_TINT_MOVE_COLOR, MAO_TINT_PUPIL_MAX * tm);
    pc = mix(pc, MAO_TINT_WARM_COLOR, MAO_TINT_PUPIL_MAX * tw);
    pc = mix(pc, MAO_TINT_RED_COLOR, MAO_TINT_PUPIL_MAX * clampf(v[CH_TINT_RED], 0.0f, 1.0f));
    const float dark = clampf(v[CH_DARK], 0.0f, 1.0f);
    out->pupil_color = mix(pc, L->core_color ? L->core_color : 0x101014, 0.85f * dark);

    /* Pupils: gaze, plus rolling around the eyeball while the face orbits,
     * plus independent drift when dizzy. They shrink a little when the eyes
     * go wide, and squash with the eyeball. */
    float constrict = open > 1.0f ? 1.0f - (open - 1.0f) * MAO_PUPIL_CONSTRICT : 1.0f;
    constrict *= clampf(1.0f + MAO_PUPIL_DILATE * v[CH_PUPIL], 0.35f, 1.6f);
    const float pw = L->pupil_w * constrict;
    const float rngx = (w - pw) * 0.5f - MAO_PUPIL_MARGIN;
    const float roll = clampf(r / MAO_ORBIT_RADIUS, 0.0f, 1.0f) * MAO_PUPIL_ORBIT;
    const float px0 = gx * MAO_PUPIL_GAIN + sinf(a) * roll * rngx;
    const float py0 = gy * MAO_PUPIL_GAIN - cosf(a) * roll * rngx;
    const float eh[2] = { out->lh, out->rh };
    const float wx[2] = { wlx, wrx }, wy[2] = { wly, wry };
    float ox[2], oy[2], ph[2];
    for (int e = 0; e < 2; e++) {
        const float f = out->fore[e];
        ph[e] = L->pupil_h * constrict * (eh[e] / L->eye_h < 1.0f ? eh[e] / L->eye_h : 1.0f);
        ox[e] = px0 + wx[e] * MAO_PUPIL_WOBBLE + (e ? -v[CH_CROSS] : v[CH_CROSS]);
        oy[e] = py0 + wy[e] * MAO_PUPIL_WOBBLE;
        clamp_ellipse(&ox[e], &oy[e], rngx, (eh[e] - ph[e]) * 0.5f - MAO_PUPIL_MARGIN);
        ox[e] *= f;   /* on a turned eye the iris is foreshortened too */
    }
    out->pw = pw;
    out->plx = out->lx + ox[0];
    out->ply = out->ly + oy[0];
    out->prx = out->rx + ox[1];
    out->pry = out->ry + oy[1];
    out->plh = ph[0];
    out->prh = ph[1];

    /* Lids: a flat lower edge coming down from above each eyeball. */
    const float base = MAO_LID_NARROW * narrow + MAO_LID_SLEEP * sleep;
    /* A small dead zone: an almost-open lid would only flatten the eyeball's
     * top, so it starts from nothing instead. */
    const float dz = MAO_LID_DEADZONE;
    const float dl = clampf((base + MAO_LID_SQUINT * (sq > 0.0f ? sq : 0.0f) - dz) / (1.0f - dz), 0.0f, MAO_LID_MAX);
    const float dr = clampf((base + MAO_LID_SQUINT * (sq < 0.0f ? -sq : 0.0f) - dz) / (1.0f - dz), 0.0f, MAO_LID_MAX);
    /* Smiling raises the upper lid: the cheeks push up, the brow lifts. */
    const float lift_lid = 1.0f - clampf(v[CH_SMILE], 0.0f, 1.0f);
    out->lid_l = out->ly - out->lh * 0.5f + dl * lift_lid * out->lh;
    out->lid_r = out->ry - out->rh * 0.5f + dr * lift_lid * out->rh;
    const float ang = clampf(v[CH_LID_ANGLE], -1.0f, 1.0f);
    out->lid_tilt[0] = ang * lift_lid * MAO_LID_ANGLE_PX * out->lw;
    out->lid_tilt[1] = ang * lift_lid * MAO_LID_ANGLE_PX * out->rw;

    /* Covers: a round lid descends over each eye; fully closed leaves a thin
     * lower crescent. Blinks and sleep use it too. */
    /* Blinks fade out as the eyes smile shut - "^ ^" eyes don't vanish. */
    const float close = v[CH_CLOSE] + (1.0f - env) * (1.0f - 0.9f * clampf(v[CH_SMILE], 0.0f, 1.0f)),
                wink = v[CH_WINK];
    const float cl = clampf(close + (wink > 0.0f ? wink : 0.0f), 0.0f, 1.0f);
    const float cr = clampf(close + (wink < 0.0f ? -wink : 0.0f), 0.0f, 1.0f);
    out->cover_on = cl > 0.02f || cr > 0.02f;
    out->cover_l = cl > 0.02f ? out->ly - (out->lh + 2.0f) * (1.0f - cl) - MAO_CRESCENT_PX * cl : -400.0f;
    out->cover_r = cr > 0.02f ? out->ry - (out->rh + 2.0f) * (1.0f - cr) - MAO_CRESCENT_PX * cr : -400.0f;

    /* Lower lids: an eye-sized background shape rising from below. */
    /* Closing and smiling don't stack: a half-closed eye with a rising lower
     * lid would leave spiky notches. The more it closes, the less it smiles. */
    const float smile = clampf(v[CH_SMILE], 0.0f, 1.0f) * MAO_SMILE_MAX * (1.0f - fmaxf(cl, cr));
    out->low_on = smile > 0.02f;
    out->low_l = out->ly + (out->lh + 2.0f) - smile * out->lh;
    out->low_r = out->ry + (out->rh + 2.0f) - smile * out->rh;

    /* Pupil core inside the iris. */
    out->core_color = L->core_color;
    const float slit = clampf(v[CH_SLIT], 0.0f, 1.0f);
    out->cw = pw * L->core * (1.0f - (1.0f - MAO_SLIT_MIN) * slit);
    out->ch_l = out->plh * L->core * (1.08f + 0.5f * slit);
    out->ch_r = out->prh * L->core * (1.08f + 0.5f * slit);

    out->star = pw * 0.44f * clampf(v[CH_STAR], 0.0f, 1.3f) * (1.0f - fmaxf(cl, cr));   /* closes with the eye */

    /* Catchlights: upper right of each pupil, riding with it. */
    /* Blank dark irises lose their highlights. */
    const float shine = L->shine * clampf(1.0f + v[CH_SHINE], 0.0f, 2.5f) * (1.0f - dark);
    out->ss = shine > 0.0f ? pw * MAO_SHINE_SIZE * shine : 0.0f;
    out->sx[0] = out->plx + pw * 0.20f * out->fore[0];
    out->sy[0] = out->ply - out->plh * 0.20f;
    out->sx[1] = out->prx + pw * 0.20f * out->fore[1];
    out->sy[1] = out->pry - out->prh * 0.20f;
}
