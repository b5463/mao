/*
 * MAO character: reaction-state machine, idle behaviour and command intake.
 *
 * Commands arrive from any task through a small queue and are applied on the
 * character tick (an LVGL timer, so drawing needs no extra locking). States
 * are presentation states with timeouts that settle back to IDLE (or SLEEPY).
 */
#include "mao_character.h"
#include "mao_character_priv.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "MAO_CHARACTER";

#define TICK_MS            33        /* ~30 Hz motion update */
#define CMD_QUEUE_LEN      16
#define PI_F               3.14159265f
#define TWO_PI_F           6.28318531f

#define FOLLOW_HOLD_MS     650       /* dial reaction lingers this long after the last detent */
#define DIZZY_MS           1600
#define NOTICE_MS          400
#define SURPRISED_MS       750
#define HAPPY_MS           900
#define WIDE_HOLD_MS       300

#define SLEEPY_OPEN        0.32f
#define AWAY_OFFSET        175.0f    /* below the round screen */

typedef enum {
    CMD_DIAL,
    CMD_PRESS,
    CMD_REACT,
    CMD_SLEEPY,
    CMD_PRESENT,
    CMD_STRESS,
} cmd_type_t;

typedef struct {
    uint8_t type;
    uint8_t arg;       /* speed / reaction */
    bool flag;
    int32_t value;
} cmd_t;

static QueueHandle_t s_cmds;
static mao_motion_t s_m;
static mao_char_draw_t s_draw;
static uint32_t s_last_tick_ms;

static volatile mao_character_state_t s_state = MAO_CHAR_IDLE;
static uint32_t s_state_until;     /* 0 = no timeout */
static bool s_visible;             /* has appeared at least once */
static bool s_present = true;      /* false while moved away (menu) */
static bool s_sleepy;
static bool s_pressed;
static mao_mouth_t s_mouth = MAO_MOUTH_NONE;

static uint32_t s_next_idle_ms;
static uint32_t s_glance_until;
static uint32_t s_wide_until;
static float s_base_x, s_base_y;   /* idle resting offset */
static bool s_orbiting;
static float s_orbit_target;
static uint32_t s_stress_until;
static volatile uint8_t s_detents_per_rev = 30;

void mao_character_set_detents_per_rev(uint8_t detents_per_rev)
{
    if (detents_per_rev > 0) {
        s_detents_per_rev = detents_per_rev;
    }
}

static const char *const kStateNames[MAO_CHAR_STATE_COUNT] = {
    "IDLE", "NOTICE", "FOLLOW", "DIZZY", "SLEEPY", "SURPRISED", "HAPPY",
};

const char *mao_character_state_name(mao_character_state_t state)
{
    return state < MAO_CHAR_STATE_COUNT ? kStateNames[state] : "?";
}

mao_character_state_t mao_character_get_state(void)
{
    return s_state;
}

static uint32_t rnd(uint32_t n)
{
    return n ? esp_random() % n : 0;
}

static void set_state(mao_character_state_t st, uint32_t now, uint32_t duration_ms)
{
    if (st != s_state) {
        ESP_LOGI(TAG, "%s -> %s", kStateNames[s_state], kStateNames[st]);
        s_state = st;
    }
    s_state_until = duration_ms ? now + duration_ms : 0;
}

static void schedule_idle(uint32_t now)
{
    s_next_idle_ms = now + (s_sleepy ? 3500 + rnd(5000) : 1800 + rnd(3800));
}

/* Resting pose for the current (idle / sleepy / pressed) situation. */
static void settle_targets(void)
{
    mao_motion_set(&s_m, CH_FACE_X, s_base_x);
    mao_motion_set(&s_m, CH_FACE_Y, s_base_y + (s_sleepy ? 8.0f : 0.0f));
    mao_motion_set(&s_m, CH_GAZE_X, 0.0f);
    mao_motion_set(&s_m, CH_GAZE_Y, s_sleepy ? 3.0f : 0.0f);
    mao_motion_set(&s_m, CH_OPEN, s_sleepy ? SLEEPY_OPEN : 1.0f);
    mao_motion_set(&s_m, CH_SQUASH, s_pressed ? 0.55f : 0.0f);
    mao_motion_set(&s_m, CH_TILT, 0.0f);
    mao_motion_set(&s_m, CH_HAPPY, 0.0f);
    mao_motion_set(&s_m, CH_ORBIT_R, 0.0f);
    mao_motion_set(&s_m, CH_WOBBLE, 0.0f);
    s_mouth = MAO_MOUTH_NONE;
    s_orbiting = false;
    s_m.breathing = s_sleepy;
}

static void wake_if_sleepy(uint32_t now)
{
    if (s_sleepy) {
        s_sleepy = false;
        s_m.breathing = false;
        settle_targets();
        set_state(MAO_CHAR_IDLE, now, 0);
    }
}

/* ---------------------------------------------------------------------- */
/* Command handlers (LVGL task)                                           */
/* ---------------------------------------------------------------------- */

static void on_dial(int32_t detents, mao_dial_speed_t speed, bool reversing, uint32_t now)
{
    if (!s_visible || !s_present || detents == 0) {
        return;
    }
    wake_if_sleepy(now);
    const float dir = detents > 0 ? 1.0f : -1.0f;

    if (reversing) {
        settle_targets();
        mao_motion_set(&s_m, CH_WOBBLE, 4.5f);
        mao_motion_set(&s_m, CH_OPEN, 0.72f);
        s_mouth = MAO_MOUTH_FLAT;
        set_state(MAO_CHAR_DIZZY, now, DIZZY_MS);
        return;
    }
    if (s_state == MAO_CHAR_DIZZY) {
        return;   /* let the dizziness play out */
    }

    s_mouth = MAO_MOUTH_NONE;
    mao_motion_set(&s_m, CH_WOBBLE, 0.0f);
    mao_motion_set(&s_m, CH_HAPPY, 0.0f);
    set_state(MAO_CHAR_FOLLOW, now, FOLLOW_HOLD_MS);

    switch (speed) {
    case MAO_DIAL_STILL:
    case MAO_DIAL_SLOW:
        /* Eyes follow the dial. */
        s_orbiting = false;
        mao_motion_set(&s_m, CH_ORBIT_R, 0.0f);
        mao_motion_set(&s_m, CH_FACE_X, s_base_x + dir * 3.0f);
        mao_motion_set(&s_m, CH_GAZE_X, dir * 7.0f);
        mao_motion_set(&s_m, CH_TILT, 0.0f);
        mao_motion_set(&s_m, CH_OPEN, 1.0f);
        break;
    case MAO_DIAL_NORMAL:
        /* Lean into the turn. */
        s_orbiting = false;
        mao_motion_set(&s_m, CH_ORBIT_R, 0.0f);
        mao_motion_set(&s_m, CH_FACE_X, s_base_x + dir * 12.0f);
        mao_motion_set(&s_m, CH_GAZE_X, dir * 9.0f);
        mao_motion_set(&s_m, CH_TILT, dir * 2.5f);
        mao_motion_set(&s_m, CH_OPEN, 1.0f);
        break;
    case MAO_DIAL_FAST:
    case MAO_DIAL_VERY_FAST: {
        /* Pulled around the rim, following the knob's actual angle. */
        if (!s_orbiting) {
            s_orbiting = true;
            if (s_m.ch[CH_ORBIT_R].x < 3.0f) {
                /* Start on the side the dial is turning towards. */
                const float start = dir > 0 ? PI_F / 2.0f : -PI_F / 2.0f;
                s_m.ch[CH_ORBIT_A].x = start;
                s_m.ch[CH_ORBIT_A].v = 0.0f;
                s_orbit_target = start;
            } else {
                s_orbit_target = s_m.ch[CH_ORBIT_A].x;
            }
        }
        s_orbit_target += (float)detents * (TWO_PI_F / (float)s_detents_per_rev);
        mao_motion_set(&s_m, CH_ORBIT_A, s_orbit_target);
        const bool very = speed == MAO_DIAL_VERY_FAST;
        mao_motion_set(&s_m, CH_ORBIT_R, very ? 42.0f : 26.0f);
        mao_motion_set(&s_m, CH_FACE_X, 0.0f);
        mao_motion_set(&s_m, CH_GAZE_X, dir * (very ? -4.0f : 6.0f));   /* very fast: eyes lag */
        mao_motion_set(&s_m, CH_TILT, 0.0f);
        mao_motion_set(&s_m, CH_OPEN, very ? 0.85f : 1.0f);
        break;
    }
    }
}

static void on_press(bool down, uint32_t now)
{
    if (!s_visible || !s_present) {
        return;
    }
    wake_if_sleepy(now);
    s_pressed = down;
    mao_motion_set(&s_m, CH_SQUASH, down ? 0.55f : 0.0f);
    if (down) {
        set_state(MAO_CHAR_NOTICE, now, 0);          /* hold while pressed */
    } else {
        mao_motion_kick(&s_m, CH_FACE_Y, -55.0f);    /* small bounce on release */
        set_state(MAO_CHAR_NOTICE, now, NOTICE_MS);
    }
}

static void on_react(mao_character_reaction_t r, uint32_t now)
{
    if (r == MAO_CHAR_REACT_WAKE) {
        s_visible = true;
        s_sleepy = false;
        s_m.breathing = false;
        settle_targets();
        mao_motion_kick(&s_m, CH_FACE_Y, -35.0f);
        set_state(MAO_CHAR_NOTICE, now, NOTICE_MS + 100);
        return;
    }
    if (!s_visible || !s_present) {
        return;
    }
    wake_if_sleepy(now);
    switch (r) {
    case MAO_CHAR_REACT_NOTICE:
        mao_motion_set(&s_m, CH_OPEN, 1.18f);
        s_wide_until = now + WIDE_HOLD_MS;
        mao_motion_kick(&s_m, CH_FACE_Y, -40.0f);
        set_state(MAO_CHAR_NOTICE, now, NOTICE_MS);
        break;
    case MAO_CHAR_REACT_SURPRISED:
        mao_motion_set(&s_m, CH_OPEN, 1.35f);
        mao_motion_set(&s_m, CH_SQUASH, 0.0f);
        mao_motion_kick(&s_m, CH_FACE_Y, -60.0f);
        s_mouth = MAO_MOUTH_O;
        set_state(MAO_CHAR_SURPRISED, now, SURPRISED_MS);
        break;
    case MAO_CHAR_REACT_ATTEND:
        /* Notice + a glance to the rim, as if towards the new arrival. */
        mao_motion_set(&s_m, CH_OPEN, 1.15f);
        s_wide_until = now + WIDE_HOLD_MS;
        mao_motion_set(&s_m, CH_GAZE_X, 11.0f);
        mao_motion_set(&s_m, CH_GAZE_Y, -2.0f);
        mao_motion_set(&s_m, CH_FACE_X, s_base_x + 7.0f);
        s_glance_until = now + 1000;
        mao_motion_kick(&s_m, CH_FACE_Y, -30.0f);
        set_state(MAO_CHAR_NOTICE, now, 1000);
        break;
    case MAO_CHAR_REACT_HAPPY:
        mao_motion_set(&s_m, CH_HAPPY, 1.0f);
        mao_motion_kick(&s_m, CH_FACE_Y, -30.0f);
        set_state(MAO_CHAR_HAPPY, now, HAPPY_MS);
        break;
    default:
        break;
    }
}

static void on_sleepy(bool sleepy, uint32_t now)
{
    if (sleepy == s_sleepy) {
        return;
    }
    s_sleepy = sleepy;
    settle_targets();
    set_state(sleepy ? MAO_CHAR_SLEEPY : MAO_CHAR_IDLE, now, 0);
    schedule_idle(now);
}

static void on_present(bool present, uint32_t now)
{
    s_present = present;
    if (present) {
        s_visible = true;
    } else {
        settle_targets();
        set_state(s_sleepy ? MAO_CHAR_SLEEPY : MAO_CHAR_IDLE, now, 0);
    }
    mao_motion_set(&s_m, CH_AWAY_Y, present ? 0.0f : AWAY_OFFSET);
}

/* ---------------------------------------------------------------------- */
/* Idle behaviour                                                         */
/* ---------------------------------------------------------------------- */

static void run_idle_event(uint32_t now)
{
    const uint32_t r = rnd(100);
    if (s_sleepy) {
        if (r < 70) {
            mao_motion_blink(&s_m, now, 520, 1);     /* slow, heavy blink */
        } else {
            mao_motion_kick(&s_m, CH_FACE_Y, 18.0f); /* small nod */
        }
        return;
    }
    if (r < 40) {
        mao_motion_blink(&s_m, now, 150, 1);
    } else if (r < 50) {
        mao_motion_blink(&s_m, now, 150, 2);
    } else if (r < 75) {
        static const int8_t kGlance[][2] = { {-8, 0}, {8, 0}, {-6, -5}, {6, -5}, {0, 5}, {-5, 4}, {5, 4} };
        const uint32_t i = rnd(sizeof(kGlance) / sizeof(kGlance[0]));
        mao_motion_set(&s_m, CH_GAZE_X, kGlance[i][0]);
        mao_motion_set(&s_m, CH_GAZE_Y, kGlance[i][1]);
        s_glance_until = now + 700 + rnd(700);
    } else if (r < 87) {
        s_base_x = (float)((int32_t)rnd(13) - 6);
        s_base_y = (float)((int32_t)rnd(7) - 3);
        mao_motion_set(&s_m, CH_FACE_X, s_base_x);
        mao_motion_set(&s_m, CH_FACE_Y, s_base_y);
    } else if (r < 94) {
        const float side = rnd(2) ? 1.0f : -1.0f;    /* look at the edge of the screen */
        mao_motion_set(&s_m, CH_GAZE_X, 11.0f * side);
        mao_motion_set(&s_m, CH_GAZE_Y, -2.0f);
        mao_motion_set(&s_m, CH_FACE_X, s_base_x + 8.0f * side);
        s_glance_until = now + 900;
    } else {
        mao_motion_kick(&s_m, CH_FACE_Y, -28.0f);    /* tiny hop */
    }
}

/* ---------------------------------------------------------------------- */
/* Tick                                                                   */
/* ---------------------------------------------------------------------- */

static void apply_command(const cmd_t *c, uint32_t now)
{
    switch (c->type) {
    case CMD_DIAL:    on_dial(c->value, (mao_dial_speed_t)c->arg, c->flag, now); break;
    case CMD_PRESS:   on_press(c->flag, now); break;
    case CMD_REACT:   on_react((mao_character_reaction_t)c->arg, now); break;
    case CMD_SLEEPY:  on_sleepy(c->flag, now); break;
    case CMD_PRESENT: on_present(c->flag, now); break;
    case CMD_STRESS:  s_stress_until = now + (uint32_t)c->value; break;
    default: break;
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    const uint32_t now = lv_tick_get();
    float dt = (float)(now - s_last_tick_ms) / 1000.0f;
    s_last_tick_ms = now;
    if (dt > 0.05f) {
        dt = 0.05f;   /* after a stall, don't let the springs jump */
    }

    cmd_t c;
    while (xQueueReceive(s_cmds, &c, 0) == pdTRUE) {
        apply_command(&c, now);
    }

    if ((int32_t)(s_stress_until - now) > 0 && s_visible && s_present) {
        if (!s_orbiting) {
            s_orbiting = true;
            s_orbit_target = s_m.ch[CH_ORBIT_A].x;
        }
        s_orbit_target += 0.22f;
        mao_motion_set(&s_m, CH_ORBIT_A, s_orbit_target);
        mao_motion_set(&s_m, CH_ORBIT_R, 40.0f);
        set_state(MAO_CHAR_FOLLOW, now, 300);
    }

    if (s_wide_until && (int32_t)(now - s_wide_until) >= 0) {
        s_wide_until = 0;
        mao_motion_set(&s_m, CH_OPEN, s_sleepy ? SLEEPY_OPEN : 1.0f);
    }

    if (s_state_until && (int32_t)(now - s_state_until) >= 0) {
        s_state_until = 0;
        settle_targets();
        set_state(s_sleepy ? MAO_CHAR_SLEEPY : MAO_CHAR_IDLE, now, 0);
        schedule_idle(now);
    }

    if (s_glance_until && (int32_t)(now - s_glance_until) >= 0) {
        s_glance_until = 0;
        mao_motion_set(&s_m, CH_GAZE_X, 0.0f);
        mao_motion_set(&s_m, CH_GAZE_Y, s_sleepy ? 3.0f : 0.0f);
        mao_motion_set(&s_m, CH_FACE_X, s_base_x);
    }

    if ((s_state == MAO_CHAR_IDLE || s_state == MAO_CHAR_SLEEPY) && s_visible && s_present &&
        (int32_t)(now - s_next_idle_ms) >= 0) {
        run_idle_event(now);
        schedule_idle(now);
    }

    /* Keep the orbit angle bounded once the orbit has collapsed. */
    if (!s_orbiting && s_m.ch[CH_ORBIT_R].x < 0.5f && fabsf(s_orbit_target) > TWO_PI_F) {
        const float wraps = TWO_PI_F * floorf(s_orbit_target / TWO_PI_F);
        s_orbit_target -= wraps;
        s_m.ch[CH_ORBIT_A].x -= wraps;
        mao_motion_set(&s_m, CH_ORBIT_A, s_orbit_target);
    }

    mao_motion_step(&s_m, dt, now);
    if (s_visible) {
        mao_pose_t pose;
        mao_motion_pose(&s_m, s_mouth, &pose);
        mao_char_draw_apply(&s_draw, &pose);
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
    mao_motion_init(&s_m);
    esp_err_t err = mao_char_draw_create(&s_draw, parent);
    if (err != ESP_OK) {
        return err;
    }
    s_last_tick_ms = lv_tick_get();
    schedule_idle(s_last_tick_ms);
    if (!lv_timer_create(tick_cb, TICK_MS, NULL)) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "character ready (procedural eyes, %d ms motion tick)", TICK_MS);
    return ESP_OK;
}

static void post(cmd_t c)
{
    if (s_cmds) {
        xQueueSend(s_cmds, &c, 0);
    }
}

void mao_character_dial(int32_t detents, mao_dial_speed_t speed, bool reversing)
{
    post((cmd_t) { .type = CMD_DIAL, .arg = (uint8_t)speed, .flag = reversing, .value = detents });
}

void mao_character_press(bool down)
{
    post((cmd_t) { .type = CMD_PRESS, .flag = down });
}

void mao_character_react(mao_character_reaction_t reaction)
{
    post((cmd_t) { .type = CMD_REACT, .arg = (uint8_t)reaction });
}

void mao_character_set_sleepy(bool sleepy)
{
    post((cmd_t) { .type = CMD_SLEEPY, .flag = sleepy });
}

void mao_character_set_present(bool present)
{
    post((cmd_t) { .type = CMD_PRESENT, .flag = present });
}

void mao_character_debug_stress(uint32_t duration_ms)
{
    post((cmd_t) { .type = CMD_STRESS, .value = (int32_t)duration_ms });
}
