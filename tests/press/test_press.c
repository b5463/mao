/*
 * Host tests for the press gesture (components/mao_input/mao_press.c): what
 * MAO hears from the switch and the dial, with the turn guard off (the
 * LCDkit, unchanged) and on (A1, the whole top presses).
 */
#include <stdio.h>
#include <string.h>
#include "mao_press.h"

static int s_fail, s_pass;
#define CHECK(cond) do { if (cond) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { P = MAO_EVENT_INPUT_PRESS, R = MAO_EVENT_INPUT_RELEASE, CL = MAO_EVENT_INPUT_CLICK,
       L = MAO_EVENT_INPUT_LONG_PRESS, DB = MAO_EVENT_INPUT_DOUBLE_CLICK };

/* What was heard, as a short list of event types. */
static int s_ev[32], s_n;
static void emit(mao_event_type_t type, int32_t value, void *ctx)
{
    (void)value; (void)ctx;
    if (s_n < 32) {
        s_ev[s_n++] = (int)type;
    }
}
static bool heard(const int *want, int n)
{
    bool ok = s_n == n && memcmp(s_ev, want, (size_t)n * sizeof(int)) == 0;
    if (!ok) {
        printf("  heard:");
        for (int i = 0; i < s_n; i++) {
            printf(" %d", s_ev[i]);
        }
        printf("\n");
    }
    s_n = 0;
    return ok;
}
#define HEARD(...) heard((const int[]) { __VA_ARGS__ }, (int)(sizeof((int[]) { __VA_ARGS__ }) / sizeof(int)))
#define NOTHING() heard(NULL, 0)

#define MS(x) ((int64_t)(x) * 1000)

static const mao_press_cfg_t OFF = { 0, 0 };
static const mao_press_cfg_t A1 = { 150, 250 };

static void fresh(mao_press_t *p, const mao_press_cfg_t *cfg)
{
    mao_press_init(p, cfg, false, emit, NULL);
    s_n = 0;
}

/* Run the timers up to t. */
static void run_to(mao_press_t *p, int64_t t)
{
    for (int i = 0; i < 8; i++) {
        const int64_t d = mao_press_deadline(p);
        if (d > t) {
            break;
        }
        mao_press_tick(p, d);
    }
    mao_press_tick(p, t);
}

/* Both configurations: the plain gestures behave the same. */
static void plain_gestures(const mao_press_cfg_t *cfg)
{
    mao_press_t p;

    /* a click */
    fresh(&p, cfg);
    mao_press_level(&p, MS(1000), true);
    CHECK(HEARD(P));
    run_to(&p, MS(1100));
    mao_press_level(&p, MS(1100), false);
    CHECK(HEARD(R, CL));

    /* a double click */
    mao_press_level(&p, MS(1300), true);
    mao_press_level(&p, MS(1380), false);
    CHECK(HEARD(P, R, CL, DB));

    /* a long press: LONG at 900 ms, no CLICK at the release */
    fresh(&p, cfg);
    mao_press_level(&p, MS(5000), true);
    run_to(&p, MS(5899));
    CHECK(HEARD(P));
    run_to(&p, MS(5900));
    CHECK(HEARD(L));
    mao_press_level(&p, MS(6200), false);
    CHECK(HEARD(R));

    /* hold, then turn (the options): PRESS / RELEASE frame it, no CLICK, no LONG */
    fresh(&p, cfg);
    mao_press_level(&p, MS(2000), true);
    CHECK(mao_press_detents(&p, MS(2500), 1, false) == 1);
    run_to(&p, MS(4000));
    mao_press_level(&p, MS(4000), false);
    CHECK(HEARD(P, R));

    /* the push nudge: one detent in the first 200 ms is not a turn and keeps the click */
    fresh(&p, cfg);
    mao_press_level(&p, MS(3000), true);
    CHECK(mao_press_detents(&p, MS(3050), 1, false) == 0);
    mao_press_level(&p, MS(3150), false);
    CHECK(HEARD(P, R, CL));

    /* ... two detents in that window are a turn */
    fresh(&p, cfg);
    mao_press_level(&p, MS(3000), true);
    CHECK(mao_press_detents(&p, MS(3050), 1, false) == 0);
    CHECK(mao_press_detents(&p, MS(3100), 1, false) == 2);
    mao_press_level(&p, MS(3400), false);
    CHECK(HEARD(P, R));

    /* turning with nothing pressed: plain turns */
    fresh(&p, cfg);
    CHECK(mao_press_detents(&p, MS(100), 1, false) == 1);
    CHECK(mao_press_detents(&p, MS(140), -2, false) == -2);
    CHECK(NOTHING());

    /* a deliberate press well after the last turn is a press */
    mao_press_level(&p, MS(900), true);
    mao_press_level(&p, MS(1000), false);
    CHECK(HEARD(P, R, CL));
}

int main(void)
{
    mao_press_t p;

    plain_gestures(&OFF);
    plain_gestures(&A1);

    /* --- guard off (LCDkit): a press straight after a detent is heard, as before */
    fresh(&p, &OFF);
    CHECK(mao_press_detents(&p, MS(1000), 1, false) == 1);
    mao_press_level(&p, MS(1040), true);
    mao_press_level(&p, MS(1120), false);
    CHECK(HEARD(P, R, CL));
    CHECK(p.turn_presses == 0);

    /* turned while the press was still debouncing: a hold-and-turn from the start */
    fresh(&p, &OFF);
    CHECK(mao_press_detents(&p, MS(1000), 2, true) == 2);
    mao_press_level(&p, MS(1010), true);
    run_to(&p, MS(2500));
    mao_press_level(&p, MS(2500), false);
    CHECK(HEARD(P, R));

    /* --- guard on (A1): pressure mid-turn is part of the turn */
    /* the top closes between detents and opens before the next one: nothing heard */
    fresh(&p, &A1);
    CHECK(mao_press_detents(&p, MS(1000), 1, false) == 1);
    mao_press_level(&p, MS(1060), true);
    mao_press_level(&p, MS(1110), false);
    CHECK(NOTHING());
    CHECK(p.turn_presses == 1);

    /* closed through several detents: they stay plain turns (no options), nothing heard */
    fresh(&p, &A1);
    CHECK(mao_press_detents(&p, MS(1000), 1, false) == 1);
    mao_press_level(&p, MS(1050), true);
    CHECK(mao_press_detents(&p, MS(1090), 1, false) == 1);
    CHECK(mao_press_detents(&p, MS(1130), 1, false) == 1);
    CHECK(mao_press_detents(&p, MS(1180), -1, false) == -1);
    run_to(&p, MS(1300));
    mao_press_level(&p, MS(1300), false);
    CHECK(NOTHING());

    /* a press that closes while still debouncing during the turn: plain turns too */
    fresh(&p, &A1);
    CHECK(mao_press_detents(&p, MS(1000), 1, true) == 1);
    mao_press_level(&p, MS(1015), true);
    CHECK(mao_press_detents(&p, MS(1060), 1, false) == 1);
    mao_press_level(&p, MS(1100), false);
    CHECK(NOTHING());

    /* turned, then pushed and held still: a press once the turn has settled */
    fresh(&p, &A1);
    CHECK(mao_press_detents(&p, MS(1000), 1, false) == 1);
    mao_press_level(&p, MS(1080), true);
    CHECK(mao_press_deadline(&p) == MS(1250));
    run_to(&p, MS(1249));
    CHECK(NOTHING());
    run_to(&p, MS(1250));
    CHECK(HEARD(P));
    mao_press_level(&p, MS(1400), false);
    CHECK(HEARD(R, CL));

    /* ... and held on: a long press, timed from when it was heard */
    fresh(&p, &A1);
    CHECK(mao_press_detents(&p, MS(1000), 1, false) == 1);
    mao_press_level(&p, MS(1080), true);
    run_to(&p, MS(2149));
    CHECK(HEARD(P));
    run_to(&p, MS(2150));
    CHECK(HEARD(L));
    mao_press_level(&p, MS(2300), false);
    CHECK(HEARD(R));

    /* ... and once heard, turning again is hold-and-turn (the options), as always */
    fresh(&p, &A1);
    CHECK(mao_press_detents(&p, MS(1000), 1, false) == 1);
    mao_press_level(&p, MS(1080), true);
    run_to(&p, MS(1250));
    CHECK(HEARD(P));
    CHECK(mao_press_detents(&p, MS(1600), 1, false) == 1);
    mao_press_level(&p, MS(2000), false);
    CHECK(HEARD(R));

    /* the guard is short: a press 150 ms after the last detent is heard at once */
    fresh(&p, &A1);
    CHECK(mao_press_detents(&p, MS(1000), 1, false) == 1);
    mao_press_level(&p, MS(1150), true);
    CHECK(HEARD(P));
    mao_press_level(&p, MS(1250), false);
    CHECK(HEARD(R, CL));
    CHECK(p.turn_presses == 0);

    /* a turn press does not count towards a double click */
    fresh(&p, &A1);
    mao_press_level(&p, MS(500), true);
    mao_press_level(&p, MS(560), false);
    CHECK(HEARD(P, R, CL));
    CHECK(mao_press_detents(&p, MS(600), 1, false) == 1);
    mao_press_level(&p, MS(650), true);
    mao_press_level(&p, MS(700), false);
    CHECK(NOTHING());
    mao_press_level(&p, MS(900), true);
    mao_press_level(&p, MS(950), false);
    CHECK(HEARD(P, R, CL));

    /* idle: no deadline */
    fresh(&p, &A1);
    CHECK(mao_press_deadline(&p) == INT64_MAX);

    printf("press: %d checks, %d failed\n", s_pass + s_fail, s_fail);
    return s_fail ? 1 : 0;
}
