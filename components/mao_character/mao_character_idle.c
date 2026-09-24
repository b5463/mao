/*
 * Idle behaviour: MAO does almost nothing, but never looks dead.
 *
 * One small event at a time, separated by irregular pauses; about a third of
 * the pauses are long quiet stretches. Randomness is drawn only when the next
 * event is scheduled, never per frame. Weights and timings are in
 * mao_character_tune.h. Idle has the lowest priority and yields instantly.
 */
#include "esp_random.h"
#include "mao_character_priv.h"

float mao_frand(float lo, float hi)
{
    return lo + (hi - lo) * ((float)(esp_random() & 0xFFFF) / 65535.0f);
}

static float rsign(void)
{
    return (esp_random() & 1) ? 1.0f : -1.0f;
}

void mao_idle_schedule(mao_idle_t *s, uint32_t now_ms, bool sleepy, bool after_interaction)
{
    float gap;
    if (sleepy) {
        gap = mao_frand(MAO_SLEEP_GAP_MIN, MAO_SLEEP_GAP_MAX);
    } else if (after_interaction) {
        gap = mao_frand(MAO_IDLE_FIRST_MIN, MAO_IDLE_FIRST_MAX);
    } else if ((int)(esp_random() % 100) < MAO_IDLE_QUIET_CHANCE) {
        gap = mao_frand(MAO_IDLE_QUIET_MIN, MAO_IDLE_QUIET_MAX);
    } else {
        gap = mao_frand(MAO_IDLE_GAP_MIN, MAO_IDLE_GAP_MAX);
    }
    s->next_ms = now_ms + (uint32_t)(gap * 1000.0f);
}

static void look(mao_idle_t *s, mao_motion_t *m, float gx, float gy, float face_dx, float hold_s, uint32_t now)
{
    mao_motion_set(m, CH_GAZE_X, gx);
    mao_motion_set(m, CH_GAZE_Y, gy);
    mao_motion_set(m, CH_FACE_X, s->base_x + face_dx);
    s->glance_until = now + (uint32_t)(hold_s * 1000.0f);
}

static void run_awake_event(mao_idle_t *s, mao_motion_t *m, uint32_t now)
{
    enum { TOTAL = MAO_W_BLINK + MAO_W_GLANCE_SMALL + MAO_W_MICRO + MAO_W_GLANCE_LONG +
                   MAO_W_REPOSITION + MAO_W_DOUBLE_BLINK + MAO_W_EDGE + MAO_W_HOP + MAO_W_INSPECT };
    const float A = MAO_IDLE_AMP;
    int r = (int)(esp_random() % TOTAL);

    if ((r -= MAO_W_BLINK) < 0) {
        mao_motion_blink(m, now, (uint16_t)(MAO_BLINK_S * 1000.0f), 1);
    } else if ((r -= MAO_W_GLANCE_SMALL) < 0) {
        /* Barely there: a couple of pixels, back soon. */
        look(s, m, rsign() * mao_frand(2.0f, 4.0f) * A, mao_frand(-2.0f, 2.0f) * A, 0.0f, mao_frand(0.5f, 1.1f), now);
    } else if ((r -= MAO_W_MICRO) < 0) {
        /* The whole face settles by a pixel: almost subliminal. */
        s->base_x += rsign();
        s->base_x = s->base_x > 6.0f ? 6.0f : (s->base_x < -6.0f ? -6.0f : s->base_x);
        mao_motion_set(m, CH_FACE_X, s->base_x);
    } else if ((r -= MAO_W_GLANCE_LONG) < 0) {
        look(s, m, rsign() * mao_frand(6.0f, 9.0f) * A, mao_frand(-3.0f, 3.0f) * A, rsign() * 3.0f,
             mao_frand(1.2f, 2.2f), now);
    } else if ((r -= MAO_W_REPOSITION) < 0) {
        s->base_x = mao_frand(-5.0f, 5.0f) * A;
        s->base_y = mao_frand(-3.0f, 3.0f) * A;
        mao_motion_set(m, CH_FACE_X, s->base_x);
        mao_motion_set(m, CH_FACE_Y, s->base_y);
    } else if ((r -= MAO_W_DOUBLE_BLINK) < 0) {
        mao_motion_blink(m, now, (uint16_t)(MAO_BLINK_S * 1000.0f), 2);
    } else if ((r -= MAO_W_EDGE) < 0) {
        /* Inspect the rim of the circle. */
        const float side = rsign();
        look(s, m, 14.0f * side, mao_frand(-4.0f, 2.0f), 11.0f * side, 0.9f, now);
    } else if ((r -= MAO_W_HOP) < 0) {
        mao_motion_kick(m, CH_FACE_Y, -36.0f);   /* tiny hop */
    } else {
        /* Maomao's inspection: lean towards the rim, one eye narrowed,
         * as if examining something suspicious. */
        const float side = rsign();
        look(s, m, 12.0f * side, -2.0f, 14.0f * side, mao_frand(1.1f, 1.8f), now);
        mao_motion_set(m, CH_SQUINT, side > 0 ? -0.5f : 0.5f);   /* narrow the eye nearer the rim */
        mao_motion_set(m, CH_TILT, side * 2.5f);
    }
}

static void run_sleepy_event(mao_idle_t *s, mao_motion_t *m, uint32_t now)
{
    const int r = (int)(esp_random() % 100);
    if (r < 65) {
        mao_motion_blink(m, now, (uint16_t)(MAO_SLEEP_BLINK_S * 1000.0f), 1);
    } else if (r < 85) {
        mao_motion_kick(m, CH_FACE_Y, 16.0f);    /* slow nod */
    } else {
        look(s, m, rsign() * 2.0f, 1.0f, 0.0f, 1.5f, now);
    }
}

void mao_idle_update(mao_idle_t *s, mao_motion_t *m, uint32_t now, bool sleepy)
{
    if (s->glance_until && (int32_t)(now - s->glance_until) >= 0) {
        s->glance_until = 0;
        mao_motion_set(m, CH_GAZE_X, 0.0f);
        mao_motion_set(m, CH_GAZE_Y, 0.0f);
        mao_motion_set(m, CH_FACE_X, s->base_x);
        mao_motion_set(m, CH_SQUINT, 0.0f);
        mao_motion_set(m, CH_TILT, 0.0f);
    }
    if ((int32_t)(now - s->next_ms) >= 0) {
        if (sleepy) {
            run_sleepy_event(s, m, now);
        } else {
            run_awake_event(s, m, now);
        }
        mao_idle_schedule(s, now, sleepy, false);
    }
}

void mao_idle_cancel(mao_idle_t *s, mao_motion_t *m)
{
    if (s->glance_until) {
        s->glance_until = 0;
        mao_motion_set(m, CH_GAZE_X, 0.0f);
        mao_motion_set(m, CH_GAZE_Y, 0.0f);
    }
    mao_motion_set(m, CH_SQUINT, 0.0f);
    m->blinking = false;   /* a blink in progress must not hide a reaction */
}
