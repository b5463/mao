/*
 * MAO's inner life (see mao_life.h): drives, impulses, attention with
 * saccades, cat mode, and how human events land. Randomness is drawn only
 * when something is decided, never per frame.
 */
#include "mao_life.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "MAO_LIFE";

/* Drives (seconds). */
#define ENERGY_DRAIN_S      1500.0f   /* awake idle time to run flat */
#define ENERGY_REFILL_S     300.0f
#define CURIOSITY_BUILD_S   45.0f
#define SOCIAL_BUILD_S      150.0f
#define IRRITATION_DECAY_S  25.0f
#define ABSENT_S            90.0f     /* gone this long = "welcome back" */
/* Impulses. */
#define IMPULSE_MIN_S       3.5f
#define IMPULSE_MAX_S       10.0f
/* Saccades (gaze px). */
#define SACCADE_JUMP        2.5f      /* error that triggers a jump */
#define SACCADE_REFRACT_MS  110       /* minimum time between jumps */
#define MICRO_MIN_MS        600
#define MICRO_MAX_MS        2600
#define MICRO_AMP           1.6f
/* Cat mode. */
#define CAT_MIN_S           15.0f
#define CAT_MAX_S           35.0f

enum { ATT_NONE = 0, ATT_FLY, ATT_POINT, ATT_USER };
enum { AFTER_NONE = 0, AFTER_POUNCE, AFTER_SHRUG, AFTER_SNIFF_FIND };

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (float)(esp_random() % 10000) / 10000.0f;
}

static float clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static bool chance(float p)
{
    return frand(0.0f, 1.0f) < p;
}

static void play(mao_life_t *l, mao_lark_t *lark, const char *state, uint32_t now)
{
    const int s = mao_lark_find(state);
    if (s >= 0) {
        mao_lark_switch(lark, s, now);
    }
    l->doing = state;
}

static void attend(mao_life_t *l, uint8_t kind, float tx, float ty, float seconds, uint8_t after, uint32_t now)
{
    l->att = kind;
    l->att_t0 = now;
    l->att_until = now + (uint32_t)(seconds * 1000.0f);
    l->tx = tx;
    l->ty = ty;
    l->after = after;
    for (int i = 0; i < 4; i++) {
        l->ph[i] = frand(0.0f, 6.283f);
    }
}

void mao_life_init(mao_life_t *l, uint32_t now)
{
    memset(l, 0, sizeof(*l));
    l->energy = 1.0f;
    l->curiosity = 0.3f;
    l->last_ms = l->last_input_ms = now;
    l->next_impulse_ms = now + 6000;
    l->next_sacc_ms = now + 800;
    mao_spring_init(&l->gain, 1.0f, MAO_SPRING_SOFT);
    mao_spring_init(&l->catness, 0.0f, (mao_spring_profile_t){ .k = 40.0f, .zeta = 0.8f });
    memset(l->history, -1, sizeof(l->history));
}

bool mao_life_is_cat(const mao_life_t *l, uint32_t now)
{
    return l->cat_until && (int32_t)(l->cat_until - now) > 0;
}

/* ---------------------------------------------------------------------- */
/* Human events                                                           */
/* ---------------------------------------------------------------------- */

void mao_life_event(mao_life_t *l, mao_lark_t *lark, life_event_t ev, uint32_t now)
{
    const bool cat = mao_life_is_cat(l, now);
    switch (ev) {
    case LIFE_EV_FIRST_TOUCH:
        l->att = ATT_NONE;
        if ((float)(now - l->last_input_ms) / 1000.0f > ABSENT_S) {
            play(l, lark, "greet", now);                  /* you're back */
        } else if (l->energy < 0.25f) {
            play(l, lark, cat ? "cat_startle" : "startled", now);
        } else {
            play(l, lark, cat ? "cat_curious" : "keen", now);
        }
        break;
    case LIFE_EV_TOUCH:
        if (l->irritation > 0.55f) {
            play(l, lark, "hmph", now);
        } else if (cat) {
            play(l, lark, "cat_purr", now);
        } else if (l->social > 0.6f) {
            play(l, lark, "pleased", now);                /* finally, attention */
        }
        l->social = 0.0f;
        break;
    case LIFE_EV_INPUT:
        l->last_input_ms = now;
        l->social = clamp01(l->social - 0.15f);
        l->att = ATT_NONE;
        break;
    case LIFE_EV_REVERSAL:
        l->irritation = clamp01(l->irritation + 0.05f);
        break;
    case LIFE_EV_DIZZY:
        l->irritation = clamp01(l->irritation + 0.25f);
        break;
    case LIFE_EV_WARM:
        l->social = 0.0f;
        l->irritation = 0.0f;
        break;
    case LIFE_EV_WOKEN:
        l->irritation = clamp01(l->irritation + 0.3f);
        l->energy = clamp01(l->energy + 0.1f);
        play(l, lark, l->irritation > 0.5f || chance(0.4f) ? "grumpywake" : "wakeup", now);
        break;
    }
}

/* ---------------------------------------------------------------------- */
/* Impulses                                                               */
/* ---------------------------------------------------------------------- */

enum {
    IMP_GLANCE, IMP_FLY, IMP_SNIFF, IMP_REMEMBER, IMP_SEEK, IMP_LONELY, IMP_CAT, IMP_TIRED, IMP_DOZE,
    IMP_STARTLE, IMP_HUM, IMP_SCHEME, IMP_GRUMBLE, IMP_PEEK,
    IMP_CAT_LOAF, IMP_CAT_GROOM, IMP_CAT_STRETCH, IMP_CAT_ZOOM, IMP_CAT_ANNOYED, IMP_CAT_BLINK,
    IMP_CAT_DEADPAN, IMP_CAT_CONTENT, IMP_CAT_GREED, IMP_CAT_EAT,
    IMP_IMPROV,
    IMP_COUNT,
};

static void impulse(mao_life_t *l, mao_lark_t *lark, uint32_t now)
{
    const bool cat = mao_life_is_cat(l, now);
    const float c = l->curiosity, s = l->social, e = l->energy, ir = l->irritation;
    float w[IMP_COUNT] = { 0 };
    w[IMP_GLANCE] = 4.0f;
    w[IMP_IMPROV] = 12.0f;            /* the most common: something new every time */
    w[IMP_FLY] = 1.2f + 6.0f * c;
    w[IMP_SNIFF] = 1.5f + 5.0f * c;
    w[IMP_REMEMBER] = 0.8f + 2.5f * c;
    w[IMP_SEEK] = 8.0f * s;
    w[IMP_LONELY] = s > 0.7f ? 6.0f * s * s : 0.0f;
    w[IMP_CAT] = cat ? 0.0f : 0.08f;   /* exceedingly rare */
    w[IMP_TIRED] = 6.0f * (1.0f - e);
    w[IMP_DOZE] = 8.0f * (1.0f - e) * (1.0f - e);
    w[IMP_STARTLE] = 0.3f;              /* at nothing: rare, or it stops being funny */
    w[IMP_HUM] = 2.5f * (1.0f - ir) * e;
    w[IMP_SCHEME] = 1.2f;
    w[IMP_GRUMBLE] = 9.0f * ir;
    w[IMP_PEEK] = 1.0f + 1.5f * c;
    if (cat) {
        w[IMP_CAT_LOAF] = 4.0f + 3.0f * (1.0f - e);
        w[IMP_CAT_GROOM] = 3.0f;
        w[IMP_CAT_STRETCH] = 2.0f + 3.0f * (1.0f - e);
        w[IMP_CAT_ZOOM] = e > 0.5f ? 2.5f : 0.0f;
        w[IMP_CAT_ANNOYED] = 1.0f + 8.0f * ir;
        w[IMP_CAT_BLINK] = 2.0f + 4.0f * s;
        w[IMP_CAT_DEADPAN] = 7.0f;            /* her usual cat face */
        w[IMP_CAT_CONTENT] = 2.0f + 3.0f * (1.0f - ir);
        w[IMP_CAT_GREED] = 1.0f + 2.5f * c;
        w[IMP_CAT_EAT] = 1.2f;
        w[IMP_REMEMBER] = 0.0f;             /* in cat mode it is greed instead */
        w[IMP_FLY] *= 1.6f;          /* cats watch everything */
        w[IMP_SNIFF] *= 0.5f;
        w[IMP_HUM] = 0.0f;
    }
    /* Nothing that just happened happens again right away (improvisations
     * and glances are composed fresh, so they are exempt). */
    for (int h = 0; h < MAO_LIFE_HISTORY; h++) {
        const int k = l->history[h];
        if (k >= 0 && k != IMP_IMPROV && k != IMP_GLANCE) {
            w[k] *= h < 2 ? 0.08f : 0.4f;
        }
    }
    float total = 0.0f;
    for (int i = 0; i < IMP_COUNT; i++) {
        total += w[i];
    }
    float r = frand(0.0f, total);
    int k = 0;
    for (; k < IMP_COUNT - 1; k++) {
        if ((r -= w[k]) < 0.0f) {
            break;
        }
    }
    memmove(&l->history[1], &l->history[0], sizeof(l->history) - sizeof(l->history[0]));
    l->history[0] = (int8_t)k;
    const float side = chance(0.5f) ? 1.0f : -1.0f;
    switch (k) {
    case IMP_IMPROV:
        mao_improv_start(l, now);
        l->doing = "improvise";
        break;
    case IMP_GLANCE:
        attend(l, ATT_POINT, side * frand(5.0f, 11.0f), frand(-7.0f, 5.0f), frand(0.8f, 2.2f), AFTER_NONE, now);
        l->doing = "glance";
        break;
    case IMP_FLY:
        attend(l, ATT_FLY, 0, 0, frand(3.0f, 6.5f), cat ? (chance(0.6f) ? AFTER_POUNCE : AFTER_NONE)
                                                        : (chance(0.35f) ? AFTER_SHRUG : AFTER_NONE), now);
        play(l, lark, cat ? "cat_hunt" : "track", now);
        l->curiosity *= 0.4f;
        break;
    case IMP_SNIFF:
        play(l, lark, "sniff", now);
        l->after = chance(0.4f) ? AFTER_SNIFF_FIND : AFTER_NONE;
        l->att_until = now + 1900;     /* reuse the attention timer for the follow-up */
        l->att = ATT_NONE;
        l->curiosity *= 0.5f;
        break;
    case IMP_REMEMBER:  play(l, lark, "remember", now); l->curiosity *= 0.6f; break;
    case IMP_SEEK:
        attend(l, ATT_USER, 0, 0, 2.6f, AFTER_NONE, now);
        play(l, lark, "seek", now);
        break;
    case IMP_LONELY:    play(l, lark, "lonely", now); break;
    case IMP_CAT:
        l->cat_until = now + (uint32_t)(frand(CAT_MIN_S, CAT_MAX_S) * 1000.0f);
        play(l, lark, "cat_curious", now);
        ESP_LOGI(TAG, "cat mode for %lu s", (unsigned long)((l->cat_until - now) / 1000));
        break;
    case IMP_TIRED:     play(l, lark, cat ? "cat_stretch" : (chance(0.5f) ? "yawn" : "sigh"), now); break;
    case IMP_DOZE:      play(l, lark, cat ? "cat_loaf" : "doze", now); break;
    case IMP_STARTLE:
        attend(l, ATT_POINT, side * frand(8.0f, 12.0f), frand(-8.0f, 2.0f), 1.4f, AFTER_NONE, now);
        play(l, lark, cat ? (chance(0.5f) ? "cat_blank" : "cat_startle") : "startled", now);
        break;
    case IMP_HUM:       play(l, lark, "hum", now); break;
    case IMP_SCHEME:    play(l, lark, chance(0.6f) ? "sly" : "sinister", now); break;
    case IMP_GRUMBLE: {
        static const char *const kGrumble[] = { "hmph", "tsk", "eyeroll", "sulky", "pout" };
        play(l, lark, kGrumble[esp_random() % 5], now);
        l->irritation *= 0.6f;
        break;
    }
    case IMP_PEEK:      play(l, lark, "peek", now); break;
    case IMP_CAT_LOAF:  play(l, lark, "cat_loaf", now); break;
    case IMP_CAT_GROOM: play(l, lark, "cat_groom", now); break;
    case IMP_CAT_STRETCH: play(l, lark, "cat_stretch", now); break;
    case IMP_CAT_ZOOM:  play(l, lark, "cat_zoomies", now); break;
    case IMP_CAT_ANNOYED: play(l, lark, chance(0.5f) ? "cat_glare" : "cat_annoyed", now); l->irritation *= 0.6f; break;
    case IMP_CAT_BLINK: play(l, lark, "cat_slowblink", now); break;
    case IMP_CAT_DEADPAN: play(l, lark, "cat_deadpan", now); break;
    case IMP_CAT_CONTENT: play(l, lark, "cat_content", now); break;
    case IMP_CAT_GREED: play(l, lark, "cat_greed", now); l->curiosity *= 0.5f; break;
    case IMP_CAT_EAT:   play(l, lark, "cat_eat", now); break;
    default: break;
    }
    ESP_LOGI(TAG, "impulse %s (energy %.2f curiosity %.2f social %.2f irritation %.2f%s)", l->doing ? l->doing : "?",
             (double)e, (double)c, (double)s, (double)ir, cat ? ", cat" : "");
    l->next_impulse_ms = now + (uint32_t)(1000.0f * frand(IMPULSE_MIN_S, IMPULSE_MAX_S) * (1.15f - 0.4f * c));
}

/* ---------------------------------------------------------------------- */
/* Attention and saccades                                                 */
/* ---------------------------------------------------------------------- */

static void attention_target(mao_life_t *l, uint32_t now, float *tx, float *ty)
{
    const float t = (float)(now - l->att_t0) / 1000.0f;
    switch (l->att) {
    case ATT_FLY:     /* an imaginary fly: two layered wanders, never a pattern */
        *tx = 10.0f * sinf(0.9f * t + l->ph[0]) + 4.0f * sinf(2.6f * t + l->ph[1]);
        *ty = -4.0f + 7.0f * sinf(1.3f * t + l->ph[2]) + 3.0f * sinf(3.3f * t + l->ph[3]);
        break;
    case ATT_USER:
        *tx = 0.0f;
        *ty = -1.0f;
        break;
    case ATT_POINT:
        *tx = l->tx;
        *ty = l->ty;
        break;
    default:
        *tx = *ty = 0.0f;
        break;
    }
}

static void saccades(mao_life_t *l, uint32_t now, bool idle)
{
    if (!idle) {
        l->gx = l->gy = 0.0f;
        return;
    }
    if (l->att != ATT_NONE) {
        float tx, ty;
        attention_target(l, now, &tx, &ty);
        const float ex = tx - l->gx, ey = ty - l->gy;
        if ((ex * ex + ey * ey) > SACCADE_JUMP * SACCADE_JUMP && (int32_t)(now - l->next_sacc_ms) >= 0) {
            /* Jump slightly past a moving target, like real pursuit catch-up. */
            l->gx = tx + (l->att == ATT_FLY ? 0.25f * ex : 0.0f);
            l->gy = ty + (l->att == ATT_FLY ? 0.25f * ey : 0.0f);
            l->next_sacc_ms = now + SACCADE_REFRACT_MS;
        }
        return;
    }
    /* Resting: micro-saccades around the centre. */
    if ((int32_t)(now - l->next_sacc_ms) >= 0) {
        l->gx = frand(-MICRO_AMP, MICRO_AMP);
        l->gy = frand(-MICRO_AMP, MICRO_AMP) * 0.7f;
        l->next_sacc_ms = now + (uint32_t)frand((float)MICRO_MIN_MS, (float)MICRO_MAX_MS);
    }
}

static void attention_end(mao_life_t *l, mao_lark_t *lark, uint32_t now)
{
    const uint8_t after = l->after;
    l->att = ATT_NONE;
    l->after = AFTER_NONE;
    switch (after) {
    case AFTER_POUNCE:      play(l, lark, "cat_pounce", now); break;
    case AFTER_SHRUG:       play(l, lark, "deadpan", now); break;
    case AFTER_SNIFF_FIND:  play(l, lark, mao_life_is_cat(l, now) ? "cat_greed" : "poison", now); break;
    default:
        /* The fly is gone: stop staring. */
        if (l->doing && (!strcmp(l->doing, "track") || !strcmp(l->doing, "cat_hunt"))) {
            play(l, lark, "neutral", now);
        }
        break;
    }
}

/* ---------------------------------------------------------------------- */

void mao_life_update(mao_life_t *l, mao_lark_t *lark, mao_motion_t *m, uint32_t now, bool idle, bool sleepy,
                     float add[CH_COUNT])
{
    const float dt = (float)(now - l->last_ms) / 1000.0f;
    l->last_ms = now;

    /* Drives. */
    if (sleepy) {
        l->energy = clamp01(l->energy + dt / ENERGY_REFILL_S);
    } else {
        l->energy = clamp01(l->energy - dt / ENERGY_DRAIN_S);
    }
    if (idle) {
        l->curiosity = clamp01(l->curiosity + dt / CURIOSITY_BUILD_S);
    }
    if ((float)(now - l->last_input_ms) / 1000.0f > 20.0f) {
        l->social = clamp01(l->social + dt / SOCIAL_BUILD_S);
    }
    l->irritation -= l->irritation * dt / IRRITATION_DECAY_S;

    /* Impulses and their follow-ups. */
    float igx = 0.0f, igy = 0.0f;
    if (!idle || sleepy) {
        l->att = ATT_NONE;
        l->att_until = 0;                      /* the user interrupted: drop any follow-up */
        l->after = AFTER_NONE;
        mao_improv_cancel(l);
        l->next_impulse_ms = now + 2500;       /* collect itself before acting again */
        mao_improv_update(l, m, now, &igx, &igy, add);   /* lets a cancelled phrase relax */
    } else {
        if (l->att_until && (int32_t)(now - l->att_until) >= 0) {
            l->att_until = 0;
            attention_end(l, lark, now);
        }
        if ((int32_t)(now - l->next_impulse_ms) >= 0 && l->att == ATT_NONE && !l->improv.active) {
            impulse(l, lark, now);
        }
        const bool was = l->improv.active;
        if (mao_improv_update(l, m, now, &igx, &igy, add)) {
            l->att = ATT_POINT;                /* the phrase steers the saccades */
            l->tx = igx;
            l->ty = igy;
        } else if (was) {
            l->att = ATT_NONE;
        }
    }
    saccades(l, now, idle && !sleepy);

    l->gain.target = idle ? 1.0f : 0.15f;
    mao_spring_step(&l->gain, dt < 0.05f ? dt : 0.05f);
    const float g = l->gain.x < 0.0f ? 0.0f : l->gain.x;
    add[CH_GAZE_X] += g * l->gx;
    add[CH_GAZE_Y] += g * l->gy;

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
