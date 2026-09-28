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

    printf("fiddle: %d checks passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
