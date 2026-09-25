/*
 * Lark-style state / keyframe player and behaviour model (see
 * mao_lark.h). Evaluation is a handful of key lookups per tick; nothing is
 * allocated.
 */
#include "mao_lark.h"

#include <string.h>
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "MAO_LARK";

/* Behaviour model (seconds unless noted). */
#define DWELL_MIN_S        5.0f
#define DWELL_MAX_S        12.0f
#define BORED_FULL_S       75.0f    /* input-free time that is "completely bored" */
#define AGITATION_DECAY_S  20.0f
#define AFFECTION_DECAY_S  90.0f
#define GAIN_PROFILE       MAO_SPRING_SOFT
/* Variants. */
#define TEMPO_MIN          0.85f
#define TEMPO_MAX          1.20f
#define AMP_MIN            0.85f
#define AMP_MAX            1.15f
#define KEY_JITTER         0.20f    /* +-20 % on every keyframe value */
#define TRACK_JITTER       0.12f    /* +-12 % on every track's timing */

static float clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static float frand01(void)
{
    return (float)(esp_random() % 10000) / 10000.0f;
}

/* Deterministic noise in -1..1 from a play seed and two indices. */
static float jitter(uint32_t seed, uint32_t a, uint32_t b)
{
    uint32_t h = seed ^ (a * 0x9E3779B1u) ^ (b * 0x85EBCA77u);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (float)(h & 0xFFFF) / 32767.5f - 1.0f;
}

static float ease(uint8_t e, float t)
{
    switch (e) {
    case LARK_IN_OUT: return t * t * (3.0f - 2.0f * t);
    case LARK_OUT:    return 1.0f - (1.0f - t) * (1.0f - t);
    case LARK_IN:     return t * t;
    case LARK_BACK: {
        const float s = 1.6f, u = t - 1.0f;
        return 1.0f + u * u * ((s + 1.0f) * u + s);
    }
    case LARK_HOLD:   return t < 1.0f ? 0.0f : 1.0f;
    default:          return t;
    }
}

/* Key value with this play's jitter (zeros stay exactly zero). */
static float key_v(const lark_key_t *k, int i, uint32_t seed, uint32_t track)
{
    return k[i].v * (1.0f + KEY_JITTER * jitter(seed, track + 1, (uint32_t)i + 1));
}

static float track_eval(const lark_track_t *tr, uint32_t t, uint32_t seed, uint32_t track)
{
    const lark_key_t *k = tr->keys;
    if (t <= k[0].t_ms) {
        return key_v(k, 0, seed, track);
    }
    for (int i = 1; i < tr->n; i++) {
        if (t <= k[i].t_ms) {
            const float span = (float)(k[i].t_ms - k[i - 1].t_ms);
            const float u = span > 0.0f ? (float)(t - k[i - 1].t_ms) / span : 1.0f;
            const float a = key_v(k, i - 1, seed, track), b = key_v(k, i, seed, track);
            return a + (b - a) * ease(k[i].ease, u);
        }
    }
    return key_v(k, tr->n - 1, seed, track);
}

/* What is actually playing: the authored timeline, or this play's generated one. */
typedef struct {
    const lark_track_t *tracks;
    uint8_t n;
    uint16_t length_ms;
    uint8_t flags;
} view_t;

static view_t view_of(const mao_lark_t *l, int s, int8_t gen)
{
    const lark_state_t *st = mao_lark_state(s);
    if (st->gen && gen >= 0) {
        const lark_gen_t *g = &l->gen[gen];
        return (view_t) { g->tracks, g->n_tracks, g->length_ms, st->flags };
    }
    return (view_t) { st->tracks, st->n_tracks, st->length_ms, st->flags };
}

/* Local timeline position of a state played since t0 with variant v. */
static uint32_t local_t(const view_t *vw, uint32_t t0, const lark_variant_t *v, uint32_t now)
{
    uint32_t t = (uint32_t)((float)(now - t0) * v->tempo);
    if (vw->length_ms) {
        t = (vw->flags & LARK_ONESHOT) ? (t > vw->length_ms ? vw->length_ms : t) : t % vw->length_ms;
    }
    return t;
}

static uint32_t play_ms(const view_t *vw, const lark_variant_t *v)
{
    return (uint32_t)((float)vw->length_ms / v->tempo);
}

/* Channels a mirrored variant flips, and channels its amplitude scales. */
static bool mirrored_ch(int ch)
{
    return ch == CH_GAZE_X || ch == CH_FACE_X || ch == CH_TILT || ch == CH_SQUINT || ch == CH_WINK;
}

static bool amp_ch(int ch)
{
    return ch == CH_GAZE_X || ch == CH_GAZE_Y || ch == CH_FACE_X || ch == CH_FACE_Y || ch == CH_TILT;
}

/* Adds a playing timeline into out[]. */
static void state_eval(const view_t *vw, uint32_t t0, const lark_variant_t *v, uint32_t now, float out[CH_COUNT])
{
    const uint32_t t = local_t(vw, t0, v, now);
    for (int i = 0; i < vw->n; i++) {
        const int ch = vw->tracks[i].ch;
        /* Each track runs on its own slightly different clock this play. */
        uint32_t ti = (uint32_t)((float)t * (1.0f + TRACK_JITTER * jitter(v->seed, 0x51u, (uint32_t)i)));
        if (vw->length_ms && !(vw->flags & LARK_ONESHOT)) {
            ti %= vw->length_ms;
        }
        float x = track_eval(&vw->tracks[i], ti, v->seed, (uint32_t)i);
        if (mirrored_ch(ch)) {
            x *= (float)v->mirror;
        }
        if (amp_ch(ch)) {
            x *= v->amp;
        }
        out[ch] += x;
    }
}

int mao_lark_find(const char *name)
{
    for (int i = 0; i < mao_lark_state_count(); i++) {
        if (strcmp(mao_lark_state(i)->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void mao_lark_init(mao_lark_t *l, uint32_t now)
{
    memset(l, 0, sizeof(*l));
    l->pending = -1;
    l->cur_t0 = l->prev_t0 = l->trans_t0 = now;
    l->cur_v = l->prev_v = (lark_variant_t) { .mirror = 1, .tempo = 1.0f, .amp = 1.0f };
    for (int i = 0; i < LARK_HISTORY; i++) {
        l->history[i] = -1;
    }
    l->last_input_ms = l->last_ms = now;
    l->next_pick_ms = now + 4000;
    l->cur_gen = l->prev_gen = -1;
    mao_spring_init(&l->gain, 1.0f, GAIN_PROFILE);
}

void mao_lark_switch(mao_lark_t *l, int s, uint32_t now)
{
    if (s < 0 || s >= mao_lark_state_count() || s == l->cur) {
        return;
    }
    const lark_state_t *st = mao_lark_state(s);
    const lark_variant_t v = {
        .mirror = (st->flags & LARK_NO_MIRROR) || (esp_random() & 1) ? 1 : -1,
        .tempo = TEMPO_MIN + (TEMPO_MAX - TEMPO_MIN) * frand01(),
        .amp = AMP_MIN + (AMP_MAX - AMP_MIN) * frand01(),
        .seed = esp_random(),
    };
    ESP_LOGI(TAG, "state %s -> %s (%s x%.2f amp %.2f)", mao_lark_state(l->cur)->name, st->name,
             v.mirror > 0 ? "=" : "mirrored", (double)v.tempo, (double)v.amp);
    l->prev = l->cur;
    l->prev_t0 = l->cur_t0;    /* the outgoing timeline keeps playing */
    l->prev_v = l->cur_v;
    l->prev_gen = l->cur_gen;  /* ... including a generated one */
    l->cur_gen = -1;
    if (st->gen) {
        /* Compose this play's timeline into the buffer the outgoing state is not using. */
        l->cur_gen = l->prev_gen == 0 ? 1 : 0;
        st->gen(&l->gen[l->cur_gen]);
    }
    l->cur = s;
    l->cur_t0 = now;
    l->cur_v = v;
    l->trans_t0 = now;
    l->plays++;
    memmove(&l->history[1], &l->history[0], sizeof(l->history) - sizeof(l->history[0]));
    l->history[0] = (int16_t)s;
    l->next_pick_ms = now + (uint32_t)(1000.0f * (DWELL_MIN_S + (DWELL_MAX_S - DWELL_MIN_S) * frand01()));
}

static void request(mao_lark_t *l, const char *name)
{
    l->pending = mao_lark_find(name);
}

void mao_lark_event(mao_lark_t *l, lark_event_t ev, uint32_t now)
{
    switch (ev) {
    case LARK_EV_INPUT:
        l->last_input_ms = now;
        l->boredom = 0.0f;
        break;
    case LARK_EV_REVERSAL:
        l->agitation = clamp01(l->agitation + 0.12f);
        break;
    case LARK_EV_DIZZY:
        l->agitation = clamp01(l->agitation + 0.30f);
        request(l, l->agitation > 0.6f ? "dizzy_mad" : "dizzy");
        break;
    case LARK_EV_FAST:
        l->agitation = clamp01(l->agitation + 0.004f);
        break;
    case LARK_EV_PRESS:
        l->affection = clamp01(l->affection + 0.08f);
        /* Pestered: four presses within two seconds earn a look. */
        l->press_ms[l->press_i++ & 3] = now;
        if (l->press_ms[l->press_i & 3] && now - l->press_ms[l->press_i & 3] < 2000) {
            l->agitation = clamp01(l->agitation + 0.2f);
            request(l, l->agitation > 0.5f ? "contempt" : "tsk");
        }
        break;
    case LARK_EV_WARM:
        l->affection = clamp01(l->affection + 0.45f);
        request(l, "happy");
        break;
    case LARK_EV_RETURN:
        request(l, (esp_random() & 1) ? "curious" : "doubletake");
        break;
    }
}

float mao_lark_freshness(const mao_lark_t *l, int state)
{
    for (int i = 0; i < LARK_HISTORY; i++) {
        if (l->history[i] == state) {
            return i < 3 ? 0.05f : 0.35f;
        }
    }
    return 1.0f;
}

void mao_lark_update(mao_lark_t *l, uint32_t now, bool idle, bool sleepy, float gain, float out[CH_COUNT])
{
    const float dt = (float)(now - l->last_ms) / 1000.0f;
    l->last_ms = now;

    /* Mood drifts. */
    l->boredom = clamp01((float)(now - l->last_input_ms) / 1000.0f / BORED_FULL_S);
    l->agitation -= l->agitation * dt / AGITATION_DECAY_S;
    l->affection -= l->affection * dt / AFFECTION_DECAY_S;

    const lark_state_t *cs = mao_lark_state(l->cur);
    const view_t cur_view = view_of(l, l->cur, l->cur_gen);
    if (sleepy) {
        mao_lark_switch(l, mao_lark_find("asleep"), now);   /* closed crescents; the sleep channel does the rest */
        l->pending = -1;
    } else if (l->cur == mao_lark_find("asleep")) {
        mao_lark_switch(l, 0, now);
    } else if (idle && l->pending >= 0) {
        mao_lark_switch(l, l->pending, now);        /* events play as soon as MAO is free */
        l->pending = -1;
    } else if ((cs->flags & LARK_ONESHOT) && now - l->cur_t0 >= play_ms(&cur_view, &l->cur_v)) {
        const int nx = cs->next ? mao_lark_find(cs->next) : 0;
        mao_lark_switch(l, nx >= 0 ? nx : 0, now);
    }
    /* No timed state rotation: states change only when something causes it
     * (events, the mind in mao_life.c, one-shot follow-ups). */

    l->gain.target = gain;
    mao_spring_step(&l->gain, dt < 0.05f ? dt : 0.05f);
    const float g = l->gain.x < 0.0f ? 0.0f : l->gain.x;

    /* Blend outgoing -> incoming with the incoming state's curve. */
    float a[CH_COUNT] = { 0 }, b[CH_COUNT] = { 0 };
    const lark_state_t *st = mao_lark_state(l->cur);
    const float u = st->in_ms ? clamp01((float)(now - l->trans_t0) / (float)st->in_ms) : 1.0f;
    const float k = ease(st->in_ease, u);
    /* A looping generated state composes a fresh cycle each time round. */
    if ((st->flags & LARK_REGEN) && st->gen && l->cur_gen >= 0) {
        const view_t cv = view_of(l, l->cur, l->cur_gen);
        if (cv.length_ms && (float)(now - l->cur_t0) * l->cur_v.tempo >= (float)cv.length_ms && k >= 1.0f) {
            st->gen(&l->gen[l->cur_gen]);
            l->cur_t0 = now;
            l->cur_v.seed = esp_random();
        }
    }
    const view_t cv = view_of(l, l->cur, l->cur_gen);
    state_eval(&cv, l->cur_t0, &l->cur_v, now, b);
    if (k < 1.0f) {
        const view_t pv = view_of(l, l->prev, l->prev_gen);
        state_eval(&pv, l->prev_t0, &l->prev_v, now, a);
    }
    for (int i = 0; i < CH_COUNT; i++) {
        out[i] = g * (a[i] + (b[i] - a[i]) * k);
    }
}
