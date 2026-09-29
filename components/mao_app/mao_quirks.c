#include "mao_quirks.h"

#include <string.h>

/* Cooldown and chance per quirk. Context-bound ones (their moment is rare
 * by itself) always happen when it comes, at most once in their cooldown;
 * the ones whose moment is common are rarer. */
const mao_qk_rule_t mao_qk_rules[MAO_QK_COUNT] = {
    [MAO_QK_HOLD]      = { 60u * 1000u, 1000 },
    [MAO_QK_FORGIVE]   = { 45u * 1000u, 1000 },
    [MAO_QK_REVISIT]   = { 3u * 60u * 1000u, 1000 },
    [MAO_QK_DELIGHT]   = { 8u * 60u * 1000u, 90 },
    [MAO_QK_SERIES]    = { 10u * 60u * 1000u, 600 },
    [MAO_QK_OFFSCREEN] = { 20u * 60u * 1000u, 250 },
    [MAO_QK_GRUMPY]    = { 5u * 60u * 1000u, 1000 },
};

void mao_quirks_reset(mao_quirks_t *s)
{
    memset(s, 0, sizeof(*s));
}

bool mao_quirks_roll(mao_quirks_t *s, mao_qk_t k, uint32_t now_ms, uint32_t roll)
{
    if (k >= MAO_QK_COUNT) {
        return false;
    }
    if (s->ever[k] && now_ms - s->last_ms[k] < mao_qk_rules[k].cooldown_ms) {
        return false;                         /* not again so soon */
    }
    if (roll % 1000u >= mao_qk_rules[k].chance) {
        return false;                         /* not this time */
    }
    s->ever[k] = true;
    s->last_ms[k] = now_ms;
    return true;
}

bool mao_quirks_opened(mao_quirks_t *s, uint64_t id, uint32_t now_ms)
{
    if (id != s->revisit_id) {
        s->revisit_id = id;
        s->revisit_n = 0;
    }
    /* keep the opens still inside the window */
    uint8_t k = 0;
    for (uint8_t i = 0; i < s->revisit_n; i++) {
        if (now_ms - s->revisit_ms[i] < MAO_QK_REVISIT_MS) {
            s->revisit_ms[k++] = s->revisit_ms[i];
        }
    }
    s->revisit_n = k;
    if (s->revisit_n < MAO_QK_REVISIT_N) {
        s->revisit_ms[s->revisit_n++] = now_ms;
    }
    if (s->revisit_n >= MAO_QK_REVISIT_N) {
        s->revisit_n = 0;                     /* the pattern completes once, then starts over */
        return true;
    }
    return false;
}
