/*
 * MAO character - dial physics (MOTION stage, input side).
 *
 * Dial model: detents feed a smoothed signed speed estimate. Intensity
 * (speed / MAO_DIAL_FULL_DPS) is mapped continuously onto gaze, face shift,
 * tilt, acceleration lean, stretch, cobalt tint and orbit radius, so there are
 * no visible thresholds. Gaze has a faster spring than the face, so the eyes
 * lead and the face follows; when the dial stops the face settles first and
 * the eyes keep looking a moment longer. Direction reversals add to a
 * decaying "disturbance": each one kicks the face sideways (harder as it
 * accumulates) and above a level the eyes lose coordination.
 */
#include "mao_character_internal.h"

bool mao_char_dial_engaged(mao_char_t *mc, uint32_t now)
{
    return mc->last_detent_ms && (now - mc->last_detent_ms) < (uint32_t)(MAO_DIAL_ENGAGE_S * 1000.0f);
}

void mao_char_on_dial(mao_char_t *mc, int32_t n, uint32_t now)
{
    if (!mc->visible || mao_char_current_prio(mc, now) == PRIO_NAV || n == 0) {
        return;
    }
    mao_char_wake(mc, now);
    if (!mc->last_input_ms || now - mc->last_input_ms > (uint32_t)(MAO_SPARK_AFTER_S * 1000.0f)) {
        /* Something interesting: a brief keen widening before following. */
        mao_motion_set(&mc->m, CH_OPEN, MAO_SPARK_OPEN);
        mc->wide_until = now + (uint32_t)(MAO_SPARK_S * 1000.0f);
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_FIRST_TOUCH, now);
    }
    mc->last_input_ms = now;
    mao_lark_event(&mc->lark, LARK_EV_INPUT, now);
    mao_life_event(&mc->life, &mc->lark, LIFE_EV_INPUT, now);
    const int sign = n > 0 ? 1 : -1;
    if (mc->last_sign && sign != mc->last_sign &&
        (now - mc->last_detent_ms) < (uint32_t)(MAO_REV_WINDOW_S * 1000.0f)) {
        /* Reversal: momentum jerks the face on in the old direction while the
         * eyes (fast spring) already look the new way. Accumulates. */
        mc->disturb += 1.0f;
        mao_lark_event(&mc->lark, LARK_EV_REVERSAL, now);
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_REVERSAL, now);
        mao_motion_kick(&mc->m, CH_FACE_X, (float)mc->last_sign * MAO_REV_KICK * (1.0f + mc->disturb));
    }
    mc->last_sign = sign;
    mc->last_detent_ms = now;
    mc->tick_detents += n;
    mao_life_dial(&mc->life, n, now);
}

void mao_char_dial_update(mao_char_t *mc, float dt, uint32_t now)
{
    const int32_t n = mc->tick_detents;
    mc->tick_detents = 0;

    const float rate = (float)n / dt;
    const float tau = fabsf(rate) > fabsf(mc->speed) ? MAO_DIAL_TAU_UP : MAO_DIAL_TAU_DOWN;
    mc->speed += (rate - mc->speed) * (1.0f - expf(-dt / tau));
    const float abs_speed = fabsf(mc->speed);
    mc->accel += ((abs_speed - mc->prev_abs) / dt - mc->accel) * (1.0f - expf(-dt / 0.10f));
    mc->prev_abs = abs_speed;
    mc->disturb *= expf(-dt / MAO_REV_DECAY_S);

    const float i = clampf(abs_speed / MAO_DIAL_FULL_DPS, 0.0f, 1.0f);
    const float dir = (float)(mc->last_sign ? mc->last_sign : 1);
    const bool engaged = mao_char_dial_engaged(mc, now) || i > 0.05f;
    const float wob = clampf((mc->disturb - MAO_REV_WOBBLE_START) * MAO_REV_WOBBLE_GAIN, 0.0f, MAO_REV_WOBBLE_MAX);
    mao_motion_set(&mc->m, CH_WOBBLE, wob);
    if (mc->disturb >= MAO_REV_DIZZY && !mc->dizzy_noted) {
        mc->dizzy_noted = true;   /* the dizzy expression plays once the dial stops */
        mao_lark_event(&mc->lark, LARK_EV_DIZZY, now);
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_DIZZY, now);
    } else if (mc->disturb < 1.0f) {
        mc->dizzy_noted = false;
    }

    if (!mc->present) {
        return;
    }
    if (engaged) {
        const float pk = mc->peek ? MAO_PEEK_DIAL_GAIN : 1.0f;
        const float s = smoothstep(0.0f, MAO_LATERAL_RAMP, i) * pk;
        const float o = mc->peek ? 0.0f : smoothstep(MAO_ORBIT_START, MAO_ORBIT_FULL, i);
        const float lat = 1.0f - o;
        mao_motion_set(&mc->m, CH_GAZE_X, dir * (MAO_GAZE_MIN + (MAO_GAZE_MAX - MAO_GAZE_MIN) * s) * lat
                                        + dir * MAO_GAZE_LAG_ORBIT * o);
        mao_motion_set(&mc->m, CH_GAZE_Y, mc->peek ? MAO_PEEK_GAZE_UP : 0.0f);
        mao_motion_set(&mc->m, CH_FACE_X, mc->idle.base_x + dir * (MAO_FACE_MIN + (MAO_FACE_MAX - MAO_FACE_MIN) * s) * lat);
        const float lean = clampf(mc->accel * MAO_LEAN_GAIN, -MAO_LEAN_MAX, MAO_LEAN_MAX);
        mao_motion_set(&mc->m, CH_TILT, dir * (MAO_TILT_MAX * s * lat + lean));
        mao_motion_set(&mc->m, CH_SQUASH, (mc->pressed ? MAO_PRESS_SQUASH : 0.0f) - MAO_MOTION_STRETCH * i);
        mao_motion_set(&mc->m, CH_TINT_MOVE, o);
        if (!before(now, mc->wide_until)) {
            mao_motion_set(&mc->m, CH_OPEN, 1.0f - 0.06f * wob);
        }

        if (o > 0.01f) {
            if (!mc->orbiting) {
                mc->orbiting = true;
                if (mc->m.ch[CH_ORBIT_R].x < 2.0f) {
                    /* Start on the side we are turning towards. */
                    mc->orbit_target = dir * PI_F / 2.0f;
                    mc->m.ch[CH_ORBIT_A].x = mc->orbit_target;
                    mc->m.ch[CH_ORBIT_A].v = 0.0f;
                } else {
                    mc->orbit_target = mc->m.ch[CH_ORBIT_A].x;
                }
            }
            /* Follow the knob's real angle (the spring adds a slight lag). */
            mc->orbit_target += (float)n * TWO_PI_F / (float)mao_char_detents_per_rev(mc);
            mao_motion_set(&mc->m, CH_ORBIT_A, mc->orbit_target);
            if (o > 0.5f) {
                mao_lark_event(&mc->lark, LARK_EV_FAST, now);
            }
            mao_motion_set(&mc->m, CH_ORBIT_R, o * MAO_ORBIT_RADIUS +
                           smoothstep(MAO_ORBIT_RIM_START, 1.0f, i) * MAO_ORBIT_RIM_EXTRA);
        } else {
            mao_motion_set(&mc->m, CH_ORBIT_R, 0.0f);
        }
        return;
    }

    /* Disengaged: the face settles first, the eyes follow a moment later. */
    if (mc->last_detent_ms && (now - mc->last_detent_ms) < 5000) {
        mao_motion_set(&mc->m, CH_FACE_X, mc->idle.base_x);
        mao_motion_set(&mc->m, CH_TILT, 0.0f);
        mao_motion_set(&mc->m, CH_ORBIT_R, 0.0f);
        mao_motion_set(&mc->m, CH_TINT_MOVE, 0.0f);
        mao_motion_set(&mc->m, CH_SQUASH, mc->pressed ? MAO_PRESS_SQUASH : 0.0f);
        if ((now - mc->last_detent_ms) > (uint32_t)((MAO_DIAL_ENGAGE_S + MAO_GAZE_HOLD_S) * 1000.0f) &&
            !mc->idle.glance_until && !before(now, mc->react_until)) {
            mao_motion_set(&mc->m, CH_GAZE_X, 0.0f);
        }
        if (mc->orbiting && mc->m.ch[CH_ORBIT_R].x < 1.0f) {
            mc->orbiting = false;
            /* Keep the angle bounded once the orbit has collapsed. */
            const float wraps = TWO_PI_F * floorf(mc->orbit_target / TWO_PI_F);
            mc->orbit_target -= wraps;
            mc->m.ch[CH_ORBIT_A].x -= wraps;
            mao_motion_set(&mc->m, CH_ORBIT_A, mc->orbit_target);
        }
    }
}
