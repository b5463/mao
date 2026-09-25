/*
 * MAO character: command intake and the per-tick pipeline. Runs on an LVGL
 * timer; commands arrive through a queue (see docs/character_architecture.md).
 *
 * On top of the dial physics (mao_character_dial.c) runs the Lark-style
 * expression layer (mao_lark.c): authored states chosen by a small mood
 * model, blended additively and weighted down while the user is
 * interacting; and the inner life (mao_life.c): drives and impulses that make
 * MAO act on its own, real saccades, cat mode, and reactions to people that
 * depend on its mood.
 */
#include "mao_character_internal.h"

#include "esp_log.h"

static const char *TAG = "MAO_CHARACTER";

/* The motion tick must not share the display's 33 ms refresh period: two
 * unsynchronised 33 ms timers beat, a refresh regularly found no fresh pose
 * and motion stuttered to 66 ms steps (measured: median gap 38 ms, max 67).
 * At 16 ms the median gap fell to 20 ms (~50 fps) but render-busy rose to
 * ~39 %; 25 ms lands the target ~30 fps with a uniform cadence. */
#define TICK_MS        25
#define CMD_QUEUE_LEN  16

static const mao_look_t kLooks[] = MAO_LOOKS;
#define LOOK_COUNT ((int)(sizeof(kLooks) / sizeof(kLooks[0])))

/* The one owner of all character state. Zero-initialised (.bss, not a
 * multi-KB .data image); the few non-zero defaults are set in create(). */
static mao_char_t s_c;

static const char *const kStateNames[MAO_CHAR_STATE_COUNT] = {
    "IDLE", "NOTICE", "FOLLOW", "DIZZY", "SLEEPY", "SURPRISED", "WARM", "AWAY",
    "EXIT", "GONE", "ENTER", "BASH",
};
static const char *const kPreviewNames[MAO_CHAR_PREVIEW_COUNT] = {
    "idle", "blink", "follow", "fast", "vfast", "dizzy", "press", "happy", "sleepy", "leave", "hide",
};

const char *mao_character_state_name(mao_character_state_t st)
{
    return st < MAO_CHAR_STATE_COUNT ? kStateNames[st] : "?";
}

const char *mao_character_preview_name(mao_character_preview_t p)
{
    return p < MAO_CHAR_PREVIEW_COUNT ? kPreviewNames[p] : "?";
}

mao_character_state_t mao_character_get_state(void)
{
    return s_c.state;
}

int mao_character_look_count(void)
{
    return LOOK_COUNT;
}

/* ---------------------------------------------------------------------- */
/* Priority                                                               */
/* ---------------------------------------------------------------------- */

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

static void update_state(mao_char_t *mc, uint32_t now)
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

/* ---------------------------------------------------------------------- */
/* Commands                                                               */
/* ---------------------------------------------------------------------- */

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


static void on_press(mao_char_t *mc, bool down, uint32_t now)
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

static void react(mao_char_t *mc, mao_character_reaction_t r, uint32_t now)
{
    if (r == MAO_CHAR_REACT_WAKE) {
        mao_char_wake(mc, now);
        return;
    }
    if (r >= MAO_CHAR_REACT_ACK && r < MAO_CHAR_REACT_COUNT) {
        /* Controller feedback: queued, and played as soon as MAO is on
         * screen - even mid-dial, at full strength. The mind hears about it
         * too (interest, habituation, the analytical EVALUATE phase). */
        static const char *const kFb[] = {
            [MAO_CHAR_REACT_ACK] = "ack", [MAO_CHAR_REACT_BUSY] = "busy", [MAO_CHAR_REACT_DONE] = "done",
            [MAO_CHAR_REACT_FAIL] = "fail", [MAO_CHAR_REACT_BACK] = "back",
            [MAO_CHAR_REACT_DEVICE_ON] = "device_on", [MAO_CHAR_REACT_DEVICE_OFF] = "device_off",
            [MAO_CHAR_REACT_UNSURE] = "neutral",   /* unreachable: handled above */
            [MAO_CHAR_REACT_IDLE] = "neutral",
        };
        mao_char_wake(mc, now);
        mc->fb_play_at = 0;
        switch (r) {
        case MAO_CHAR_REACT_DEVICE_ON:
            mao_life_event(&mc->life, &mc->lark, LIFE_EV_DEVICE_NEW, now);
            break;
        case MAO_CHAR_REACT_DEVICE_OFF:
            mao_life_event(&mc->life, &mc->lark, LIFE_EV_DEVICE_LOST, now);
            break;
        case MAO_CHAR_REACT_ACK:
            /* Pending attention: focused waiting, no celebration. */
            mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_WAIT, now);
            break;
        case MAO_CHAR_REACT_UNSURE:
            /* No verdict exists: EVALUATE, a later second look, and back to
             * work. No lark state at all - the mind carries it. */
            mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_UNSURE, now);
            return;
        case MAO_CHAR_REACT_DONE:
            mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_OK, now);
            break;
        case MAO_CHAR_REACT_FAIL:
            /* Analysis first: freeze and study for a beat, then the verdict. */
            mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_FAIL, now);
            mc->fb_play_at = now + 450;
            break;
        case MAO_CHAR_REACT_BUSY:
            mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_BUSY, now);
            if (mc->life.habit_busy > 0.65f) {
                /* The third "busy" in a row barely registers: a slight
                 * narrowing from the mind's evaluation, nothing more. */
                ESP_LOGI(TAG, "busy barely noted (habituated %.2f)", (double)mc->life.habit_busy);
                return;
            }
            break;
        default:
            break;
        }
        mc->fb_pending = mao_lark_find(kFb[r]);
        mc->fb_hold = r == MAO_CHAR_REACT_BUSY;
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

static void appear(mao_char_t *mc, int dir, uint32_t now)
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

static void leave(mao_char_t *mc, uint32_t now)
{
    if (!mc->visible) {
        return;
    }
    mao_idle_cancel(&mc->idle, &mc->m);
    mao_motion_profile(&mc->m, CH_AWAY, MAO_P_AWAY);
    /* 1. notice the double click: eyes widen, tiny "o". */
    mao_motion_set(&mc->m, CH_OPEN, MAO_SURPRISE_OPEN);
    mao_motion_set(&mc->m, CH_SPREAD, 2.0f);
    mao_motion_set(&mc->m, CH_ORBIT_R, 0.0f);
    mc->mouth = MAO_MOUTH_O;
    mc->present = false;
    mc->leave_drop_at = now + (uint32_t)(MAO_SURPRISE_S * 1000.0f);
    mc->away_until = now + 600;
}

static void come_back(mao_char_t *mc, uint32_t now)
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
static void peek_set(mao_char_t *mc, bool on, uint32_t now)
{
    if (on == mc->peek) {
        return;
    }
    mc->peek = on;
    /* A peek into the tool, not a view transition: quick in, quick out. */
    mao_motion_profile(&mc->m, CH_AWAY, (mao_spring_profile_t){ .k = 520.0f, .zeta = 0.95f });
    if (on) {
        mc->present = true;
        mc->mouth = MAO_MOUTH_NONE;
        mc->leave_drop_at = 0;
        mao_motion_set(&mc->m, CH_AWAY, 0.0f);
        mao_motion_set(&mc->m, CH_CLOSE, 0.0f);
        mao_motion_set(&mc->m, CH_OPEN, 1.0f);
        mao_motion_set(&mc->m, CH_SQUASH, 0.0f);
        mao_motion_set(&mc->m, CH_SPREAD, 0.0f);
        mao_motion_set(&mc->m, CH_EYE_W, MAO_PEEK_SHRINK_W);
        mao_motion_set(&mc->m, CH_EYE_H, MAO_PEEK_SHRINK_H);
        mao_motion_set(&mc->m, CH_FACE_Y, mc->idle.base_y + MAO_PEEK_DROP);
        mao_motion_set(&mc->m, CH_GAZE_X, 0.0f);
        mao_motion_set(&mc->m, CH_GAZE_Y, MAO_PEEK_GAZE_UP);   /* attention on the page */
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

static void set_sleepy(mao_char_t *mc, bool sleepy, uint32_t now)
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


/* ---------------------------------------------------------------------- */
/* Tick                                                                   */
/* ---------------------------------------------------------------------- */

static void preview(mao_char_t *mc, mao_character_preview_t p, uint32_t now);

static void apply(mao_char_t *mc, const cmd_t *c, uint32_t now)
{
    switch (c->type) {
    case CMD_DIAL:       mao_char_on_dial(mc, c->value, now); break;
    case CMD_PRESS:      on_press(mc, c->flag, now); break;
    case CMD_REACT:      react(mc, (mao_character_reaction_t)c->arg, now); break;
    case CMD_APPEAR:     appear(mc, c->arg, now); break;
    case CMD_SLEEPY:     set_sleepy(mc, c->flag, now); break;
    case CMD_LEAVE:      leave(mc, now); break;
    case CMD_RETURN:     come_back(mc, now); break;
    case CMD_PREVIEW:    preview(mc, (mao_character_preview_t)c->arg, now); break;
    case CMD_DEBUG_DIAL: mc->dbg_dps = c->f; mc->dbg_acc = 0.0f; mc->dbg_dial_until = now + (uint32_t)c->value; break;
    case CMD_LOOK:       mc->m.look = kLooks[c->arg]; ESP_LOGI(TAG, "look '%s'", kLooks[c->arg].name); break;
    case CMD_EXPRESSION: mao_lark_switch(&mc->lark, c->arg, now); break;
    case CMD_TRANSFER: {
        const int dx = c->value / 10, dy = c->value % 10;
        if (c->arg == (int8_t)MAO_TR_NONE) {
            mao_transfer_begin(&mc->transfer, &mc->m, MAO_TR_NONE, 0, 0, now);
            mao_idle_schedule(&mc->idle, now, mc->sleepy, true);
        } else {
            if (!mc->visible) {
                appear(mc, 0, now);
            }
            mao_char_wake(mc, now);
            mc->fb_pending = -1;   /* a transfer outranks queued feedback */
            mao_transfer_begin(&mc->transfer, &mc->m, (uint8_t)c->arg, dx, dy, now);
        }
        break;
    }
    case CMD_PEEK:       peek_set(mc, c->flag, now); break;
    case CMD_MIND:
        if (c->arg == 0) {
            mao_life_debug_interest(&mc->life, (uint8_t)c->value);
        } else {
            mao_life_debug_novelty(&mc->life, (uint8_t)c->value);
        }
        break;
    default: break;
    }
}

static void timed_reactions(mao_char_t *mc, uint32_t now)
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
        on_press(mc, false, now);
    }
    if (mc->dbg_return_at && !before(now, mc->dbg_return_at)) {
        mc->dbg_return_at = 0;
        come_back(mc, now);
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    mao_char_t *const mc = &s_c;
    const uint32_t now = lv_tick_get();
    float dt = (float)(now - mc->last_tick_ms) / 1000.0f;
    mc->last_tick_ms = now;
    dt = clampf(dt, 0.005f, 0.05f);   /* after a stall, don't let springs jump */

    cmd_t c;
    while (xQueueReceive(mc->cmds, &c, 0) == pdTRUE) {
        apply(mc, &c, now);
    }
    if (before(now, mc->dbg_dial_until)) {
        mc->dbg_acc += mc->dbg_dps * dt;
        const int32_t n = (int32_t)mc->dbg_acc;
        if (n) {
            mc->dbg_acc -= (float)n;
            mao_char_on_dial(mc, n, now);
        }
    }

    mao_char_dial_update(mc, dt, now);
    timed_reactions(mc, now);
    mao_transfer_tick(&mc->transfer, &mc->m, &mc->draw, now);
    if (mc->visible && mao_char_current_prio(mc, now) == PRIO_IDLE) {
        /* Idle behaviour now comes from the mind (mao_life.c). */
    } else if (mao_char_current_prio(mc, now) != PRIO_IDLE) {
        /* Something more important is happening: push idle back. */
        mao_idle_schedule(&mc->idle, now, mc->sleepy, true);
    }
    update_state(mc, now);

    /* Expression layer: full in idle, faint while the user is in charge. */
    if (mc->fb_pending >= 0 && mc->visible && mc->present && !before(now, mc->away_until) &&
        mc->transfer.phase == MAO_TR_NONE && !before(now, mc->fb_play_at)) {
        const int st = mc->fb_pending;
        mc->fb_pending = -1;
        mc->lark.pending = -1;           /* controller feedback wins over a queued mood */
        mao_lark_switch(&mc->lark, st, now);
        const lark_state_t *ls = mao_lark_state(st);
        mc->fb_until = now + ((ls->flags & LARK_ONESHOT) ? (uint32_t)ls->length_ms + 300u : 0u);
    }
    if (mc->fb_hold && mc->lark.cur != mao_lark_find("busy")) {
        mc->fb_hold = false;             /* busy ended (done / fail / idle / input) */
    }
    static const float kLayerGain[] = { [PRIO_IDLE] = 1.0f, [PRIO_SYSTEM] = 0.5f, [PRIO_DIAL] = 0.25f,
                                        [PRIO_PRESS] = 0.45f, [PRIO_NAV] = 0.0f };
    const prio_t prio = mao_char_current_prio(mc, now);
    const bool feedback = mc->fb_hold || before(now, mc->fb_until);
    float gain = feedback ? 1.0f : kLayerGain[prio];
    if (mc->peek) {
        gain *= MAO_PEEK_LAYER_GAIN;   /* the page is in charge; MAO observes */
    }
    mao_lark_update(&mc->lark, now, mc->visible && prio == PRIO_IDLE, mc->sleepy, gain, mc->m.layer);
    float life[CH_COUNT] = { 0 };
    mao_life_update(&mc->life, &mc->lark, &mc->m, now, mc->visible && prio == PRIO_IDLE, mc->sleepy, life);
    if (prio != PRIO_NAV) {
        const float lg = mc->peek ? MAO_PEEK_LAYER_GAIN : 1.0f;
        for (int i = 0; i < CH_COUNT; i++) {
            mc->m.layer[i] += life[i] * lg;
        }
    }

    mao_motion_step(&mc->m, dt, now);
    if (mc->visible) {
        mao_pose_t pose;
        mao_motion_pose(&mc->m, mc->mouth, now, &pose);
        mao_char_draw_apply(&mc->draw, &pose);
        mao_char_draw_star(&mc->draw, &pose);
    }
}

/* ---------------------------------------------------------------------- */
/* Development previews                                                   */
/* ---------------------------------------------------------------------- */

static void preview(mao_char_t *mc, mao_character_preview_t p, uint32_t now)
{
    ESP_LOGI(TAG, "preview '%s'", mao_character_preview_name(p));
    if (p == MAO_CHAR_PREVIEW_HIDE) {
        mc->visible = false;
        mc->m.ch[CH_OPEN].x = 0.0f;
        mc->m.ch[CH_OPEN].v = 0.0f;
        mao_motion_set(&mc->m, CH_OPEN, 0.0f);
        mao_char_draw_hide(&mc->draw);
        return;
    }
    if (!mc->visible) {
        appear(mc, 0, now);
    }
    switch (p) {
    case MAO_CHAR_PREVIEW_IDLE:
        mc->disturb = 0.0f;
        mc->dbg_dial_until = 0;
        mao_char_wake(mc, now);
        break;
    case MAO_CHAR_PREVIEW_BLINK:     mao_motion_blink(&mc->m, now, 150, 1); break;
    case MAO_CHAR_PREVIEW_FOLLOW:    mc->dbg_dps = 6.0f;  mc->dbg_dial_until = now + 2500; break;
    case MAO_CHAR_PREVIEW_FAST:      mc->dbg_dps = 35.0f; mc->dbg_dial_until = now + 3000; break;
    case MAO_CHAR_PREVIEW_VERY_FAST: mc->dbg_dps = 75.0f; mc->dbg_dial_until = now + 3000; break;
    case MAO_CHAR_PREVIEW_DIZZY:
        for (int k = 0; k < 4; k++) {
            mao_char_on_dial(mc, k % 2 ? 1 : -1, now);
        }
        break;
    case MAO_CHAR_PREVIEW_PRESS:     on_press(mc, true, now); mc->dbg_release_at = now + 700; break;
    case MAO_CHAR_PREVIEW_WARM:      react(mc, MAO_CHAR_REACT_WARM, now); break;
    case MAO_CHAR_PREVIEW_SLEEPY:    set_sleepy(mc, !mc->sleepy, now); break;
    case MAO_CHAR_PREVIEW_LEAVE:     leave(mc, now); mc->dbg_return_at = now + 1200; break;
    default: break;
    }
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

esp_err_t mao_character_create(lv_obj_t *parent)
{
    mao_char_t *const mc = &s_c;
    mc->fb_pending = -1;
    mc->present = true;
    mc->cmds = xQueueCreate(CMD_QUEUE_LEN, sizeof(cmd_t));
    if (!mc->cmds) {
        return ESP_ERR_NO_MEM;
    }
    mao_motion_init(&mc->m, &kLooks[MAO_LOOK_DEFAULT]);
    mao_lark_init(&mc->lark, lv_tick_get());
    mao_life_init(&mc->life, lv_tick_get());
    esp_err_t err = mao_char_draw_create(&mc->draw, parent);
    if (err != ESP_OK) {
        return err;
    }
    mc->last_tick_ms = lv_tick_get();
    mao_idle_schedule(&mc->idle, mc->last_tick_ms, false, true);
    if (!lv_timer_create(tick_cb, TICK_MS, NULL)) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "character ready: look '%s', %d ms motion tick", kLooks[MAO_LOOK_DEFAULT].name, TICK_MS);
    return ESP_OK;
}

static void post(cmd_t c)
{
    if (s_c.cmds) {
        xQueueSend(s_c.cmds, &c, 0);
    }
}

void mao_character_set_detents_per_rev(uint8_t n)
{
    if (n > 0) {
        s_c.detents_per_rev = n;
    }
}

void mao_character_dial(int32_t detents)        { post((cmd_t){ .type = CMD_DIAL, .value = detents }); }
void mao_character_press(bool down)             { post((cmd_t){ .type = CMD_PRESS, .flag = down }); }
void mao_character_react(mao_character_reaction_t r) { post((cmd_t){ .type = CMD_REACT, .arg = (int8_t)r }); }
void mao_character_appear(int direction)        { post((cmd_t){ .type = CMD_APPEAR, .arg = (int8_t)direction }); }
void mao_character_set_sleepy(bool sleepy)      { post((cmd_t){ .type = CMD_SLEEPY, .flag = sleepy }); }
void mao_character_leave(void)                  { post((cmd_t){ .type = CMD_LEAVE }); }
void mao_character_return(void)                 { post((cmd_t){ .type = CMD_RETURN }); }
void mao_character_peek(bool on)                { post((cmd_t){ .type = CMD_PEEK, .flag = on }); }

void mao_character_debug_preview(mao_character_preview_t p)
{
    post((cmd_t){ .type = CMD_PREVIEW, .arg = (int8_t)p });
}

static void post_transfer(mao_transfer_phase_t phase, int dx, int dy)
{
    post((cmd_t){ .type = CMD_TRANSFER, .arg = (int8_t)phase, .value = dx * 10 + dy });
}

void mao_character_transfer_search(int dx, int dy) { post_transfer(MAO_TR_SEARCH, dx, dy); }
void mao_character_transfer_exit(int dx, int dy)   { post_transfer(MAO_TR_EXIT, dx, dy); }
void mao_character_transfer_fail(int dx, int dy)   { post_transfer(MAO_TR_BASH, dx, dy); }
void mao_character_transfer_return(int dx, int dy) { post_transfer(MAO_TR_ENTER, dx, dy); }
void mao_character_transfer_abort(void)            { post_transfer(MAO_TR_NONE, 0, 0); }

void mao_character_debug_interest(uint8_t pct)
{
    post((cmd_t){ .type = CMD_MIND, .arg = 0, .value = pct > 100 ? 100 : pct });
}

void mao_character_debug_novelty(uint8_t pct)
{
    post((cmd_t){ .type = CMD_MIND, .arg = 1, .value = pct > 100 ? 100 : pct });
}

void mao_character_debug_dial(float dps, uint32_t ms)
{
    post((cmd_t){ .type = CMD_DEBUG_DIAL, .f = dps, .value = (int32_t)ms });
}

bool mao_character_debug_look(int index)
{
    if (index < 0 || index >= LOOK_COUNT) {
        return false;
    }
    post((cmd_t){ .type = CMD_LOOK, .arg = (int8_t)index });
    return true;
}

bool mao_character_debug_expression(int index)
{
    if (index < 0 || index >= mao_lark_state_count()) {
        return false;
    }
    post((cmd_t){ .type = CMD_EXPRESSION, .arg = (int8_t)index });
    return true;
}

int mao_character_expression_count(void)
{
    return mao_lark_state_count();
}

const char *mao_character_expression_name(int index)
{
    return index >= 0 && index < mao_lark_state_count() ? mao_lark_state(index)->name : "?";
}

const char *mao_character_reaction_name(mao_character_reaction_t r)
{
    static const char *const kNames[MAO_CHAR_REACT_COUNT] = {
        "notice", "attend", "warm", "wake", "ack", "busy", "done", "fail", "back", "device_on", "device_off",
        "unsure", "idle",
    };
    return r < MAO_CHAR_REACT_COUNT ? kNames[r] : "?";
}
