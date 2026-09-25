/*
 * MAO's mind (see mao_life.h). Every action below names its cause: a
 * stimulus, or a drive that stimuli (or their absence) moved past a
 * threshold. Randomness only varies HOW something is done and exactly when
 * within a natural range - never WHETHER something happens.
 */
#include "mao_life.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "MAO_MIND";

/* Drives (seconds). */
#define ENERGY_DRAIN_S      1500.0f   /* awake time to run flat */
#define ENERGY_REFILL_S     300.0f
#define BORED_AFTER_S       20.0f     /* boredom starts growing after this long without stimuli */
#define BORED_FULL_S        150.0f
#define AROUSAL_DECAY_S     5.0f
#define IRRITATION_DECAY_S  40.0f
#define AFFECTION_DECAY_S   180.0f
#define HABIT_DECAY_S       20.0f
#define ABSENT_S            90.0f     /* gone this long = "you're back" */
/* Behaviours. */
#define SPONT_GAP_MS        6000      /* at least this between spontaneous behaviours */
#define SLOW_DIAL_MIN       0.4f      /* detents/s: careful, deliberate turning */
#define SLOW_DIAL_MAX       4.0f
#define SLOW_DIAL_S         2.5f
/* Cat mode. */
#define CAT_MIN_S           15.0f
#define CAT_MAX_S           30.0f

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (float)(esp_random() % 10000) / 10000.0f;
}

static float clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static bool before(uint32_t now, uint32_t t)
{
    return t && (int32_t)(t - now) > 0;
}

static void play(mao_life_t *l, mao_lark_t *lark, const char *state, uint32_t now, const char *why)
{
    const int s = mao_lark_find(state);
    if (s >= 0) {
        mao_lark_switch(lark, s, now);
    }
    l->doing = state;
    ESP_LOGI(TAG, "%s <- %s (arousal %.2f energy %.2f boredom %.2f irritation %.2f affection %.2f)", state, why,
             (double)l->arousal, (double)l->energy, (double)l->boredom, (double)l->irritation, (double)l->affection);
}

/* A looping state held until the next stimulus. */
static void hold(mao_life_t *l, mao_lark_t *lark, const char *state, uint32_t now, const char *why)
{
    play(l, lark, state, now, why);
    l->holding = (int8_t)mao_lark_find(state);
}

static void refract(mao_life_t *l, int b, uint32_t now, float seconds)
{
    l->refract[b] = now + (uint32_t)(1000.0f * seconds * frand(0.8f, 1.25f));
}

static bool ready(const mao_life_t *l, int b, uint32_t now)
{
    return !before(now, l->refract[b]);
}

void mao_life_init(mao_life_t *l, uint32_t now)
{
    memset(l, 0, sizeof(*l));
    l->energy = 1.0f;
    l->arousal = 0.3f;
    l->holding = -1;
    l->last_ms = l->last_input_ms = l->last_stim_ms = now;
    l->next_fix_ms = now + 3000;
    l->next_blink_ms = now + 2500;
    mao_spring_init(&l->gain, 1.0f, MAO_SPRING_SOFT);
    mao_spring_init(&l->catness, 0.0f, (mao_spring_profile_t){ .k = 40.0f, .zeta = 0.8f });
}

bool mao_life_is_cat(const mao_life_t *l, uint32_t now)
{
    return before(now, l->cat_until);
}

/* A stimulus: raise arousal by its salience, reset boredom, remember where. */
static float stimulus(mao_life_t *l, float base, float habit, int8_t side, uint32_t now)
{
    const float novelty = 1.0f + 1.5f * l->boredom;          /* after quiet, anything is interesting */
    const float sal = clamp01(base * (1.0f - 0.75f * habit) * novelty);
    l->arousal = clamp01(l->arousal + 0.4f * sal);
    l->boredom *= 0.2f;
    l->last_stim_ms = now;
    if (side) {
        l->stim_side = side;
        l->watch_until = now + (uint32_t)(1500.0f + 5000.0f * sal);   /* anticipation lasts with salience */
    }
    return sal;
}

/* ---------------------------------------------------------------------- */
/* Stimuli from people                                                    */
/* ---------------------------------------------------------------------- */

void mao_life_dial(mao_life_t *l, int32_t detents, uint32_t now)
{
    if (detents == 0) {
        return;
    }
    const float n = fabsf((float)detents);
    const float dt = l->dial_last_ms ? fmaxf((float)(now - l->dial_last_ms) / 1000.0f, 0.03f) : 1.0f;
    l->dial_last_ms = now;
    const float inst = n / fminf(dt, 3.0f);
    l->dial_rate += (inst - l->dial_rate) * 0.5f;
    const float base = 0.2f + 0.5f * clamp01(l->dial_rate / 40.0f);
    stimulus(l, base, l->habit_dial, detents > 0 ? 1 : -1, now);
    l->habit_dial = clamp01(l->habit_dial + 0.03f * n);
    /* Careful slow turning: she starts to study it. */
    if (l->dial_rate >= SLOW_DIAL_MIN && l->dial_rate <= SLOW_DIAL_MAX) {
        if (!l->slow_since) {
            l->slow_since = now;
        }
    } else {
        l->slow_since = 0;
    }
}

void mao_life_event(mao_life_t *l, mao_lark_t *lark, life_event_t ev, uint32_t now)
{
    switch (ev) {
    case LIFE_EV_FIRST_TOUCH: {
        const float away = (float)(now - l->last_input_ms) / 1000.0f;
        if (away > ABSENT_S) {
            play(l, lark, "greet", now, "first touch after a long absence");
        } else if (l->arousal < 0.15f && l->energy < 0.4f) {
            play(l, lark, "startled", now, "touched while half asleep");
        } else if (l->boredom > 0.4f) {
            play(l, lark, "keen", now, "touched while bored: finally something");
        }
        break;
    }
    case LIFE_EV_TOUCH: {
        const float sal = stimulus(l, 0.5f, l->habit_press, 0, now);
        l->habit_press = clamp01(l->habit_press + 0.15f);
        if (l->irritation > 0.55f) {
            play(l, lark, "hmph", now, "touched while irritated");
        } else if (l->affection > 0.35f && sal > 0.3f && lark->cur == 0) {
            play(l, lark, "pleased", now, "touched by someone she likes");
        }
        if (l->irritation < 0.5f) {
            l->affection = clamp01(l->affection + 0.05f);
        }
        break;
    }
    case LIFE_EV_INPUT:
        l->last_input_ms = now;
        /* Any stimulus ends a held mood (doze, sulk, drowsy...). */
        if (l->holding >= 0 || (lark->cur != 0 && !(mao_lark_state(lark->cur)->flags & LARK_ONESHOT))) {
            mao_lark_switch(lark, 0, now);
            l->holding = -1;
        }
        break;
    case LIFE_EV_REVERSAL:
        l->irritation = clamp01(l->irritation + 0.06f);
        l->arousal = clamp01(l->arousal + 0.1f);
        break;
    case LIFE_EV_DIZZY:
        l->irritation = clamp01(l->irritation + 0.25f);
        break;
    case LIFE_EV_WARM:
        l->affection = clamp01(l->affection + 0.5f);
        l->irritation *= 0.3f;
        l->arousal = clamp01(l->arousal + 0.2f);
        if (l->affection > 0.9f && !mao_life_is_cat(l, now)) {
            /* Deeply content: she turns into a cat for a little while. */
            l->cat_until = now + (uint32_t)(frand(CAT_MIN_S, CAT_MAX_S) * 1000.0f);
            ESP_LOGI(TAG, "cat mode <- deep contentment (affection %.2f)", (double)l->affection);
        }
        break;
    case LIFE_EV_WOKEN:
        l->irritation = clamp01(l->irritation + 0.25f);
        l->energy = clamp01(l->energy + 0.1f);
        l->arousal = clamp01(l->arousal + 0.5f);
        play(l, lark, (l->irritation > 0.5f || l->energy < 0.3f) ? "grumpywake" : "wakeup", now, "woken up");
        break;
    }
}

/* ---------------------------------------------------------------------- */
/* Spontaneous behaviour: drives past thresholds                          */
/* ---------------------------------------------------------------------- */

static void behave(mao_life_t *l, mao_lark_t *lark, uint32_t now)
{
    static uint32_t s_next_spont;
    if (before(now, s_next_spont) || l->holding >= 0 || lark->cur != 0) {
        return;        /* something is already going on */
    }
    const float quiet = (float)(now - l->last_input_ms) / 1000.0f;
    bool acted = true;
    if (l->energy < 0.2f && l->boredom > 0.5f && ready(l, LIFE_B_DOZE, now)) {
        hold(l, lark, "doze", now, "exhausted and nothing happening");
        refract(l, LIFE_B_DOZE, now, 60.0f);
    } else if (l->energy < 0.45f && l->boredom > 0.3f && ready(l, LIFE_B_YAWN, now)) {
        play(l, lark, "yawn", now, "tired and bored");
        refract(l, LIFE_B_YAWN, now, 120.0f);
    } else if (l->irritation > 0.45f && l->arousal < 0.5f && quiet > 3.0f && ready(l, LIFE_B_GRUMBLE, now)) {
        play(l, lark, l->irritation > 0.7f ? "eyeroll" : (l->irritation > 0.55f ? "tsk" : "sidelong"), now,
             "still annoyed about it");
        l->irritation *= 0.7f;
        refract(l, LIFE_B_GRUMBLE, now, 30.0f);
    } else if (l->boredom > 0.85f && ready(l, LIFE_B_SULK, now)) {
        hold(l, lark, l->affection > 0.3f ? "lonely" : "sulky", now, "ignored for a long time");
        refract(l, LIFE_B_SULK, now, 90.0f);
    } else if (l->boredom > 0.55f && ready(l, LIFE_B_SIGH, now)) {
        play(l, lark, "sigh", now, "bored");
        refract(l, LIFE_B_SIGH, now, 70.0f);
    } else if (l->boredom > 0.35f && l->stim_side && ready(l, LIFE_B_WATCH, now)) {
        /* She looks at where the knob was last turned: waiting for it. */
        l->watch_until = now + (uint32_t)frand(1800.0f, 3200.0f);
        l->next_fix_ms = now;
        l->doing = "watch the knob";
        ESP_LOGI(TAG, "watch the knob <- bored, remembers the last turn (side %d)", (int)l->stim_side);
        refract(l, LIFE_B_WATCH, now, 35.0f);
    } else if (l->affection > 0.5f && l->irritation < 0.2f && l->arousal < 0.3f && ready(l, LIFE_B_CONTENT, now)) {
        play(l, lark, "slowblink", now, "content, calm, fond of you");
        refract(l, LIFE_B_CONTENT, now, 60.0f);
    } else {
        acted = false;
    }
    if (acted) {
        s_next_spont = now + SPONT_GAP_MS;
    }
}

/* ---------------------------------------------------------------------- */
/* Attention, saccades and blinks                                         */
/* ---------------------------------------------------------------------- */

static void attention(mao_life_t *l, mao_motion_t *m, uint32_t now)
{
    const float calm = 1.0f - l->arousal;
    if ((int32_t)(now - l->next_fix_ms) < 0) {
        return;
    }
    float tx, ty;
    uint32_t hold_ms;
    if (before(now, l->watch_until) && l->stim_side) {
        /* Watching the side the stimulus came from. */
        tx = (float)l->stim_side * (6.0f + 5.0f * l->arousal) + frand(-1.2f, 1.2f);
        ty = -1.0f + frand(-1.0f, 1.0f);
        hold_ms = (uint32_t)frand(700.0f, 1600.0f);
    } else {
        /* Resting: straight ahead, a touch low when calm or tired; small
         * re-fixations whose size grows with arousal. */
        const float amp = 0.6f + 2.2f * l->arousal;
        tx = frand(-amp, amp);
        ty = 1.5f * calm + 2.0f * (1.0f - l->energy) + frand(-amp, amp) * 0.6f;
        hold_ms = (uint32_t)frand(2000.0f, 3500.0f + 3500.0f * calm);
    }
    const float jump = fabsf(tx - l->gx) + fabsf(ty - l->gy);
    l->gx = tx;
    l->gy = ty;
    l->next_fix_ms = now + hold_ms;
    /* Large saccades often come with a blink. */
    if (jump > 6.0f && frand(0.0f, 1.0f) < 0.3f) {
        mao_motion_blink(m, now, (uint16_t)(MAO_BLINK_S * 1000.0f), 1);
        l->next_blink_ms = now + 2000;
    }
}

static void blinks(mao_life_t *l, mao_motion_t *m, uint32_t now)
{
    if ((int32_t)(now - l->next_blink_ms) < 0) {
        return;
    }
    const float calm = 1.0f - l->arousal, tired = 1.0f - l->energy;
    const bool slow = tired > 0.6f;
    const uint8_t count = (frand(0.0f, 1.0f) < 0.08f + 0.1f * l->irritation) ? 2 : 1;
    mao_motion_blink(m, now, (uint16_t)(MAO_BLINK_S * 1000.0f * (slow ? 1.8f : 1.0f)), count);
    const float base = 3200.0f + 2600.0f * calm - 1400.0f * tired;
    l->next_blink_ms = now + (uint32_t)(base * frand(0.6f, 1.5f));
}

/* ---------------------------------------------------------------------- */

void mao_life_update(mao_life_t *l, mao_lark_t *lark, mao_motion_t *m, uint32_t now, bool idle, bool sleepy,
                     float add[CH_COUNT])
{
    const float dt = (float)(now - l->last_ms) / 1000.0f;
    l->last_ms = now;

    /* Drives. */
    l->energy = clamp01(l->energy + (sleepy ? dt / ENERGY_REFILL_S : -dt / ENERGY_DRAIN_S));
    const float quiet = (float)(now - l->last_stim_ms) / 1000.0f;
    l->boredom = clamp01(fmaxf(l->boredom, (quiet - BORED_AFTER_S) / BORED_FULL_S));
    l->arousal -= l->arousal * dt / AROUSAL_DECAY_S;
    l->irritation -= l->irritation * dt / IRRITATION_DECAY_S;
    l->affection -= l->affection * dt / AFFECTION_DECAY_S;
    l->habit_dial -= l->habit_dial * dt / HABIT_DECAY_S;
    l->habit_press -= l->habit_press * dt / HABIT_DECAY_S;
    if ((float)(now - l->last_input_ms) > 600.0f) {
        l->dial_rate *= expf(-dt / 0.4f);
    }

    if (idle && !sleepy) {
        /* Careful slow turning that just stopped: she keeps studying it. */
        if (l->slow_since && now - l->last_input_ms > 300 && l->last_input_ms - l->slow_since > SLOW_DIAL_S * 1000.0f &&
            ready(l, LIFE_B_EXAMINE, now) && lark->cur == 0) {
            play(l, lark, "examine", now, "you were turning it slowly and carefully");
            refract(l, LIFE_B_EXAMINE, now, 40.0f);
        }
        if (now - l->last_input_ms > 1000) {
            l->slow_since = 0;
        }
        behave(l, lark, now);
        attention(l, m, now);
        blinks(l, m, now);
    } else {
        l->next_fix_ms = now + 400;          /* let the dial / press own the gaze */
        l->next_blink_ms = now + 1500;
    }

    /* How she looks right now, continuously, from her state. */
    const float calm = 1.0f - l->arousal, tired = 1.0f - l->energy;
    l->gain.target = idle ? 1.0f : 0.25f;
    mao_spring_step(&l->gain, dt < 0.05f ? dt : 0.05f);
    const float g = l->gain.x < 0.0f ? 0.0f : l->gain.x;
    add[CH_GAZE_X] += g * l->gx;
    add[CH_GAZE_Y] += g * l->gy;
    if (!sleepy) {
        add[CH_NARROW] += 0.10f * calm + 0.22f * tired + 0.10f * l->boredom + 0.08f * l->irritation
                          - 0.26f * powf(l->arousal, 0.7f);
        add[CH_PUPIL] += 0.40f * l->arousal - 0.15f * l->irritation - 0.08f * calm;
        add[CH_SMILE] += 0.26f * l->affection * (1.0f - l->irritation) * calm;
        add[CH_LID_ANGLE] += 0.40f * l->irritation - 0.25f * l->boredom * (1.0f - l->irritation);
    }

    /* Cat mode: slit pupils and almond eyes. */
    const bool cat = mao_life_is_cat(l, now);
    if (!cat && l->cat_until) {
        l->cat_until = 0;
        ESP_LOGI(TAG, "cat mode over");
    }
    l->catness.target = cat ? 1.0f : 0.0f;
    mao_spring_step(&l->catness, dt < 0.05f ? dt : 0.05f);
    const float k = clamp01(l->catness.x);
    add[CH_SLIT] += 0.75f * k;
    add[CH_EYE_W] += 0.06f * k;
    add[CH_EYE_H] -= 0.12f * k;
}
