#include "mao_character_accent.h"

#include <math.h>
#include <string.h>

/* Families, not one colour per state: few enough that each reads as a mood. */
#define ACC_MAD     0xF2646Eu   /* mad, dizzy-mad, sinister */
#define ACC_CROSS   0xF58C84u   /* glare, tsk, hmph, sulky, pout, contempt, grumpy */
#define ACC_WARM    0xF7B58Cu   /* hum, purr, happy, pleased, content */
#define ACC_BRIGHT  0xF5C870u   /* excited, glint, proud, giggle */
#define ACC_SAD     0xA8B0F2u   /* sad, lonely, sigh, worry, anxious */
#define ACC_SLEEP   0xC7A6F7u   /* asleep, doze, drowsy, yawn (= the lavender egg) */
#define ACC_BLANK   0xBCAFB8u   /* zoned, blank, deadpan, bored, daydream */

static const struct {
    const char *state;
    uint32_t col;
} kMap[] = {
    { "mad", ACC_MAD }, { "dizzy_mad", ACC_MAD }, { "sinister", ACC_MAD },
    { "glare", ACC_CROSS }, { "cat_glare", ACC_CROSS }, { "tsk", ACC_CROSS }, { "hmph", ACC_CROSS },
    { "sulky", ACC_CROSS }, { "pout", ACC_CROSS }, { "contempt", ACC_CROSS }, { "cat_annoyed", ACC_CROSS },
    { "grumpywake", ACC_CROSS }, { "eyeroll", ACC_CROSS },
    { "hum", ACC_WARM }, { "purr", ACC_WARM }, { "cat_purr", ACC_WARM }, { "happy", ACC_WARM },
    { "pleased", ACC_WARM }, { "cat_content", ACC_WARM }, { "relief", ACC_WARM }, { "cat_slowblink", ACC_WARM },
    { "excited", ACC_BRIGHT }, { "glint", ACC_BRIGHT }, { "proud", ACC_BRIGHT }, { "giggle", ACC_BRIGHT },
    { "sad", ACC_SAD }, { "lonely", ACC_SAD }, { "sigh", ACC_SAD }, { "worry", ACC_SAD }, { "anxious", ACC_SAD },
    { "asleep", ACC_SLEEP }, { "doze", ACC_SLEEP }, { "drowsy", ACC_SLEEP }, { "nodoff", ACC_SLEEP },
    { "yawn", ACC_SLEEP },
    { "zoned", ACC_BLANK }, { "cat_blank", ACC_BLANK }, { "deadpan", ACC_BLANK }, { "cat_deadpan", ACC_BLANK },
    { "bored", ACC_BLANK }, { "daydream", ACC_BLANK },
};

uint32_t mao_accent_for_state(const char *state)
{
    if (state) {
        for (unsigned i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) {
            if (strcmp(kMap[i].state, state) == 0) {
                return kMap[i].col;
            }
        }
    }
    return MAO_ACCENT_BASE;
}

const char *mao_accent_mapped_state(int i)
{
    return i >= 0 && i < (int)(sizeof(kMap) / sizeof(kMap[0])) ? kMap[i].state : NULL;
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        const float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
        out |= (uint32_t)lrintf(ca + (cb - ca) * t) << s;
    }
    return out;
}

uint32_t mao_accent_target(const char *state, bool sleepy, float weight)
{
    if (sleepy) {
        return ACC_SLEEP;
    }
    return mix(MAO_ACCENT_BASE, mao_accent_for_state(state), weight);
}

void mao_accent_step(mao_accent_t *a, uint32_t target, float dt_s)
{
    const float tr = (float)((target >> 16) & 0xFF), tg = (float)((target >> 8) & 0xFF),
                tb = (float)(target & 0xFF);
    if (!a->init) {
        a->r = tr;
        a->g = tg;
        a->b = tb;
        a->init = true;
        a->landed = target;
        return;
    }
    if (a->landed == target) {
        return;
    }
    a->landed = 0;
    const float k = dt_s <= 0.0f ? 0.0f : 1.0f - expf(-dt_s / MAO_ACCENT_FADE_S);
    a->r += (tr - a->r) * k;
    a->g += (tg - a->g) * k;
    a->b += (tb - a->b) * k;
    /* land exactly: no endless sub-step fade */
    if (fabsf(tr - a->r) < 2.0f && fabsf(tg - a->g) < 2.0f && fabsf(tb - a->b) < 2.0f) {
        a->r = tr;
        a->g = tg;
        a->b = tb;
        a->landed = target;
    }
}

uint32_t mao_accent_color(const mao_accent_t *a)
{
    if (!a->init) {
        return MAO_ACCENT_BASE;
    }
    if (a->landed) {
        return a->landed;                    /* exact: the base is the base */
    }
    return ((uint32_t)lrintf(a->r) << 16) | ((uint32_t)lrintf(a->g) << 8) | (uint32_t)lrintf(a->b);
}

uint32_t mao_accent_disc(uint32_t accent, uint32_t base_disc)
{
    return accent == MAO_ACCENT_BASE ? base_disc : mix(accent, 0xFFFFFF, 0.72f);
}
