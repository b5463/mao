/*
 * MAO application: what MAO does about what it noticed (boards with
 * sensors: the A1's IMU and ToF; perception does not run on the LCDkit).
 *
 * Ported from the A0 onto the M4.1 character. Percepts arrive filtered and
 * fused (mao_perception); this is the cause / state side, decided with
 * context, not by a table:
 *   - only HOME shows reactions; menus and device views stay undisturbed,
 *     and the first encounter ignores percepts;
 *   - controller first: while the knob is in use (input in the last
 *     CONTROL_QUIET_US) HOME's gaze belongs to the light scale, so a percept
 *     never moves the eyes then; feedback about the controller always wins;
 *   - habituation: each percept has an interest that a reaction spends and
 *     that recovers over a minute; a small look covers the gap;
 *   - a bigger reaction is not trampled by small ones for BIG_REACTION_US.
 * The vocabulary is the M4.1 one: NOTICE, ATTEND and look(x, y). MAO never
 * shows a "happy" state and never turns yellow; there is no DIZZY reaction
 * (being shaken or dropped is a NOTICE and a haptic tremor). Fiddling is
 * mao_fiddle's alone (HOME's light): FIDDLING_ESCALATION never arrives.
 */
#include "mao_app_priv.h"

#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mao_audio.h"
#include "mao_character.h"
#include "mao_haptics.h"
#include "mao_percept.h"

static const char *TAG = "MAO_APP";

#define INTEREST_RECOVER_S    60.0f    /* habituation recovers with this time constant */
#define INTEREST_SPEND        0.55f    /* a reaction leaves this much interest */
#define INTEREST_PASSIVE      0.85f    /* noticed but not reacted to */
#define BIG_REACTION_US       (1200 * 1000)
#define CONTROL_QUIET_US      (2500 * 1000)   /* the knob was used this recently: hands off the gaze */
/* Gaze targets, screen px from the centre (+ = right / down). */
#define LOOK_UP_PX            (-60)
#define LOOK_DOWN_PX          50
#define LOOK_SIDE_PX          70

typedef struct {
    float interest;
    int64_t last_us;
} habit_t;

static habit_t s_habit[MAO_PERCEPT_COUNT];
static int64_t s_busy_until_us;     /* a bigger reaction is playing */
static bool s_upside_down;

void mao_app_percept_init(void)
{
    for (int i = 0; i < MAO_PERCEPT_COUNT; i++) {
        s_habit[i].interest = 1.0f;
    }
}

static uint32_t chance(void)
{
    return esp_random() % 100;
}

/* Is this still interesting enough (scaled by how sure perception is)?
 * Reacting spends interest; it comes back over a minute. */
static bool novel(mao_percept_t p, uint8_t confidence, int64_t now, float threshold)
{
    habit_t *h = &s_habit[p];
    if (h->last_us) {
        const float dt = (float)(now - h->last_us) / 1e6f;
        h->interest = 1.0f - (1.0f - h->interest) * expf(-dt / INTEREST_RECOVER_S);
    }
    h->last_us = now;
    const bool ok = h->interest * ((float)confidence / 100.0f) >= threshold;
    h->interest *= ok ? INTEREST_SPEND : INTEREST_PASSIVE;
    return ok;
}

static bool busy(int64_t now)
{
    return now < s_busy_until_us;
}

static void big(int64_t now)
{
    s_busy_until_us = now + BIG_REACTION_US;
}

/* The knob is in use: the face reports the controller, not the room. */
static bool controlling(int64_t now)
{
    return now - mao_state()->last_input_us < CONTROL_QUIET_US;
}

static void look(int x, int y)
{
    mao_character_look(x, y, true);   /* lets go on its own ~2.5 s after */
}

/* Someone is here: counts as activity, and MAO wakes for it (as after an
 * input; a resting screen comes back). */
static void company(int64_t now)
{
    mao_app_power_input();
    mao_app_wake_now(now);
}

void mao_app_on_percept(const mao_percept_msg_t *m, int64_t now)
{
    const mao_app_state_t *st = mao_state();
    if (st->view == MAO_VIEW_INTRO) {
        return;   /* the first encounter waits for the dial, undistracted */
    }
    const bool home = st->view == MAO_VIEW_HOME;
    const bool sleepy = !st->awake;
    const bool free_face = home && !busy(now) && !controlling(now);
    const mao_percept_t p = m->percept;
    const uint8_t c = m->confidence;

    switch (p) {
    case MAO_PERCEPT_APPROACH_STARTED:
        /* Someone coming closer: the eyes lift towards them. Not yet a
         * reason to wake up. */
        if (free_face && novel(p, c, now, 0.4f)) {
            look(0, sleepy ? LOOK_UP_PX / 3 : LOOK_UP_PX);
        }
        break;

    case MAO_PERCEPT_APPROACH_NEAR:
        company(now);
        if (free_face) {
            if (sleepy || novel(p, c, now, 0.5f)) {
                mao_character_react(MAO_CHAR_REACT_NOTICE);
            } else {
                look(0, LOOK_UP_PX);
            }
        }
        break;

    case MAO_PERCEPT_WITHDRAWN:
        /* Watching them go, if they were just with MAO. */
        if (free_face && st->awake && mao_state_idle_ms(now) < 20000 && chance() < 60) {
            look(chance() < 50 ? -LOOK_SIDE_PX : LOOK_SIDE_PX, LOOK_DOWN_PX / 2);
        }
        break;

    case MAO_PERCEPT_PICKED_UP:
        company(now);
        if (home && !controlling(now)) {
            mao_character_react(MAO_CHAR_REACT_NOTICE);
            if (novel(p, c, now, 0.45f)) {
                mao_haptics_short_pulse();
                big(now);
            }
        }
        break;

    case MAO_PERCEPT_PUT_DOWN:
        company(now);
        if (free_face && novel(p, c, now, 0.3f)) {
            look(0, LOOK_DOWN_PX);            /* settles, glances down */
        }
        break;

    case MAO_PERCEPT_HARD_PUT_DOWN:
        company(now);
        if (home && !controlling(now)) {
            mao_character_react(MAO_CHAR_REACT_NOTICE);
            if (m->detail) {
                mao_haptics_tremor();         /* it was dropped */
            }
            big(now);
        }
        break;

    case MAO_PERCEPT_UPSIDE_DOWN:
        company(now);
        s_upside_down = m->detail != 0;
        if (home && !controlling(now) && s_upside_down) {
            mao_character_react(MAO_CHAR_REACT_NOTICE);
            big(now);
        }
        break;

    case MAO_PERCEPT_SHAKE:
        company(now);
        if (home && !controlling(now)) {
            mao_character_react(MAO_CHAR_REACT_NOTICE);
            if (novel(p, c, now, 0.4f)) {
                mao_haptics_tremor();
            }
            big(now);
        }
        break;

    case MAO_PERCEPT_NUDGED:
        /* A bumped desk is not company. Awake MAO glances; asleep, nothing. */
        if (free_face && st->awake && novel(p, c, now, 0.35f) && chance() < 70) {
            look(chance() < 50 ? -LOOK_SIDE_PX : LOOK_SIDE_PX, 0);
        }
        break;

    case MAO_PERCEPT_COVERED:
        /* The ToF alone on the A1 (no light sensor): something over the
         * window. */
        company(now);
        if (free_face) {
            if (m->detail) {
                look(0, LOOK_UP_PX);
            } else if (novel(p, c, now, 0.35f)) {
                mao_character_react(MAO_CHAR_REACT_NOTICE);
            }
        }
        break;

    case MAO_PERCEPT_USB_CONNECTED:
        if (m->detail) {
            company(now);
            if (free_face) {
                mao_character_react(MAO_CHAR_REACT_NOTICE);
            }
            mao_audio_notice();
            mao_haptics_short_pulse();
        } else if (free_face && st->awake) {
            look(0, LOOK_DOWN_PX);
        }
        break;

    case MAO_PERCEPT_LOW_BATTERY:
        /* A tired look; no alarm. The critical case is the battery event's
         * (mao_app_power_critical). */
        if (m->detail < 2 && free_face && st->awake) {
            look(0, LOOK_DOWN_PX);
        }
        break;

    case MAO_PERCEPT_REMOTE_SIGNAL:
        company(now);
        if (free_face && novel(p, c, now, 0.4f)) {
            mao_character_react(MAO_CHAR_REACT_ATTEND);
        }
        break;

    case MAO_PERCEPT_LONG_ABSENCE:
        company(now);
        if (home) {
            mao_character_react(MAO_CHAR_REACT_NOTICE);
            mao_haptics_wake_pulse();
            mao_audio_notice();
            big(now);
        }
        ESP_LOGI(TAG, "someone is back after %u min", m->detail);
        break;

    default:
        /* Touch, pet, knock, room noise / light, fiddling: not on this
         * board (or not MAO's), never acted on. */
        break;
    }
}
