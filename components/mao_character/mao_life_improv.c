/*
 * Improvisation: a small composer that makes up a new eye "phrase" every
 * time - a few beats, each a fixation somewhere, a lid mood, maybe a blink,
 * maybe a micro-expression - so idle MAO is never a loop and never a rerun.
 * The palette follows Maomao and her current drives: mostly deadpan lids and
 * short, observant looks; wider and higher when curious, heavier and lower
 * when bored or tired, squintier when irritated, looking back at you when
 * she wants attention. Gaze goes through the life layer's saccades; the rest
 * is eased onto the rig as additive offsets.
 */
#include "mao_life.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "MAO_IMPROV";

enum { B_LID, B_SMILE, B_SQUINT, B_OPEN, B_TILT, B_FY, B_PUPIL, B_COUNT };
static const uint8_t kChannel[B_COUNT] = { CH_NARROW, CH_SMILE, CH_SQUINT, CH_OPEN, CH_TILT, CH_FACE_Y, CH_PUPIL };

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (float)(esp_random() % 10000) / 10000.0f;
}

static bool chance(float p)
{
    return frand(0.0f, 1.0f) < p;
}

static float smooth01(float x)
{
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    return x * x * (3.0f - 2.0f * x);
}

void mao_improv_start(mao_life_t *l, uint32_t now)
{
    mao_improv_t *im = &l->improv;
    const float c = l->curiosity, e = l->energy, s = l->social, ir = l->irritation;
    const bool cat = mao_life_is_cat(l, now);
    memset(im->beat, 0, sizeof(im->beat));
    im->n = 2 + (uint8_t)(esp_random() % (MAO_IMPROV_BEATS - 2));
    float side = chance(0.5f) ? 1.0f : -1.0f;

    for (int b = 0; b < im->n; b++) {
        mao_improv_beat_t *bt = &im->beat[b];
        const bool last = b == im->n - 1;
        /* Timing: observant looks are short, a tired or bored eye lingers. */
        bt->dur_ms = (uint16_t)frand(320.0f, 1100.0f + 900.0f * (1.0f - e) + 600.0f * (1.0f - c));

        /* Where to look. */
        if (last) {
            bt->gx = frand(-1.5f, 1.5f);
            bt->gy = frand(-1.0f, 1.0f);
        } else if (chance(0.22f + 0.4f * s)) {
            bt->gx = frand(-1.5f, 1.5f);                 /* back at you */
            bt->gy = frand(-1.5f, 0.5f);
        } else {
            if (chance(0.45f)) {
                side = -side;                            /* the other way */
            }
            bt->gx = side * frand(3.0f, 12.0f);
            bt->gy = frand(-9.0f + 5.0f * (1.0f - c), 3.0f + 5.0f * (1.0f - e));
        }

        /* Lid mood: her deadpan, shaped by the drives. */
        float lid = frand(-0.04f, 0.14f) + 0.14f * (1.0f - e) + 0.10f * (1.0f - c) - 0.22f * c * c + 0.08f * ir;
        if (cat) {
            lid += 0.06f;
        }
        bt->v[B_LID] = last ? 0.0f : lid;
        bt->v[B_PUPIL] = last ? 0.0f : frand(-0.15f, 0.15f) + 0.25f * c - 0.2f * ir;

        /* Now and then a micro-expression. */
        if (!last && chance(0.3f)) {
            switch (esp_random() % 7) {
            case 0: bt->v[B_SMILE] = frand(0.15f, 0.38f) * (1.0f - ir); break;          /* a private smile */
            case 1: bt->v[B_SQUINT] = (chance(0.5f) ? 1.0f : -1.0f) * frand(0.3f, 0.6f); break;
            case 2: bt->v[B_OPEN] = frand(0.06f, 0.14f); bt->v[B_LID] -= 0.2f; break;   /* oh? */
            case 3: bt->v[B_TILT] = (chance(0.5f) ? 1.0f : -1.0f) * frand(1.5f, 4.0f); break;
            case 4: bt->v[B_FY] = frand(4.0f, 10.0f); break;                              /* a small nod */
            case 5: bt->v[B_FY] = -frand(4.0f, 9.0f); break;                              /* perks up */
            default: bt->v[B_LID] += frand(0.08f, 0.16f); break;                          /* unimpressed */
            }
        }

        /* Blinks at the start of a beat: saccades and blinks go together. */
        const float r = frand(0.0f, 1.0f);
        bt->blink = r < 0.05f ? 3 : (r < 0.10f ? 2 : (r < 0.30f + 0.2f * (1.0f - e) ? 1 : 0));
    }
    im->b = 0;
    im->started = 0;
    im->beat_t0 = now;
    im->active = true;
    for (int k = 0; k < B_COUNT; k++) {
        im->from[k] = im->cur[k];
    }
    ESP_LOGI(TAG, "phrase of %u beats", (unsigned)im->n);
}

/* Advance the phrase: returns the gaze target for the saccade system and
 * adds the eased offsets. Returns false when the phrase is over. */
bool mao_improv_update(mao_life_t *l, mao_motion_t *m, uint32_t now, float *gx, float *gy, float add[CH_COUNT])
{
    mao_improv_t *im = &l->improv;
    if (!im->active) {
        /* Let the last values relax to nothing. */
        for (int k = 0; k < B_COUNT; k++) {
            im->cur[k] *= 0.9f;
            add[kChannel[k]] += im->cur[k];
        }
        return false;
    }
    mao_improv_beat_t *bt = &im->beat[im->b];
    const uint32_t t = now - im->beat_t0;
    if (im->started != im->b + 1) {      /* first tick of this beat */
        im->started = im->b + 1;
        if (bt->blink == 1) {
            mao_motion_blink(m, now, (uint16_t)(MAO_BLINK_S * 1000.0f), 1);
        } else if (bt->blink == 2) {
            mao_motion_blink(m, now, (uint16_t)(MAO_BLINK_S * 1000.0f), 2);
        } else if (bt->blink == 3) {
            mao_motion_blink(m, now, (uint16_t)(MAO_BLINK_S * 2400.0f), 1);   /* slow and heavy */
        }
    }
    /* Lids and expressions ease in over the first part of the beat. */
    const float ramp = fminf(260.0f, 0.4f * (float)bt->dur_ms);
    const float u = smooth01((float)t / ramp);
    for (int k = 0; k < B_COUNT; k++) {
        im->cur[k] = im->from[k] + (bt->v[k] - im->from[k]) * u;
        add[kChannel[k]] += im->cur[k];
    }
    *gx = bt->gx;
    *gy = bt->gy;
    if (t >= bt->dur_ms) {
        for (int k = 0; k < B_COUNT; k++) {
            im->from[k] = im->cur[k];
        }
        if (++im->b >= im->n) {
            im->active = false;
            return false;
        }
        im->beat_t0 = now;
    }
    return true;
}

void mao_improv_cancel(mao_life_t *l)
{
    l->improv.active = false;
}
