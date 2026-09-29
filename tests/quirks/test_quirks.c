/*
 * Host tests for when MAO's quirks may happen (components/mao_app/mao_quirks.c):
 * never on cue, never every time, and each moment understood correctly.
 */
#include <stdio.h>
#include "mao_quirks.h"

static int s_fail, s_pass;
#define CHECK(cond) do { if (cond) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    mao_quirks_t q;

    /* a context-bound quirk happens when its moment comes ... */
    mao_quirks_reset(&q);
    CHECK(mao_quirks_roll(&q, MAO_QK_HOLD, 1000, 999));
    /* ... but never again inside its cooldown, whatever the roll */
    CHECK(!mao_quirks_roll(&q, MAO_QK_HOLD, 2000, 0));
    CHECK(!mao_quirks_roll(&q, MAO_QK_HOLD, 1000 + mao_qk_rules[MAO_QK_HOLD].cooldown_ms - 1, 0));
    /* ... and again after it */
    CHECK(mao_quirks_roll(&q, MAO_QK_HOLD, 1000 + mao_qk_rules[MAO_QK_HOLD].cooldown_ms, 500));
    /* each quirk keeps its own cooldown */
    CHECK(mao_quirks_roll(&q, MAO_QK_FORGIVE, 2000, 0));

    /* a rare one: only below its chance, and a miss does not start the cooldown */
    mao_quirks_reset(&q);
    const uint32_t ch = mao_qk_rules[MAO_QK_DELIGHT].chance;
    CHECK(ch > 0 && ch < 1000);
    CHECK(!mao_quirks_roll(&q, MAO_QK_DELIGHT, 1000, ch));        /* just over: no */
    CHECK(mao_quirks_roll(&q, MAO_QK_DELIGHT, 1100, ch - 1));     /* just under: yes, right after a miss */
    CHECK(!mao_quirks_roll(&q, MAO_QK_DELIGHT, 1200, 0));         /* then its cooldown */

    /* how rare, over many moments far apart: close to its chance */
    mao_quirks_reset(&q);
    int hits = 0;
    uint32_t t = 0, r = 12345;
    for (int i = 0; i < 20000; i++) {
        t += mao_qk_rules[MAO_QK_OFFSCREEN].cooldown_ms;        /* never held back by the cooldown */
        r = r * 1103515245u + 12345u;
        hits += mao_quirks_roll(&q, MAO_QK_OFFSCREEN, t, (r >> 8) % 1000u);
    }
    const int want = 20000 * (int)mao_qk_rules[MAO_QK_OFFSCREEN].chance / 1000;
    CHECK(hits > want * 8 / 10 && hits < want * 12 / 10);
    /* moments that come constantly still fire at most once per cooldown */
    mao_quirks_reset(&q);
    hits = 0;
    for (uint32_t ms = 0; ms < 60u * 60u * 1000u; ms += 50) {  /* an hour of moments, every 50 ms */
        hits += mao_quirks_roll(&q, MAO_QK_GRUMPY, ms, 0);
    }
    CHECK(hits == (int)(60u * 60u * 1000u / mao_qk_rules[MAO_QK_GRUMPY].cooldown_ms));
    /* no rule without a cooldown: nothing can repeat every frame */
    for (int k = 0; k < MAO_QK_COUNT; k++) {
        CHECK(mao_qk_rules[k].cooldown_ms >= 30000u);
    }
    CHECK(!mao_quirks_roll(&q, MAO_QK_COUNT, 0, 0));

    /* the same device again and again */
    mao_quirks_reset(&q);
    CHECK(!mao_quirks_opened(&q, 7, 1000));
    CHECK(!mao_quirks_opened(&q, 7, 5000));
    CHECK(mao_quirks_opened(&q, 7, 9000));                        /* the third within the window */
    CHECK(!mao_quirks_opened(&q, 7, 12000));                      /* and it starts over */
    /* too slow: no */
    mao_quirks_reset(&q);
    mao_quirks_opened(&q, 7, 0);
    mao_quirks_opened(&q, 7, 20000);
    CHECK(!mao_quirks_opened(&q, 7, 20000 + MAO_QK_REVISIT_MS));  /* the first has left the window */
    /* another device in between breaks it */
    mao_quirks_reset(&q);
    mao_quirks_opened(&q, 7, 0);
    mao_quirks_opened(&q, 8, 1000);
    mao_quirks_opened(&q, 7, 2000);
    CHECK(!mao_quirks_opened(&q, 7, 3000) || 0);                  /* only two of 7 in a row */
    CHECK(mao_quirks_opened(&q, 7, 4000));

    printf("quirks: %d checks passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
