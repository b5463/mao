/*
 * MAO character - REACTION stage and the state it reports.
 *
 * Priorities decide who owns the face (navigation > press > dial > system
 * reaction > idle); input handlers (press), reactions (notice / attend /
 * warm), view choreography (appear, leave, return, peek, sleepy) and the
 * timed ends of those reactions. mao_char_update_state() derives the reported
 * character state from all of it.
 */
#include "mao_character_internal.h"

#include "esp_log.h"

static const char *TAG = "MAO_CHARACTER";

static const char *const kStateNames[MAO_CHAR_STATE_COUNT] = {
    "IDLE", "NOTICE", "FOLLOW", "DIZZY", "SLEEPY", "SURPRISED", "WARM", "AWAY",
    "EXIT", "GONE", "ENTER", "BASH",
};

const char *mao_character_state_name(mao_character_state_t st)
{
    return st < MAO_CHAR_STATE_COUNT ? kStateNames[st] : "?";
}

prio_t mao_char_current_prio(mao_char_t *mc, uint32_t now)
{
    if (mc->transfer.phase != MAO_TR_NONE || !mc->present || before(now, mc->away_until)) {
        return PRIO_NAV;
    }
    if (mc->pressed || before(now, mc->press_until) || before(now, mc->warm_until)) {
        return PRIO_PRESS;
    }
    if (mao_char_dial_engaged(mc, now) || fabsf(mc->speed) > 1.0f || mc->disturb > 0.3f) {
        return PRIO_DIAL;
    }
    if (before(now, mc->react_until)) {
        return PRIO_SYSTEM;
    }
    return PRIO_IDLE;
}

void mao_char_update_state(mao_char_t *mc, uint32_t now)
{
    mao_character_state_t st;
    if (mc->transfer.phase != MAO_TR_NONE) {
        st = mc->transfer.phase == MAO_TR_EXIT ? MAO_CHAR_EXIT :
             mc->transfer.phase == MAO_TR_GONE ? MAO_CHAR_GONE :
             mc->transfer.phase == MAO_TR_ENTER ? MAO_CHAR_ENTER :
             mc->transfer.phase == MAO_TR_BASH ? MAO_CHAR_BASH : MAO_CHAR_NOTICE;
    } else if (!mc->present || before(now, mc->away_until)) {
        st = before(now, mc->leave_drop_at) ? MAO_CHAR_SURPRISED : MAO_CHAR_AWAY;
    } else if (before(now, mc->warm_until)) {
        st = MAO_CHAR_WARM;
    } else if (mc->pressed || before(now, mc->press_until) || before(now, mc->react_until)) {
        st = MAO_CHAR_NOTICE;
    } else if (mc->disturb >= MAO_REV_DIZZY) {
        st = MAO_CHAR_DIZZY;
    } else if (mao_char_dial_engaged(mc, now) || fabsf(mc->speed) > 1.0f) {
        st = MAO_CHAR_FOLLOW;
    } else if (mc->sleepy) {
        st = MAO_CHAR_SLEEPY;
    } else {
        st = MAO_CHAR_IDLE;
    }
    if (st != mc->state) {
        ESP_LOGI(TAG, "%s -> %s", kStateNames[mc->state], kStateNames[st]);
        mc->state = st;
    }
}

void mao_char_wake(mao_char_t *mc, uint32_t now)
{
    if (mc->sleepy) {
        mc->sleepy = false;
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_WOKEN, now);
        /* Waking is fast: swap the slow sleep spring for a quick one. */
        mao_motion_profile(&mc->m, CH_SLEEP, (mao_spring_profile_t){ .k = 300.0f, .zeta = 0.9f });
        mao_motion_set(&mc->m, CH_SLEEP, 0.0f);
    }
    mao_idle_cancel(&mc->idle, &mc->m);
    mao_idle_schedule(&mc->idle, now, false, true);
}

void mao_char_on_press(mao_char_t *mc, bool down, uint32_t now)
{
    if (!mc->visible || !mc->present) {
        return;
    }
    mao_char_wake(mc, now);
    if (down && (!mc->last_input_ms || now - mc->last_input_ms > (uint32_t)(MAO_SPARK_AFTER_S * 1000.0f))) {
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_FIRST_TOUCH, now);
    }
    mc->last_input_ms = now;
    mao_lark_event(&mc->lark, LARK_EV_INPUT, now);
    mao_life_event(&mc->life, &mc->lark, LIFE_EV_INPUT, now);
    if (down) {
        mao_lark_event(&mc->lark, LARK_EV_PRESS, now);
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_TOUCH, now);
    }
    mc->pressed = down;
    mao_motion_set(&mc->m, CH_PRESS, down ? MAO_PRESS_DROP : 0.0f);
    mao_motion_set(&mc->m, CH_SPREAD, down ? MAO_PRESS_SPREAD : 0.0f);
    mao_motion_set(&mc->m, CH_SQUASH, down ? MAO_PRESS_SQUASH : 0.0f);
    if (!down) {
        /* Release is the satisfying part: a lift and a brief stretch. */
        mao_motion_kick(&mc->m, CH_PRESS, MAO_RELEASE_KICK);
        mao_motion_kick(&mc->m, CH_SQUASH, MAO_RELEASE_SQUASH_KICK);
        mc->press_until = now + 300;
    }
}

void mao_char_react(mao_char_t *mc, mao_character_reaction_t r, uint32_t now)
{
    if (r == MAO_CHAR_REACT_WAKE) {
        mao_char_wake(mc, now);
        return;
    }
    if (r >= MAO_CHAR_REACT_ACK && r < MAO_CHAR_REACT_COUNT) {
        mao_char_feedback(mc, r, now);
        return;
    }
    if (!mc->visible || !mc->present) {
        return;
    }
    if (r == MAO_CHAR_REACT_WARM) {                       /* press priority (long press) */
        mao_char_wake(mc, now);
        mao_motion_set(&mc->m, CH_NARROW, MAO_WARM_NARROW);
        mao_motion_set(&mc->m, CH_FACE_Y, mc->idle.base_y + MAO_WARM_LIFT);
        mao_motion_set(&mc->m, CH_TINT_WARM, 1.0f);
        mao_motion_kick(&mc->m, CH_FACE_Y, -18.0f);
        mc->warm_until = now + (uint32_t)(MAO_WARM_S * 1000.0f);
        mc->warm_tint_until = now + (uint32_t)(MAO_WARM_TINT_S * 1000.0f);
        mao_lark_event(&mc->lark, LARK_EV_WARM, now);
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_WARM, now);
        return;
    }
    /* System reactions yield to anything the user is doing. */
    if (mao_char_current_prio(mc, now) > PRIO_SYSTEM) {
        ESP_LOGD(TAG, "system reaction skipped (user interaction has priority)");
        return;
    }
    mao_idle_cancel(&mc->idle, &mc->m);
    mao_char_wake(mc, now);
    mao_motion_set(&mc->m, CH_OPEN, MAO_NOTICE_OPEN);
    mc->wide_until = now + (uint32_t)(MAO_NOTICE_S * 1000.0f);
    mao_motion_kick(&mc->m, CH_FACE_Y, -30.0f);
    if (r == MAO_CHAR_REACT_ATTEND) {
        mao_motion_set(&mc->m, CH_GAZE_X, 11.0f);
        mao_motion_set(&mc->m, CH_GAZE_Y, -2.0f);
        mao_motion_set(&mc->m, CH_FACE_X, mc->idle.base_x + 7.0f);
        mc->react_until = now + (uint32_t)(MAO_ATTEND_S * 1000.0f);
    } else {
        mc->react_until = now + (uint32_t)(MAO_NOTICE_S * 1000.0f);
    }
}

void mao_char_appear(mao_char_t *mc, int dir, uint32_t now)
{
    mc->visible = true;
    mc->present = true;
    mao_motion_set(&mc->m, CH_OPEN, 1.0f);
    if (dir != 0) {
        /* Arrive displaced towards the first turn, looking that way, then settle. */
        mc->m.ch[CH_FACE_X].x = (float)dir * MAO_APPEAR_OFFSET;
        mc->m.ch[CH_GAZE_X].x = (float)dir * MAO_GAZE_MAX;
        mc->last_sign = dir;
        mc->last_detent_ms = now;
    }
    mao_idle_schedule(&mc->idle, now, false, true);
}

void mao_char_leave(mao_char_t *mc, uint32_t now)
{
    if (!mc->visible) {
        return;
    }
    mao_idle_cancel(&mc->idle, &mc->m);
    mao_motion_profile(&mc->m, CH_AWAY, MAO_P_AWAY);
    /* 1. notice: eyes widen (eyes only - no mouth, M4.1). */
    mao_motion_set(&mc->m, CH_OPEN, MAO_SURPRISE_OPEN);
    mao_motion_set(&mc->m, CH_SPREAD, 2.0f);
    mao_motion_set(&mc->m, CH_ORBIT_R, 0.0f);
    mc->mouth = MAO_MOUTH_NONE;
    mc->present = false;
    mc->leave_drop_at = now + (uint32_t)(MAO_SURPRISE_S * 1000.0f);
    mc->away_until = now + 600;
}

void mao_char_come_back(mao_char_t *mc, uint32_t now)
{
    mc->present = true;
    mc->peek = false;
    mao_motion_profile(&mc->m, CH_AWAY, MAO_P_AWAY);
    mc->mouth = MAO_MOUTH_NONE;
    mao_motion_set(&mc->m, CH_EYE_W, 0.0f);
    mao_motion_set(&mc->m, CH_EYE_H, 0.0f);
    mao_motion_set(&mc->m, CH_FACE_Y, mc->idle.base_y);
    mc->leave_drop_at = 0;
    mao_motion_set(&mc->m, CH_AWAY, 0.0f);
    mao_motion_set(&mc->m, CH_CLOSE, 0.0f);
    mao_motion_set(&mc->m, CH_SQUASH, mc->pressed ? MAO_PRESS_SQUASH : 0.0f);
    mao_motion_set(&mc->m, CH_SPREAD, 0.0f);
    mao_motion_set(&mc->m, CH_OPEN, 1.0f);
    mc->away_until = now + 350;
    mao_idle_schedule(&mc->idle, now, mc->sleepy, true);
    mao_lark_event(&mc->lark, LARK_EV_RETURN, now);
}

/* Compact presence for the DEVICE page: small eyes low on the screen,
 * watching the page rather than owning it. */
void mao_char_peek_set(mao_char_t *mc, bool on, uint32_t now)
{
    if (on == mc->peek) {
        return;
    }
    mc->peek = on;
    /* Stepping down to the rim is a move with some weight (PAGE), not a
     * blink; the travel is CH_AWAY: unscaled by the look, unlike CH_FACE_Y. */
    mao_motion_profile(&mc->m, CH_AWAY, (mao_spring_profile_t){ .k = 190.0f, .zeta = 0.86f });
    if (on) {
        mc->present = true;
        mc->mouth = MAO_MOUTH_NONE;
        mc->leave_drop_at = 0;
        mao_motion_set(&mc->m, CH_AWAY, MAO_PEEK_DROP);
        mao_motion_set(&mc->m, CH_CLOSE, 0.0f);
        mao_motion_set(&mc->m, CH_OPEN, 1.0f);
        mao_motion_set(&mc->m, CH_SQUASH, 0.0f);
        mao_motion_set(&mc->m, CH_SPREAD, 0.0f);
        mao_motion_set(&mc->m, CH_EYE_W, MAO_PEEK_SHRINK_W);
        mao_motion_set(&mc->m, CH_EYE_H, MAO_PEEK_SHRINK_H);
        mao_motion_set(&mc->m, CH_FACE_Y, mc->idle.base_y);
        mao_motion_set(&mc->m, CH_GAZE_X, mc->attend_gx);
        mao_motion_set(&mc->m, CH_GAZE_Y, mc->attend_gy ? mc->attend_gy : MAO_PEEK_GAZE_UP);   /* the page */
        mc->away_until = now + 250;
        mao_idle_schedule(&mc->idle, now, mc->sleepy, true);
    } else {
        /* Back below the screen (the list view owns the stage again). */
        mc->present = false;
        mao_motion_set(&mc->m, CH_EYE_W, 0.0f);
        mao_motion_set(&mc->m, CH_EYE_H, 0.0f);
        mao_motion_set(&mc->m, CH_AWAY, MAO_LEAVE_Y);
        mao_motion_set(&mc->m, CH_OPEN, MAO_LEAVE_OPEN);
        mao_motion_set(&mc->m, CH_CLOSE, 1.0f);
        mao_motion_set(&mc->m, CH_FACE_Y, mc->idle.base_y);
    }
}

void mao_char_set_sleepy(mao_char_t *mc, bool sleepy, uint32_t now)
{
    if (sleepy == mc->sleepy) {
        return;
    }
    if (sleepy) {
        mc->sleepy = true;
        mao_motion_profile(&mc->m, CH_SLEEP, MAO_SLEEP_PROFILE);
        mao_motion_set(&mc->m, CH_SLEEP, 1.0f);
        mao_idle_schedule(&mc->idle, now, true, false);
    } else {
        mao_char_wake(mc, now);
    }
}

void mao_char_timed_reactions(mao_char_t *mc, uint32_t now)
{
    if (mc->wide_until && !before(now, mc->wide_until)) {
        mc->wide_until = 0;
        mao_motion_set(&mc->m, CH_OPEN, 1.0f);
    }
    if (mc->react_until && !before(now, mc->react_until)) {
        mc->react_until = 0;
        mao_motion_set(&mc->m, CH_GAZE_X, 0.0f);
        mao_motion_set(&mc->m, CH_GAZE_Y, 0.0f);
        mao_motion_set(&mc->m, CH_FACE_X, mc->idle.base_x);
    }
    if (mc->warm_tint_until && !before(now, mc->warm_tint_until)) {
        mc->warm_tint_until = 0;
        mao_motion_set(&mc->m, CH_TINT_WARM, 0.0f);
    }
    if (mc->warm_until && !before(now, mc->warm_until)) {
        mc->warm_until = 0;
        mao_motion_set(&mc->m, CH_NARROW, MAO_REST_NARROW);
        mao_motion_set(&mc->m, CH_FACE_Y, mc->idle.base_y);
    }
    if (mc->leave_drop_at && !before(now, mc->leave_drop_at) && !mc->present) {
        /* 2. drop and fold out through the bottom of the circle. */
        mc->leave_drop_at = 0;
        mc->mouth = MAO_MOUTH_NONE;
        mao_motion_set(&mc->m, CH_AWAY, MAO_LEAVE_Y);
        mao_motion_set(&mc->m, CH_SQUASH, MAO_LEAVE_SQUASH);
        mao_motion_set(&mc->m, CH_OPEN, MAO_LEAVE_OPEN);
        mao_motion_set(&mc->m, CH_CLOSE, 1.0f);   /* pupil looks: crescents as it drops */
        mao_motion_set(&mc->m, CH_SPREAD, 0.0f);
    }
    if (mc->dbg_release_at && !before(now, mc->dbg_release_at)) {
        mc->dbg_release_at = 0;
        mao_char_on_press(mc, false, now);
    }
    if (mc->dbg_return_at && !before(now, mc->dbg_return_at)) {
        mc->dbg_return_at = 0;
        mao_char_come_back(mc, now);
    }
}
