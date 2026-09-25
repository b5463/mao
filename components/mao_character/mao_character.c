/*
 * MAO character: command intake, continuous dial physics, reactions and
 * priorities. Runs on a 30 Hz LVGL timer; commands arrive through a queue.
 *
 * Dial model: detents feed a smoothed signed speed estimate. Intensity
 * (speed / MAO_DIAL_FULL_DPS) is mapped continuously onto gaze, face shift,
 * tilt, acceleration lean, stretch, cobalt tint and orbit radius, so there are
 * no visible thresholds. Gaze has a faster spring than the face, so the eyes
 * lead and the face follows; when the dial stops the face settles first and
 * the eyes keep looking a moment longer. Direction reversals add to a
 * decaying "disturbance": each one kicks the face sideways (harder as it
 * accumulates) and above a level the eyes lose coordination.
 *
 * On top of all this runs the Lark-style expression layer (mao_lark.c):
 * authored states chosen by a small mood model, blended additively and
 * weighted down while the user is interacting; and the inner life
 * (mao_life.c): drives and impulses that make MAO act on its own, real
 * saccades, cat mode, and reactions to people that depend on its mood.
 */
#include "mao_character.h"
#include "mao_character_priv.h"
#include "mao_lark.h"
#include "mao_life.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "MAO_CHARACTER";

#define TICK_MS        33
#define CMD_QUEUE_LEN  16
#define PI_F           3.14159265f
#define TWO_PI_F       6.28318531f

typedef enum { PRIO_IDLE = 0, PRIO_SYSTEM, PRIO_DIAL, PRIO_PRESS, PRIO_NAV } prio_t;

typedef enum {
    CMD_DIAL, CMD_PRESS, CMD_REACT, CMD_APPEAR, CMD_SLEEPY, CMD_LEAVE, CMD_RETURN,
    CMD_PREVIEW, CMD_DEBUG_DIAL, CMD_LOOK, CMD_EXPRESSION, CMD_TRANSFER, CMD_MIND,
} cmd_type_t;

typedef struct {
    uint8_t type;
    int8_t arg;
    bool flag;
    int32_t value;
    float f;
} cmd_t;

static const mao_look_t kLooks[] = MAO_LOOKS;
#define LOOK_COUNT ((int)(sizeof(kLooks) / sizeof(kLooks[0])))

static QueueHandle_t s_cmds;
static mao_motion_t s_m;
static mao_char_draw_t s_draw;
static mao_idle_t s_idle;
static mao_lark_t s_lark;
static mao_life_t s_life;
static mao_transfer_t s_transfer;
static int s_fb_pending = -1;       /* controller feedback state waiting to play */
static uint32_t s_fb_until;         /* feedback playing: full layer gain */
static uint32_t s_fb_play_at;       /* verdicts wait for the mind's EVALUATE phase */
static bool s_fb_hold;              /* a held feedback (busy) is on */
static bool s_dizzy_noted;
static uint32_t s_last_tick_ms;
static volatile mao_character_state_t s_state = MAO_CHAR_IDLE;
static volatile uint8_t s_detents_per_rev = (uint8_t)MAO_DETENTS_PER_REV;

static bool s_visible;          /* has appeared */
static bool s_present = true;   /* false while away (menu) */
static bool s_sleepy;
static bool s_pressed;
static mao_mouth_t s_mouth;

/* Dial physics */
static int32_t s_tick_detents;  /* detents received this tick */
static int s_last_sign;
static uint32_t s_last_detent_ms;
static float s_speed;           /* smoothed signed detents/s */
static float s_prev_abs;
static float s_accel;           /* smoothed d|speed|/dt */
static float s_disturb;
static bool s_orbiting;
static float s_orbit_target;

/* Timed reactions */
static uint32_t s_press_until;
static uint32_t s_react_until;      /* system reaction (notice/attend) */
static uint32_t s_warm_until;
static uint32_t s_warm_tint_until;
static uint32_t s_wide_until;
static uint32_t s_leave_drop_at;    /* leave: when the drop starts */
static uint32_t s_away_until;       /* away/transition in progress */
static uint32_t s_last_input_ms;    /* dial or press, for the curiosity spark */

/* Development */
static uint32_t s_dbg_dial_until;
static float s_dbg_dps, s_dbg_acc;
static uint32_t s_dbg_release_at, s_dbg_return_at;

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
    return s_state;
}

int mao_character_look_count(void)
{
    return LOOK_COUNT;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float smoothstep(float e0, float e1, float x)
{
    const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static bool before(uint32_t now, uint32_t t)
{
    return t && (int32_t)(t - now) > 0;
}

/* ---------------------------------------------------------------------- */
/* Priority                                                               */
/* ---------------------------------------------------------------------- */

static bool dial_engaged(uint32_t now)
{
    return s_last_detent_ms && (now - s_last_detent_ms) < (uint32_t)(MAO_DIAL_ENGAGE_S * 1000.0f);
}

static prio_t current_prio(uint32_t now)
{
    if (s_transfer.phase != MAO_TR_NONE || !s_present || before(now, s_away_until)) {
        return PRIO_NAV;
    }
    if (s_pressed || before(now, s_press_until) || before(now, s_warm_until)) {
        return PRIO_PRESS;
    }
    if (dial_engaged(now) || fabsf(s_speed) > 1.0f || s_disturb > 0.3f) {
        return PRIO_DIAL;
    }
    if (before(now, s_react_until)) {
        return PRIO_SYSTEM;
    }
    return PRIO_IDLE;
}

static void update_state(uint32_t now)
{
    mao_character_state_t st;
    if (s_transfer.phase != MAO_TR_NONE) {
        st = s_transfer.phase == MAO_TR_EXIT ? MAO_CHAR_EXIT :
             s_transfer.phase == MAO_TR_GONE ? MAO_CHAR_GONE :
             s_transfer.phase == MAO_TR_ENTER ? MAO_CHAR_ENTER :
             s_transfer.phase == MAO_TR_BASH ? MAO_CHAR_BASH : MAO_CHAR_NOTICE;
    } else if (!s_present || before(now, s_away_until)) {
        st = before(now, s_leave_drop_at) ? MAO_CHAR_SURPRISED : MAO_CHAR_AWAY;
    } else if (before(now, s_warm_until)) {
        st = MAO_CHAR_WARM;
    } else if (s_pressed || before(now, s_press_until) || before(now, s_react_until)) {
        st = MAO_CHAR_NOTICE;
    } else if (s_disturb >= MAO_REV_DIZZY) {
        st = MAO_CHAR_DIZZY;
    } else if (dial_engaged(now) || fabsf(s_speed) > 1.0f) {
        st = MAO_CHAR_FOLLOW;
    } else if (s_sleepy) {
        st = MAO_CHAR_SLEEPY;
    } else {
        st = MAO_CHAR_IDLE;
    }
    if (st != s_state) {
        ESP_LOGI(TAG, "%s -> %s", kStateNames[s_state], kStateNames[st]);
        s_state = st;
    }
}

/* ---------------------------------------------------------------------- */
/* Commands                                                               */
/* ---------------------------------------------------------------------- */

static void wake(uint32_t now)
{
    if (s_sleepy) {
        s_sleepy = false;
        mao_life_event(&s_life, &s_lark, LIFE_EV_WOKEN, now);
        /* Waking is fast: swap the slow sleep spring for a quick one. */
        mao_motion_profile(&s_m, CH_SLEEP, (mao_spring_profile_t){ .k = 300.0f, .zeta = 0.9f });
        mao_motion_set(&s_m, CH_SLEEP, 0.0f);
    }
    mao_idle_cancel(&s_idle, &s_m);
    mao_idle_schedule(&s_idle, now, false, true);
}

static void on_dial(int32_t n, uint32_t now)
{
    if (!s_visible || current_prio(now) == PRIO_NAV || n == 0) {
        return;
    }
    wake(now);
    if (!s_last_input_ms || now - s_last_input_ms > (uint32_t)(MAO_SPARK_AFTER_S * 1000.0f)) {
        /* Something interesting: a brief keen widening before following. */
        mao_motion_set(&s_m, CH_OPEN, MAO_SPARK_OPEN);
        s_wide_until = now + (uint32_t)(MAO_SPARK_S * 1000.0f);
        mao_life_event(&s_life, &s_lark, LIFE_EV_FIRST_TOUCH, now);
    }
    s_last_input_ms = now;
    mao_lark_event(&s_lark, LARK_EV_INPUT, now);
    mao_life_event(&s_life, &s_lark, LIFE_EV_INPUT, now);
    const int sign = n > 0 ? 1 : -1;
    if (s_last_sign && sign != s_last_sign &&
        (now - s_last_detent_ms) < (uint32_t)(MAO_REV_WINDOW_S * 1000.0f)) {
        /* Reversal: momentum jerks the face on in the old direction while the
         * eyes (fast spring) already look the new way. Accumulates. */
        s_disturb += 1.0f;
        mao_lark_event(&s_lark, LARK_EV_REVERSAL, now);
        mao_life_event(&s_life, &s_lark, LIFE_EV_REVERSAL, now);
        mao_motion_kick(&s_m, CH_FACE_X, (float)s_last_sign * MAO_REV_KICK * (1.0f + s_disturb));
    }
    s_last_sign = sign;
    s_last_detent_ms = now;
    s_tick_detents += n;
    mao_life_dial(&s_life, n, now);
}

static void on_press(bool down, uint32_t now)
{
    if (!s_visible || !s_present) {
        return;
    }
    wake(now);
    if (down && (!s_last_input_ms || now - s_last_input_ms > (uint32_t)(MAO_SPARK_AFTER_S * 1000.0f))) {
        mao_life_event(&s_life, &s_lark, LIFE_EV_FIRST_TOUCH, now);
    }
    s_last_input_ms = now;
    mao_lark_event(&s_lark, LARK_EV_INPUT, now);
    mao_life_event(&s_life, &s_lark, LIFE_EV_INPUT, now);
    if (down) {
        mao_lark_event(&s_lark, LARK_EV_PRESS, now);
        mao_life_event(&s_life, &s_lark, LIFE_EV_TOUCH, now);
    }
    s_pressed = down;
    mao_motion_set(&s_m, CH_PRESS, down ? MAO_PRESS_DROP : 0.0f);
    mao_motion_set(&s_m, CH_SPREAD, down ? MAO_PRESS_SPREAD : 0.0f);
    mao_motion_set(&s_m, CH_SQUASH, down ? MAO_PRESS_SQUASH : 0.0f);
    if (!down) {
        /* Release is the satisfying part: a lift and a brief stretch. */
        mao_motion_kick(&s_m, CH_PRESS, MAO_RELEASE_KICK);
        mao_motion_kick(&s_m, CH_SQUASH, MAO_RELEASE_SQUASH_KICK);
        s_press_until = now + 300;
    }
}

static void react(mao_character_reaction_t r, uint32_t now)
{
    if (r == MAO_CHAR_REACT_WAKE) {
        wake(now);
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
            [MAO_CHAR_REACT_IDLE] = "neutral",
        };
        wake(now);
        s_fb_play_at = 0;
        switch (r) {
        case MAO_CHAR_REACT_DEVICE_ON:
            mao_life_event(&s_life, &s_lark, LIFE_EV_DEVICE_NEW, now);
            break;
        case MAO_CHAR_REACT_DEVICE_OFF:
            mao_life_event(&s_life, &s_lark, LIFE_EV_DEVICE_LOST, now);
            break;
        case MAO_CHAR_REACT_DONE:
            mao_life_event(&s_life, &s_lark, LIFE_EV_CMD_OK, now);
            break;
        case MAO_CHAR_REACT_FAIL:
            /* Analysis first: freeze and study for a beat, then the verdict. */
            mao_life_event(&s_life, &s_lark, LIFE_EV_CMD_FAIL, now);
            s_fb_play_at = now + 450;
            break;
        case MAO_CHAR_REACT_BUSY:
            mao_life_event(&s_life, &s_lark, LIFE_EV_CMD_BUSY, now);
            if (s_life.habit_busy > 0.65f) {
                /* The third "busy" in a row barely registers: a slight
                 * narrowing from the mind's evaluation, nothing more. */
                ESP_LOGI(TAG, "busy barely noted (habituated %.2f)", (double)s_life.habit_busy);
                return;
            }
            break;
        default:
            break;
        }
        s_fb_pending = mao_lark_find(kFb[r]);
        s_fb_hold = r == MAO_CHAR_REACT_BUSY;
        return;
    }
    if (!s_visible || !s_present) {
        return;
    }
    if (r == MAO_CHAR_REACT_WARM) {                       /* press priority (long press) */
        wake(now);
        mao_motion_set(&s_m, CH_NARROW, MAO_WARM_NARROW);
        mao_motion_set(&s_m, CH_FACE_Y, s_idle.base_y + MAO_WARM_LIFT);
        mao_motion_set(&s_m, CH_TINT_WARM, 1.0f);
        mao_motion_kick(&s_m, CH_FACE_Y, -18.0f);
        s_warm_until = now + (uint32_t)(MAO_WARM_S * 1000.0f);
        s_warm_tint_until = now + (uint32_t)(MAO_WARM_TINT_S * 1000.0f);
        mao_lark_event(&s_lark, LARK_EV_WARM, now);
        mao_life_event(&s_life, &s_lark, LIFE_EV_WARM, now);
        return;
    }
    /* System reactions yield to anything the user is doing. */
    if (current_prio(now) > PRIO_SYSTEM) {
        ESP_LOGD(TAG, "system reaction skipped (user interaction has priority)");
        return;
    }
    mao_idle_cancel(&s_idle, &s_m);
    wake(now);
    mao_motion_set(&s_m, CH_OPEN, MAO_NOTICE_OPEN);
    s_wide_until = now + (uint32_t)(MAO_NOTICE_S * 1000.0f);
    mao_motion_kick(&s_m, CH_FACE_Y, -30.0f);
    if (r == MAO_CHAR_REACT_ATTEND) {
        mao_motion_set(&s_m, CH_GAZE_X, 11.0f);
        mao_motion_set(&s_m, CH_GAZE_Y, -2.0f);
        mao_motion_set(&s_m, CH_FACE_X, s_idle.base_x + 7.0f);
        s_react_until = now + (uint32_t)(MAO_ATTEND_S * 1000.0f);
    } else {
        s_react_until = now + (uint32_t)(MAO_NOTICE_S * 1000.0f);
    }
}

static void appear(int dir, uint32_t now)
{
    s_visible = true;
    s_present = true;
    mao_motion_set(&s_m, CH_OPEN, 1.0f);
    if (dir != 0) {
        /* Arrive displaced towards the first turn, looking that way, then settle. */
        s_m.ch[CH_FACE_X].x = (float)dir * MAO_APPEAR_OFFSET;
        s_m.ch[CH_GAZE_X].x = (float)dir * MAO_GAZE_MAX;
        s_last_sign = dir;
        s_last_detent_ms = now;
    }
    mao_idle_schedule(&s_idle, now, false, true);
}

static void leave(uint32_t now)
{
    if (!s_visible) {
        return;
    }
    mao_idle_cancel(&s_idle, &s_m);
    /* 1. notice the double click: eyes widen, tiny "o". */
    mao_motion_set(&s_m, CH_OPEN, MAO_SURPRISE_OPEN);
    mao_motion_set(&s_m, CH_SPREAD, 2.0f);
    mao_motion_set(&s_m, CH_ORBIT_R, 0.0f);
    s_mouth = MAO_MOUTH_O;
    s_present = false;
    s_leave_drop_at = now + (uint32_t)(MAO_SURPRISE_S * 1000.0f);
    s_away_until = now + 600;
}

static void come_back(uint32_t now)
{
    s_present = true;
    s_mouth = MAO_MOUTH_NONE;
    s_leave_drop_at = 0;
    mao_motion_set(&s_m, CH_AWAY, 0.0f);
    mao_motion_set(&s_m, CH_CLOSE, 0.0f);
    mao_motion_set(&s_m, CH_SQUASH, s_pressed ? MAO_PRESS_SQUASH : 0.0f);
    mao_motion_set(&s_m, CH_SPREAD, 0.0f);
    mao_motion_set(&s_m, CH_OPEN, 1.0f);
    s_away_until = now + 350;
    mao_idle_schedule(&s_idle, now, s_sleepy, true);
    mao_lark_event(&s_lark, LARK_EV_RETURN, now);
}

static void set_sleepy(bool sleepy, uint32_t now)
{
    if (sleepy == s_sleepy) {
        return;
    }
    if (sleepy) {
        s_sleepy = true;
        mao_motion_profile(&s_m, CH_SLEEP, MAO_SLEEP_PROFILE);
        mao_motion_set(&s_m, CH_SLEEP, 1.0f);
        mao_idle_schedule(&s_idle, now, true, false);
    } else {
        wake(now);
    }
}

/* ---------------------------------------------------------------------- */
/* Dial physics (every tick)                                              */
/* ---------------------------------------------------------------------- */

static void dial_update(float dt, uint32_t now)
{
    const int32_t n = s_tick_detents;
    s_tick_detents = 0;

    const float rate = (float)n / dt;
    const float tau = fabsf(rate) > fabsf(s_speed) ? MAO_DIAL_TAU_UP : MAO_DIAL_TAU_DOWN;
    s_speed += (rate - s_speed) * (1.0f - expf(-dt / tau));
    const float abs_speed = fabsf(s_speed);
    s_accel += ((abs_speed - s_prev_abs) / dt - s_accel) * (1.0f - expf(-dt / 0.10f));
    s_prev_abs = abs_speed;
    s_disturb *= expf(-dt / MAO_REV_DECAY_S);

    const float i = clampf(abs_speed / MAO_DIAL_FULL_DPS, 0.0f, 1.0f);
    const float dir = (float)(s_last_sign ? s_last_sign : 1);
    const bool engaged = dial_engaged(now) || i > 0.05f;
    const float wob = clampf((s_disturb - MAO_REV_WOBBLE_START) * MAO_REV_WOBBLE_GAIN, 0.0f, MAO_REV_WOBBLE_MAX);
    mao_motion_set(&s_m, CH_WOBBLE, wob);
    if (s_disturb >= MAO_REV_DIZZY && !s_dizzy_noted) {
        s_dizzy_noted = true;   /* the dizzy expression plays once the dial stops */
        mao_lark_event(&s_lark, LARK_EV_DIZZY, now);
        mao_life_event(&s_life, &s_lark, LIFE_EV_DIZZY, now);
    } else if (s_disturb < 1.0f) {
        s_dizzy_noted = false;
    }

    if (!s_present) {
        return;
    }
    if (engaged) {
        const float s = smoothstep(0.0f, MAO_LATERAL_RAMP, i);
        const float o = smoothstep(MAO_ORBIT_START, MAO_ORBIT_FULL, i);
        const float lat = 1.0f - o;
        mao_motion_set(&s_m, CH_GAZE_X, dir * (MAO_GAZE_MIN + (MAO_GAZE_MAX - MAO_GAZE_MIN) * s) * lat
                                        + dir * MAO_GAZE_LAG_ORBIT * o);
        mao_motion_set(&s_m, CH_GAZE_Y, 0.0f);
        mao_motion_set(&s_m, CH_FACE_X, s_idle.base_x + dir * (MAO_FACE_MIN + (MAO_FACE_MAX - MAO_FACE_MIN) * s) * lat);
        const float lean = clampf(s_accel * MAO_LEAN_GAIN, -MAO_LEAN_MAX, MAO_LEAN_MAX);
        mao_motion_set(&s_m, CH_TILT, dir * (MAO_TILT_MAX * s * lat + lean));
        mao_motion_set(&s_m, CH_SQUASH, (s_pressed ? MAO_PRESS_SQUASH : 0.0f) - MAO_MOTION_STRETCH * i);
        mao_motion_set(&s_m, CH_TINT_MOVE, o);
        if (!before(now, s_wide_until)) {
            mao_motion_set(&s_m, CH_OPEN, 1.0f - 0.06f * wob);
        }

        if (o > 0.01f) {
            if (!s_orbiting) {
                s_orbiting = true;
                if (s_m.ch[CH_ORBIT_R].x < 2.0f) {
                    /* Start on the side we are turning towards. */
                    s_orbit_target = dir * PI_F / 2.0f;
                    s_m.ch[CH_ORBIT_A].x = s_orbit_target;
                    s_m.ch[CH_ORBIT_A].v = 0.0f;
                } else {
                    s_orbit_target = s_m.ch[CH_ORBIT_A].x;
                }
            }
            /* Follow the knob's real angle (the spring adds a slight lag). */
            s_orbit_target += (float)n * TWO_PI_F / (float)s_detents_per_rev;
            mao_motion_set(&s_m, CH_ORBIT_A, s_orbit_target);
            if (o > 0.5f) {
                mao_lark_event(&s_lark, LARK_EV_FAST, now);
            }
            mao_motion_set(&s_m, CH_ORBIT_R, o * MAO_ORBIT_RADIUS +
                           smoothstep(MAO_ORBIT_RIM_START, 1.0f, i) * MAO_ORBIT_RIM_EXTRA);
        } else {
            mao_motion_set(&s_m, CH_ORBIT_R, 0.0f);
        }
        return;
    }

    /* Disengaged: the face settles first, the eyes follow a moment later. */
    if (s_last_detent_ms && (now - s_last_detent_ms) < 5000) {
        mao_motion_set(&s_m, CH_FACE_X, s_idle.base_x);
        mao_motion_set(&s_m, CH_TILT, 0.0f);
        mao_motion_set(&s_m, CH_ORBIT_R, 0.0f);
        mao_motion_set(&s_m, CH_TINT_MOVE, 0.0f);
        mao_motion_set(&s_m, CH_SQUASH, s_pressed ? MAO_PRESS_SQUASH : 0.0f);
        if ((now - s_last_detent_ms) > (uint32_t)((MAO_DIAL_ENGAGE_S + MAO_GAZE_HOLD_S) * 1000.0f) &&
            !s_idle.glance_until && !before(now, s_react_until)) {
            mao_motion_set(&s_m, CH_GAZE_X, 0.0f);
        }
        if (s_orbiting && s_m.ch[CH_ORBIT_R].x < 1.0f) {
            s_orbiting = false;
            /* Keep the angle bounded once the orbit has collapsed. */
            const float wraps = TWO_PI_F * floorf(s_orbit_target / TWO_PI_F);
            s_orbit_target -= wraps;
            s_m.ch[CH_ORBIT_A].x -= wraps;
            mao_motion_set(&s_m, CH_ORBIT_A, s_orbit_target);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Tick                                                                   */
/* ---------------------------------------------------------------------- */

static void preview(mao_character_preview_t p, uint32_t now);

static void apply(const cmd_t *c, uint32_t now)
{
    switch (c->type) {
    case CMD_DIAL:       on_dial(c->value, now); break;
    case CMD_PRESS:      on_press(c->flag, now); break;
    case CMD_REACT:      react((mao_character_reaction_t)c->arg, now); break;
    case CMD_APPEAR:     appear(c->arg, now); break;
    case CMD_SLEEPY:     set_sleepy(c->flag, now); break;
    case CMD_LEAVE:      leave(now); break;
    case CMD_RETURN:     come_back(now); break;
    case CMD_PREVIEW:    preview((mao_character_preview_t)c->arg, now); break;
    case CMD_DEBUG_DIAL: s_dbg_dps = c->f; s_dbg_acc = 0.0f; s_dbg_dial_until = now + (uint32_t)c->value; break;
    case CMD_LOOK:       s_m.look = kLooks[c->arg]; ESP_LOGI(TAG, "look '%s'", kLooks[c->arg].name); break;
    case CMD_EXPRESSION: mao_lark_switch(&s_lark, c->arg, now); break;
    case CMD_TRANSFER: {
        const int dx = c->value / 10, dy = c->value % 10;
        if (c->arg == (int8_t)MAO_TR_NONE) {
            mao_transfer_begin(&s_transfer, &s_m, MAO_TR_NONE, 0, 0, now);
            mao_idle_schedule(&s_idle, now, s_sleepy, true);
        } else {
            if (!s_visible) {
                appear(0, now);
            }
            wake(now);
            s_fb_pending = -1;   /* a transfer outranks queued feedback */
            mao_transfer_begin(&s_transfer, &s_m, (uint8_t)c->arg, dx, dy, now);
        }
        break;
    }
    case CMD_MIND:
        if (c->arg == 0) {
            mao_life_debug_interest(&s_life, (uint8_t)c->value);
        } else {
            mao_life_debug_novelty(&s_life, (uint8_t)c->value);
        }
        break;
    default: break;
    }
}

static void timed_reactions(uint32_t now)
{
    if (s_wide_until && !before(now, s_wide_until)) {
        s_wide_until = 0;
        mao_motion_set(&s_m, CH_OPEN, 1.0f);
    }
    if (s_react_until && !before(now, s_react_until)) {
        s_react_until = 0;
        mao_motion_set(&s_m, CH_GAZE_X, 0.0f);
        mao_motion_set(&s_m, CH_GAZE_Y, 0.0f);
        mao_motion_set(&s_m, CH_FACE_X, s_idle.base_x);
    }
    if (s_warm_tint_until && !before(now, s_warm_tint_until)) {
        s_warm_tint_until = 0;
        mao_motion_set(&s_m, CH_TINT_WARM, 0.0f);
    }
    if (s_warm_until && !before(now, s_warm_until)) {
        s_warm_until = 0;
        mao_motion_set(&s_m, CH_NARROW, MAO_REST_NARROW);
        mao_motion_set(&s_m, CH_FACE_Y, s_idle.base_y);
    }
    if (s_leave_drop_at && !before(now, s_leave_drop_at) && !s_present) {
        /* 2. drop and fold out through the bottom of the circle. */
        s_leave_drop_at = 0;
        s_mouth = MAO_MOUTH_NONE;
        mao_motion_set(&s_m, CH_AWAY, MAO_LEAVE_Y);
        mao_motion_set(&s_m, CH_SQUASH, MAO_LEAVE_SQUASH);
        mao_motion_set(&s_m, CH_OPEN, MAO_LEAVE_OPEN);
        mao_motion_set(&s_m, CH_CLOSE, 1.0f);   /* pupil looks: crescents as it drops */
        mao_motion_set(&s_m, CH_SPREAD, 0.0f);
    }
    if (s_dbg_release_at && !before(now, s_dbg_release_at)) {
        s_dbg_release_at = 0;
        on_press(false, now);
    }
    if (s_dbg_return_at && !before(now, s_dbg_return_at)) {
        s_dbg_return_at = 0;
        come_back(now);
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    const uint32_t now = lv_tick_get();
    float dt = (float)(now - s_last_tick_ms) / 1000.0f;
    s_last_tick_ms = now;
    dt = clampf(dt, 0.005f, 0.05f);   /* after a stall, don't let springs jump */

    cmd_t c;
    while (xQueueReceive(s_cmds, &c, 0) == pdTRUE) {
        apply(&c, now);
    }
    if (before(now, s_dbg_dial_until)) {
        s_dbg_acc += s_dbg_dps * dt;
        const int32_t n = (int32_t)s_dbg_acc;
        if (n) {
            s_dbg_acc -= (float)n;
            on_dial(n, now);
        }
    }

    dial_update(dt, now);
    timed_reactions(now);
    mao_transfer_tick(&s_transfer, &s_m, &s_draw, now);
    if (s_visible && current_prio(now) == PRIO_IDLE) {
        /* Idle behaviour now comes from the mind (mao_life.c). */
    } else if (current_prio(now) != PRIO_IDLE) {
        /* Something more important is happening: push idle back. */
        mao_idle_schedule(&s_idle, now, s_sleepy, true);
    }
    update_state(now);

    /* Expression layer: full in idle, faint while the user is in charge. */
    if (s_fb_pending >= 0 && s_visible && s_present && !before(now, s_away_until) &&
        s_transfer.phase == MAO_TR_NONE && !before(now, s_fb_play_at)) {
        const int st = s_fb_pending;
        s_fb_pending = -1;
        s_lark.pending = -1;           /* controller feedback wins over a queued mood */
        mao_lark_switch(&s_lark, st, now);
        const lark_state_t *ls = mao_lark_state(st);
        s_fb_until = now + ((ls->flags & LARK_ONESHOT) ? (uint32_t)ls->length_ms + 300u : 0u);
    }
    if (s_fb_hold && s_lark.cur != mao_lark_find("busy")) {
        s_fb_hold = false;             /* busy ended (done / fail / idle / input) */
    }
    static const float kLayerGain[] = { [PRIO_IDLE] = 1.0f, [PRIO_SYSTEM] = 0.5f, [PRIO_DIAL] = 0.25f,
                                        [PRIO_PRESS] = 0.45f, [PRIO_NAV] = 0.0f };
    const prio_t prio = current_prio(now);
    const bool feedback = s_fb_hold || before(now, s_fb_until);
    mao_lark_update(&s_lark, now, s_visible && prio == PRIO_IDLE, s_sleepy,
                    feedback ? 1.0f : kLayerGain[prio], s_m.layer);
    float life[CH_COUNT] = { 0 };
    mao_life_update(&s_life, &s_lark, &s_m, now, s_visible && prio == PRIO_IDLE, s_sleepy, life);
    if (prio != PRIO_NAV) {
        for (int i = 0; i < CH_COUNT; i++) {
            s_m.layer[i] += life[i];
        }
    }

    mao_motion_step(&s_m, dt, now);
    if (s_visible) {
        mao_pose_t pose;
        mao_motion_pose(&s_m, s_mouth, now, &pose);
        mao_char_draw_apply(&s_draw, &pose);
        mao_char_draw_star(&s_draw, &pose);
    }
}

/* ---------------------------------------------------------------------- */
/* Development previews                                                   */
/* ---------------------------------------------------------------------- */

static void preview(mao_character_preview_t p, uint32_t now)
{
    ESP_LOGI(TAG, "preview '%s'", mao_character_preview_name(p));
    if (p == MAO_CHAR_PREVIEW_HIDE) {
        s_visible = false;
        s_m.ch[CH_OPEN].x = 0.0f;
        s_m.ch[CH_OPEN].v = 0.0f;
        mao_motion_set(&s_m, CH_OPEN, 0.0f);
        mao_char_draw_hide(&s_draw);
        return;
    }
    if (!s_visible) {
        appear(0, now);
    }
    switch (p) {
    case MAO_CHAR_PREVIEW_IDLE:
        s_disturb = 0.0f;
        s_dbg_dial_until = 0;
        wake(now);
        break;
    case MAO_CHAR_PREVIEW_BLINK:     mao_motion_blink(&s_m, now, 150, 1); break;
    case MAO_CHAR_PREVIEW_FOLLOW:    s_dbg_dps = 6.0f;  s_dbg_dial_until = now + 2500; break;
    case MAO_CHAR_PREVIEW_FAST:      s_dbg_dps = 35.0f; s_dbg_dial_until = now + 3000; break;
    case MAO_CHAR_PREVIEW_VERY_FAST: s_dbg_dps = 75.0f; s_dbg_dial_until = now + 3000; break;
    case MAO_CHAR_PREVIEW_DIZZY:
        for (int k = 0; k < 4; k++) {
            on_dial(k % 2 ? 1 : -1, now);
        }
        break;
    case MAO_CHAR_PREVIEW_PRESS:     on_press(true, now); s_dbg_release_at = now + 700; break;
    case MAO_CHAR_PREVIEW_WARM:      react(MAO_CHAR_REACT_WARM, now); break;
    case MAO_CHAR_PREVIEW_SLEEPY:    set_sleepy(!s_sleepy, now); break;
    case MAO_CHAR_PREVIEW_LEAVE:     leave(now); s_dbg_return_at = now + 1200; break;
    default: break;
    }
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

esp_err_t mao_character_create(lv_obj_t *parent)
{
    s_cmds = xQueueCreate(CMD_QUEUE_LEN, sizeof(cmd_t));
    if (!s_cmds) {
        return ESP_ERR_NO_MEM;
    }
    mao_motion_init(&s_m, &kLooks[MAO_LOOK_DEFAULT]);
    mao_lark_init(&s_lark, lv_tick_get());
    mao_life_init(&s_life, lv_tick_get());
    esp_err_t err = mao_char_draw_create(&s_draw, parent);
    if (err != ESP_OK) {
        return err;
    }
    s_last_tick_ms = lv_tick_get();
    mao_idle_schedule(&s_idle, s_last_tick_ms, false, true);
    if (!lv_timer_create(tick_cb, TICK_MS, NULL)) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "character ready: look '%s', %d ms motion tick", kLooks[MAO_LOOK_DEFAULT].name, TICK_MS);
    return ESP_OK;
}

static void post(cmd_t c)
{
    if (s_cmds) {
        xQueueSend(s_cmds, &c, 0);
    }
}

void mao_character_set_detents_per_rev(uint8_t n)
{
    if (n > 0) {
        s_detents_per_rev = n;
    }
}

void mao_character_dial(int32_t detents)        { post((cmd_t){ .type = CMD_DIAL, .value = detents }); }
void mao_character_press(bool down)             { post((cmd_t){ .type = CMD_PRESS, .flag = down }); }
void mao_character_react(mao_character_reaction_t r) { post((cmd_t){ .type = CMD_REACT, .arg = (int8_t)r }); }
void mao_character_appear(int direction)        { post((cmd_t){ .type = CMD_APPEAR, .arg = (int8_t)direction }); }
void mao_character_set_sleepy(bool sleepy)      { post((cmd_t){ .type = CMD_SLEEPY, .flag = sleepy }); }
void mao_character_leave(void)                  { post((cmd_t){ .type = CMD_LEAVE }); }
void mao_character_return(void)                 { post((cmd_t){ .type = CMD_RETURN }); }

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
        "notice", "attend", "warm", "wake", "ack", "busy", "done", "fail", "back", "device_on", "device_off", "idle",
    };
    return r < MAO_CHAR_REACT_COUNT ? kNames[r] : "?";
}
