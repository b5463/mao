/*
 * Host tests for fiddling detection (components/mao_app/mao_fiddle.c): what
 * a hand does when it sets a light, and what it does when it is messing
 * with MAO - and that only the second one is ever noticed.
 */
#include <stdio.h>
#include "mao_fiddle.h"

static int s_fail, s_pass;
#define CHECK(cond) do { if (cond) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* A simulated light 0..100, 1 % a detent; returns the highest level seen. */
typedef struct {
    mao_fiddle_t f;
    int v;
    uint32_t t;
    int max_level, fired[4];
} sim_t;

static void sim_init(sim_t *s, int v)
{
    mao_fiddle_reset(&s->f);
    s->v = v;
    s->t = 10000;
    s->max_level = 0;
    for (int i = 0; i < 4; i++) {
        s->fired[i] = 0;
    }
}

static void turn(sim_t *s, int d, uint32_t gap_ms)
{
    s->t += gap_ms;
    s->v += d;
    s->v = s->v < 0 ? 0 : (s->v > 100 ? 100 : s->v);
    const int8_t edge = s->v == 0 ? -1 : (s->v == 100 ? 1 : 0);
    const mao_fiddle_level_t l = mao_fiddle_turn(&s->f, d, edge, s->t);
    if (l) {
        s->fired[l]++;
        s->max_level = (int)l > s->max_level ? (int)l : s->max_level;
    }
}

/* n detents one way, `step` per event, `gap` ms apart */
static void run(sim_t *s, int n, int step, uint32_t gap)
{
    for (int i = 0; i < n; i++) {
        turn(s, step, gap);
    }
}

int main(void)
{
    sim_t s;

    /* --- setting a light: never noticed --- */
    /* slow and careful, with the usual overshoot and correction */
    sim_init(&s, 40);
    run(&s, 12, 1, 120); run(&s, 3, -1, 200); run(&s, 1, 1, 400);
    CHECK(s.max_level == 0);
    /* fine-tuning around a value: five small corrections over several seconds */
    sim_init(&s, 50);
    for (int k = 0; k < 5; k++) { run(&s, 2, 1, 300); run(&s, 1, -1, 600); }
    CHECK(s.max_level == 0);
    /* one sweep across to see the range, then back to a level */
    sim_init(&s, 50);
    run(&s, 50, 2, 30); run(&s, 60, -1, 40); run(&s, 3, 1, 300);
    CHECK(s.max_level == 0);
    /* all the way up, all the way down: twice, a few seconds apart */
    sim_init(&s, 0);
    run(&s, 25, 4, 40); s.t += 3000; run(&s, 25, -4, 40); s.t += 3000; run(&s, 25, 4, 40);
    CHECK(s.max_level == 0);
    /* pushing against the top for a while counts once */
    sim_init(&s, 95);
    run(&s, 40, 1, 60);
    CHECK(s.max_level == 0);
    /* two decisions far apart are not a reversal */
    sim_init(&s, 50);
    for (int k = 0; k < 10; k++) { run(&s, 3, k & 1 ? -1 : 1, 2000); }
    CHECK(s.max_level == 0);

    /* --- messing with it: noticed, and escalating --- */
    /* wiggling back and forth quickly */
    sim_init(&s, 50);
    for (int k = 0; k < 10; k++) { run(&s, 2, k & 1 ? -1 : 1, 80); }
    CHECK(s.max_level >= MAO_FIDDLE_NOTICE);
    CHECK(s.fired[MAO_FIDDLE_NOTICE] == 1);           /* once, not every flip */
    /* slamming end to end */
    sim_init(&s, 50);
    for (int k = 0; k < 6; k++) { run(&s, 13, k & 1 ? -8 : 8, 25); }
    CHECK(s.max_level >= MAO_FIDDLE_ANNOYED);
    /* keeping it up: fed up, and each level said once, in order */
    sim_init(&s, 50);
    for (int k = 0; k < 24; k++) { run(&s, 13, k & 1 ? -8 : 8, 25); }
    CHECK(s.max_level == MAO_FIDDLE_FED_UP);
    CHECK(s.fired[MAO_FIDDLE_NOTICE] <= 1 && s.fired[MAO_FIDDLE_ANNOYED] <= 1);
    /* going on long after "mad": said again, but not every turn */
    const int mad_before = s.fired[MAO_FIDDLE_FED_UP];
    for (int k = 0; k < 40; k++) { run(&s, 13, k & 1 ? -8 : 8, 25); }   /* ~13 s more */
    CHECK(s.fired[MAO_FIDDLE_FED_UP] > mad_before && s.fired[MAO_FIDDLE_FED_UP] <= mad_before + 3);

    /* --- it is over --- */
    /* a quiet spell starts a new bout: first level again, and only after fiddling again */
    s.t += FIDDLE_QUIET_MS + 100;
    turn(&s, 1, 0);
    CHECK(mao_fiddle_score(&s.f, s.t) == 0);
    const int notice_before = s.fired[MAO_FIDDLE_NOTICE];
    for (int k = 0; k < 10; k++) { run(&s, 2, k & 1 ? -1 : 1, 80); }
    CHECK(s.fired[MAO_FIDDLE_NOTICE] == notice_before + 1);
    /* turning on calmly after a bout does not keep it going */
    sim_init(&s, 50);
    for (int k = 0; k < 10; k++) { run(&s, 2, k & 1 ? -1 : 1, 80); }
    const int n1 = s.fired[MAO_FIDDLE_NOTICE];
    run(&s, 30, 1, 200);                              /* 6 s of one calm direction */
    CHECK(mao_fiddle_score(&s.f, s.t) < FIDDLE_L1);
    for (int k = 0; k < 10; k++) { run(&s, 2, k & 1 ? -1 : 1, 80); }
    CHECK(s.fired[MAO_FIDDLE_NOTICE] == n1 + 1);      /* a second bout is its own */

    /* zero detents mean nothing */
    sim_init(&s, 50);
    CHECK(mao_fiddle_turn(&s.f, 0, 0, 1000) == MAO_FIDDLE_NONE);

    /* --- switching the light --- */
    {
        mao_fiddle_t f;
        int max = 0, fired[4] = { 0 };
        uint32_t t = 10000;
        #define SW(gap) do { t += (gap); const mao_fiddle_level_t l_ = mao_fiddle_switch(&f, t); \
                             if (l_) { fired[l_]++; max = (int)l_ > max ? (int)l_ : max; } } while (0)
        /* on, and off again later: never */
        mao_fiddle_reset(&f); max = 0;
        SW(0); SW(3000); SW(6000); SW(9000);
        CHECK(max == 0);
        /* off and on to check it works: never */
        mao_fiddle_reset(&f); max = 0;
        SW(0); SW(1200);
        CHECK(max == 0);
        /* flicking it: noticed, then annoyed, then mad - each said once */
        mao_fiddle_reset(&f); max = 0; fired[1] = fired[2] = fired[3] = 0;
        for (int k = 0; k < 3; k++) { SW(400); }
        CHECK(max == MAO_FIDDLE_NOTICE);
        for (int k = 0; k < 2; k++) { SW(400); }
        CHECK(max == MAO_FIDDLE_ANNOYED);
        for (int k = 0; k < 3; k++) { SW(400); }
        CHECK(max == MAO_FIDDLE_FED_UP);
        CHECK(fired[1] == 1 && fired[2] == 1 && fired[3] == 1);
        /* switching and sweeping add up: one bout */
        mao_fiddle_reset(&f); max = 0;
        SW(0);
        for (int k = 0; k < 6; k++) {
            t += 60;
            const mao_fiddle_level_t l = mao_fiddle_turn(&f, k & 1 ? -2 : 2, 0, t);
            max = (int)l > max ? (int)l : max;
        }
        SW(300);
        CHECK(max >= MAO_FIDDLE_NOTICE);
        /* and a quiet spell ends it - remembering it for a while */
        t += FIDDLE_QUIET_MS + 100;
        SW(0);
        CHECK(mao_fiddle_score(&f, t) > FIDDLE_SWITCH_SCORE);
        t += FIDDLE_CARRY_MS + FIDDLE_QUIET_MS;
        SW(0);
        CHECK(mao_fiddle_score(&f, t) == FIDDLE_SWITCH_SCORE);          /* ... and forgotten after */
        #undef SW
    }

    /* --- memory: teasing a MAO that is still moody --- */
    {
        /* how many quick flicks a fresh MAO needs to be annoyed */
        int fresh_n = 0, moody_n = 0, calm_n = 0;
        sim_init(&s, 50);
        for (int k = 0; k < 40 && !s.fired[MAO_FIDDLE_ANNOYED]; k++) { run(&s, 2, k & 1 ? -1 : 1, 80); fresh_n++; }
        /* glare it, stop for 5 s, then the same flicks */
        sim_init(&s, 50);
        for (int k = 0; k < 24; k++) { run(&s, 13, k & 1 ? -8 : 8, 25); }
        CHECK(s.max_level == MAO_FIDDLE_FED_UP);
        s.t += 5000;
        s.v = 50;                                           /* the same place as the fresh run */
        s.fired[MAO_FIDDLE_ANNOYED] = 0;
        for (int k = 0; k < 40 && !s.fired[MAO_FIDDLE_ANNOYED]; k++) { run(&s, 2, k & 1 ? -1 : 1, 80); moody_n++; }
        CHECK(moody_n < fresh_n);                           /* it gets there sooner */
        /* ... but long after, it starts fresh again */
        s.t += FIDDLE_CARRY_MS + 1000;
        s.v = 50;
        s.fired[MAO_FIDDLE_ANNOYED] = 0;
        for (int k = 0; k < 40 && !s.fired[MAO_FIDDLE_ANNOYED]; k++) { run(&s, 2, k & 1 ? -1 : 1, 80); calm_n++; }
        CHECK(calm_n == fresh_n);
        /* one calm turn right after a bout is still only a turn */
        sim_init(&s, 50);
        for (int k = 0; k < 24; k++) { run(&s, 13, k & 1 ? -8 : 8, 25); }
        s.t += 5000;
        s.v = 50;                                           /* the same place as the fresh run */
        s.max_level = 0; s.fired[1] = s.fired[2] = s.fired[3] = 0;
        run(&s, 3, 1, 200);
        CHECK(s.max_level == 0);
    }

    /* --- what a stop means --- */
    {
        /* stopping just short of the tsk: MAO was waiting for it */
        sim_init(&s, 50);
        int held = 0;
        for (int k = 0; k < 40 && mao_fiddle_score(&s.f, s.t) < FIDDLE_L2 - 2; k++) { run(&s, 2, k & 1 ? -1 : 1, 80); }
        CHECK(s.max_level == MAO_FIDDLE_NOTICE);
        held = mao_fiddle_quiet(&s.f, s.t + 1200) == MAO_FIDDLE_QUIET_HELD;
        CHECK(held);
        CHECK(mao_fiddle_quiet(&s.f, s.t + 1400) == MAO_FIDDLE_QUIET_NONE);   /* once per run */
        /* stopping early in a notice (far from the tsk): nothing special */
        sim_init(&s, 50);
        for (int k = 0; k < 40 && s.max_level == 0; k++) { run(&s, 2, k & 1 ? -1 : 1, 80); }
        if (mao_fiddle_score(&s.f, s.t) < FIDDLE_L2 - FIDDLE_HELD_MARGIN) {
            CHECK(mao_fiddle_quiet(&s.f, s.t + 1200) == MAO_FIDDLE_QUIET_NONE);
        }
        /* not yet stopped: nothing */
        CHECK(mao_fiddle_quiet(&s.f, s.t + 100) == MAO_FIDDLE_QUIET_NONE);

        /* after real annoyance, one clean deliberate move: MAO softens */
        sim_init(&s, 50);
        for (int k = 0; k < 6; k++) { run(&s, 13, k & 1 ? -8 : 8, 25); }
        CHECK(s.max_level >= MAO_FIDDLE_ANNOYED && s.max_level < MAO_FIDDLE_FED_UP);
        s.t += 1500;
        run(&s, 4, -1, 150);                                 /* slow, one way, stops short of the end */
        CHECK(mao_fiddle_quiet(&s.f, s.t + 1200) == MAO_FIDDLE_QUIET_CLEAN);
        /* a move that changes its mind is not clean */
        sim_init(&s, 50);
        for (int k = 0; k < 6; k++) { run(&s, 13, k & 1 ? -8 : 8, 25); }
        s.t += 1500;
        run(&s, 3, -1, 150); run(&s, 1, 1, 150);
        CHECK(mao_fiddle_quiet(&s.f, s.t + 1200) != MAO_FIDDLE_QUIET_CLEAN);
        /* a clean move with nothing to forgive is just a move */
        sim_init(&s, 50);
        run(&s, 4, -1, 150);
        CHECK(mao_fiddle_quiet(&s.f, s.t + 1200) == MAO_FIDDLE_QUIET_NONE);
        /* a single detent is not a deliberate setting */
        sim_init(&s, 50);
        for (int k = 0; k < 6; k++) { run(&s, 13, k & 1 ? -8 : 8, 25); }
        s.t += 1500;
        run(&s, 1, -1, 150);
        CHECK(mao_fiddle_quiet(&s.f, s.t + 1200) == MAO_FIDDLE_QUIET_NONE);
    }

    printf("fiddle: %d checks passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
