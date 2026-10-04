/*
 * MAO application: what MAO does about what it noticed.
 *
 * Percepts arrive already filtered and fused (mao_perception); this is the
 * cause/state side. It decides with context, not by table lookup:
 *   - where MAO is (only HOME shows the character; menus stay undisturbed),
 *   - whether it is sleepy (a sleepy MAO only peeks unless someone is
 *     really there),
 *   - habituation: the same stimulus again soon gets a smaller reaction or
 *     none (interest recovers over a minute),
 *   - mood: fiddling escalates suspicious -> tsk -> cross, and the grudge
 *     lingers after it calms down; petting softens it,
 *   - big reactions are not trampled by small ones right after.
 * The vocabulary is the existing one (reactions, look, sounds, touches);
 * MAO never announces a sensor: an approach is a look, not a beep.
 */
#include "mao_app_priv.h"

#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mao_audio.h"
#include "mao_character.h"
#include "mao_haptics.h"
#include "mao_power.h"

static const char *TAG = "MAO_APP";

#define INTEREST_RECOVER_S    60.0f    /* habituation recovers with this time constant */
#define INTEREST_SPEND        0.55f    /* a reaction leaves this much interest */
#define INTEREST_PASSIVE      0.85f    /* noticed but not reacted to */
#define BIG_REACTION_US       (1200 * 1000)
#define GRUDGE_L2_US          (30LL * 1000 * 1000)
#define GRUDGE_L3_US          (90LL * 1000 * 1000)
#define SLEEPY_SOONER_IDLE_US (15LL * 1000 * 1000)

typedef struct {
    float interest;
    int64_t last_us;
} habit_t;

static habit_t s_habit[MAO_PERCEPT_COUNT];
static int64_t s_busy_until_us;     /* a big reaction is playing */

/* ------------------------------------------------------------------------ */

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

static bool grudge(int64_t now)
{
    return now < mao_state()->grudge_until_us;
}

/* Someone is here: counts as activity, and MAO wakes for it. */
static void company(int64_t now)
{
    mao_app_note_activity(now);
    mao_app_wake();
}

static int8_t zone_x(uint16_t zone)
{
    switch (zone) {
    case MAO_PERCEPT_ZONE_RIGHT: return 10;
    case MAO_PERCEPT_ZONE_LEFT:  return -10;
    default:                     return 0;
    }
}

/* ------------------------------------------------------------------------ */
/* Mood                                                                     */
/* ------------------------------------------------------------------------ */

static void on_fiddling(uint16_t level, bool home, int64_t now)
{
    const uint8_t was = mao_state()->annoyance;
    mao_state_set_annoyance((uint8_t)level);
    if (level == 0) {
        /* Calmed down; the grudge (if any) still runs out on its own. */
        if (was >= 2 && home) {
            mao_character_look(0, 3, 900);    /* a little sigh, looking down */
        }
        return;
    }
    if (level >= 2) {
        const int64_t g = now + (level >= 3 ? GRUDGE_L3_US : GRUDGE_L2_US);
        if (g > mao_state()->grudge_until_us) {
            mao_state_set_grudge_until(g);
        }
    }
    if (!home) {
        return;
    }
    switch (level) {
    case 1:
        mao_character_react(MAO_CHAR_REACT_SUSPICIOUS);
        break;
    case 2:
        mao_character_react(MAO_CHAR_REACT_ANNOYED);
        mao_audio_tsk();
        mao_haptics_tick();
        big(now);
        break;
    default:
        mao_character_react(MAO_CHAR_REACT_CROSS);
        mao_haptics_annoyed_buzz();
        big(now);
        break;
    }
    ESP_LOGI(TAG, "mood: annoyance %u", level);
}

/* ------------------------------------------------------------------------ */
/* Percept -> behaviour                                                     */
/* ------------------------------------------------------------------------ */

void mao_app_on_percept(const mao_percept_msg_t *m, int64_t now)
{
    const mao_app_state_t *st = mao_state();
    if (st->view == MAO_VIEW_INTRO) {
        return;   /* the first encounter waits for the dial, undistracted */
    }
    const bool home = st->view == MAO_VIEW_HOME;
    const bool sleepy = !st->awake;
    const mao_percept_t p = m->percept;
    const uint8_t c = m->confidence;

    switch (p) {
    case MAO_PERCEPT_APPROACH_STARTED:
        /* Someone coming closer: the eyes lift towards them. Not yet a
         * reason to wake up. */
        if (home && novel(p, c, now, 0.4f) && !busy(now)) {
            mao_character_look(0, sleepy ? -2 : -5, 900);
        }
        break;

    case MAO_PERCEPT_APPROACH_NEAR:
        company(now);
        if (home && !busy(now)) {
            if (sleepy || novel(p, c, now, 0.5f)) {
                mao_character_react(MAO_CHAR_REACT_NOTICE);
            } else {
                mao_character_look(0, -4, 800);
            }
        }
        break;

    case MAO_PERCEPT_WITHDRAWN:
        /* Watching them go, if they were just with MAO. */
        if (home && st->awake && mao_state_idle_ms(now) < 20000 && chance() < 60 && !busy(now)) {
            mao_character_look(chance() < 50 ? -6 : 6, 4, 1000);
        }
        break;

    case MAO_PERCEPT_TOUCH:
        company(now);
        if (!home || busy(now)) {
            break;
        }
        if (m->detail == MAO_PERCEPT_ZONE_TOP) {
            if (novel(p, c, now, 0.3f)) {
                mao_character_react(MAO_CHAR_REACT_NOTICE);
            }
        } else if (m->detail == MAO_PERCEPT_ZONE_REAR) {
            if (novel(p, c, now, 0.5f)) {
                mao_character_react(MAO_CHAR_REACT_SURPRISED);   /* something at its base */
            }
        } else {
            mao_character_look(zone_x(m->detail), 0, 1000);      /* towards the touch */
        }
        break;

    case MAO_PERCEPT_TOUCH_HOLD:
        company(now);
        if (!home || busy(now)) {
            break;
        }
        if (m->detail == MAO_PERCEPT_ZONE_TOP && !grudge(now) && st->annoyance == 0 && novel(p, c, now, 0.4f)) {
            mao_character_react(MAO_CHAR_REACT_HAPPY);
        } else if (m->detail == MAO_PERCEPT_ZONE_LEFT || m->detail == MAO_PERCEPT_ZONE_RIGHT) {
            mao_character_look(zone_x(m->detail), 1, 1600);
        }
        break;

    case MAO_PERCEPT_TOUCH_REPEAT:
        company(now);
        if (home && !busy(now) && novel(p, c, now, 0.3f)) {
            mao_character_react(MAO_CHAR_REACT_SUSPICIOUS);
        }
        break;

    case MAO_PERCEPT_GENTLE_PET:
        company(now);
        if (grudge(now)) {
            /* Still sore: a pet helps, but it is not forgiven at once. */
            const int64_t left = st->grudge_until_us - now;
            mao_state_set_grudge_until(now + left / 2);
            if (home && !busy(now)) {
                mao_character_react(MAO_CHAR_REACT_SUSPICIOUS);
            }
            break;
        }
        if (home && !busy(now)) {
            mao_character_react(MAO_CHAR_REACT_CONTENT);
            if (novel(p, c, now, 0.5f)) {
                mao_haptics_heartbeat();
            }
            big(now);
        }
        break;

    case MAO_PERCEPT_PICKED_UP:
        company(now);
        if (home) {
            if (novel(p, c, now, 0.45f)) {
                mao_character_react(MAO_CHAR_REACT_SURPRISED);
                mao_haptics_short_pulse();
                big(now);
            } else {
                mao_character_react(MAO_CHAR_REACT_NOTICE);
            }
        }
        break;

    case MAO_PERCEPT_PUT_DOWN:
        company(now);
        if (home && !busy(now) && novel(p, c, now, 0.3f)) {
            mao_character_look(0, 3, 600);     /* settles, glances down */
        }
        break;

    case MAO_PERCEPT_HARD_PUT_DOWN:
        company(now);
        if (home) {
            mao_character_react(m->detail ? MAO_CHAR_REACT_DIZZY : MAO_CHAR_REACT_SURPRISED);
            if (m->detail) {
                mao_haptics_tremor();
            }
            big(now);
        }
        break;

    case MAO_PERCEPT_UPSIDE_DOWN:
        company(now);
        mao_state_set_upside_down(m->detail != 0);
        if (home) {
            if (m->detail) {
                mao_character_react(MAO_CHAR_REACT_DIZZY);
                big(now);
            } else if (st->annoyance < 2) {
                mao_character_react(MAO_CHAR_REACT_HAPPY);   /* the right way up again */
            }
        }
        break;

    case MAO_PERCEPT_SHAKE:
        company(now);
        if (home) {
            mao_character_react(MAO_CHAR_REACT_DIZZY);
            if (novel(p, c, now, 0.4f)) {
                mao_haptics_tremor();
            }
            big(now);
        }
        break;

    case MAO_PERCEPT_NUDGED:
        /* A bumped desk is not company. Awake MAO glances; asleep, nothing. */
        if (home && st->awake && !busy(now) && novel(p, c, now, 0.35f) && chance() < 70) {
            mao_character_look(chance() < 50 ? -8 : 8, 0, 700);
        }
        break;

    case MAO_PERCEPT_KNOCK:
        company(now);
        if (home && !busy(now)) {
            if (m->detail >= 2 && novel(p, c, now, 0.4f)) {
                mao_character_react(MAO_CHAR_REACT_SURPRISED);
            } else {
                mao_character_react(MAO_CHAR_REACT_NOTICE);
                mao_character_look(0, -6, 900);    /* "who's there?" */
            }
        }
        break;

    case MAO_PERCEPT_SUDDEN_NOISE:
        /* Startle, but a sound alone does not wake MAO up properly. */
        if (home && !busy(now) && novel(p, c, now, 0.45f)) {
            if (sleepy) {
                mao_character_look(chance() < 50 ? -5 : 5, -3, 700);
            } else {
                mao_character_react(MAO_CHAR_REACT_SURPRISED);
            }
        }
        break;

    case MAO_PERCEPT_QUIET_ROOM:
        mao_state_set_room_quiet(m->detail != 0);
        if (m->detail && st->awake && mao_state_idle_ms(now) * 1000LL > SLEEPY_SOONER_IDLE_US) {
            mao_app_go_sleepy();   /* nothing going on: the eyes get heavy sooner */
        }
        break;

    case MAO_PERCEPT_DARK_ROOM:
        mao_state_set_room_dark(m->detail != 0);
        mao_app_refresh_brightness();
        if (m->detail && st->awake && mao_state_idle_ms(now) * 1000LL > SLEEPY_SOONER_IDLE_US) {
            mao_app_go_sleepy();
        } else if (!m->detail && sleepy && home) {
            mao_character_look(0, -4, 800);   /* lights on: the lids lift for a moment */
        }
        break;

    case MAO_PERCEPT_COVERED:
        company(now);
        if (!home) {
            break;
        }
        if (m->detail) {
            mao_character_look(0, -6, 1500);  /* something over the face */
        } else if (novel(p, c, now, 0.35f) && !grudge(now)) {
            mao_character_react(MAO_CHAR_REACT_HAPPY);   /* peekaboo */
            big(now);
        } else {
            mao_character_react(MAO_CHAR_REACT_NOTICE);
        }
        break;

    case MAO_PERCEPT_USB_CONNECTED:
        if (m->detail) {
            company(now);
            if (home && !busy(now)) {
                mao_character_react(MAO_CHAR_REACT_NOTICE);
            }
            mao_audio_notice();
            mao_haptics_short_pulse();
        } else if (home && st->awake) {
            mao_character_look(0, 5, 800);
        }
        break;

    case MAO_PERCEPT_LOW_BATTERY:
        if (m->detail >= 2) {
            mao_app_go_sleepy();               /* deep sleep follows within seconds */
        } else if (home && st->awake && !busy(now)) {
            mao_character_look(0, 3, 1200);    /* a tired look; no alarm */
        }
        break;

    case MAO_PERCEPT_REMOTE_SIGNAL:
        company(now);
        if (home && !busy(now) && novel(p, c, now, 0.4f)) {
            mao_character_react(MAO_CHAR_REACT_ATTEND);
        }
        break;

    case MAO_PERCEPT_LONG_ABSENCE:
        company(now);
        if (home) {
            mao_character_react(MAO_CHAR_REACT_HAPPY);
            mao_haptics_wake_pulse();
            mao_audio_confirm();
            big(now);
        }
        ESP_LOGI(TAG, "someone is back after %u min", m->detail);
        break;

    case MAO_PERCEPT_FIDDLING_ESCALATION:
        on_fiddling(m->detail, home, now);
        break;

    default:
        break;
    }
}
