/*
 * Host tests for the hold gesture (components/mao_app/mao_hold.c): every
 * sequence a finger can make on a device page, and what it must mean.
 */
#include <stdio.h>
#include "mao_hold.h"

static int s_fail, s_pass;
#define CHECK(cond) do { if (cond) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { P = MAO_EVENT_INPUT_PRESS, R = MAO_EVENT_INPUT_RELEASE, CW = MAO_EVENT_INPUT_CW, CCW = MAO_EVENT_INPUT_CCW,
       L = MAO_EVENT_INPUT_LONG_PRESS, CL = MAO_EVENT_INPUT_CLICK, DB = MAO_EVENT_INPUT_DOUBLE_CLICK };

/* Feed a sequence; return the result of the last event, count the ticks. */
static mao_hold_t seq(mao_hold_state_t *h, const int *ev, int n, bool right, bool left, int *ticks)
{
    mao_hold_t r = MAO_HOLD_PASS;
    int t = 0;
    for (int i = 0; i < n; i++) {
        bool moved = false;
        r = mao_hold_step(h, (mao_event_type_t)ev[i], right, left, &moved);
        t += moved;
    }
    if (ticks) {
        *ticks = t;
    }
    return r;
}
#define SEQ(h, r, l, t, ...) seq(h, (const int[]) { __VA_ARGS__ }, (int)(sizeof((int[]) { __VA_ARGS__ }) / sizeof(int)), r, l, t)

int main(void)
{
    mao_hold_state_t h;
    int ticks;

    /* a plain press is the page's: nothing for the hold */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, &ticks, P) == MAO_HOLD_PASS);
    CHECK(SEQ(&h, true, true, &ticks, R) == MAO_HOLD_PASS && ticks == 0);
    CHECK(SEQ(&h, true, true, NULL, CL) == MAO_HOLD_PASS);
    CHECK(SEQ(&h, true, true, NULL, DB) == MAO_HOLD_PASS);

    /* a plain turn is the page's (a light's brightness) */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, &ticks, CW) == MAO_HOLD_PASS && ticks == 0);

    /* hold, turn right, let go: the right option (CONNECT) */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, &ticks, P, CW) == MAO_HOLD_EATEN && ticks == 1);
    CHECK(h.menu && h.sel == 0);
    CHECK(SEQ(&h, true, true, NULL, R) == MAO_HOLD_RIGHT);

    /* hold, turn left, let go: the left option (FORGET) */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, NULL, P, CCW, R) == MAO_HOLD_LEFT);

    /* turned there and back: the middle is BACK */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, &ticks, P, CW, CCW, R) == MAO_HOLD_BACK && ticks == 2);

    /* it stops at its ends: one step back from any spin to the right is BACK */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, &ticks, P, CW, CW, CW, CW, CW) == MAO_HOLD_EATEN && ticks == 1);
    CHECK(SEQ(&h, true, true, NULL, CCW, R) == MAO_HOLD_BACK);
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, NULL, P, CCW, CCW, CCW, CW, CW, R) == MAO_HOLD_RIGHT);

    /* a long press brings the options up in the middle: letting go is BACK */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, &ticks, P, L) == MAO_HOLD_EATEN && ticks == 1);
    CHECK(h.menu && h.sel == -1);
    CHECK(SEQ(&h, true, true, NULL, R) == MAO_HOLD_BACK);
    /* ... and turning after it still chooses */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, NULL, P, L, CW, R) == MAO_HOLD_RIGHT);
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, NULL, P, L, CCW, R) == MAO_HOLD_LEFT);
    /* a second long press event changes nothing */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, &ticks, P, L, L) == MAO_HOLD_EATEN && ticks == 1);

    /* a long press nobody pressed for (console) is plain BACK */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, true, NULL, L) == MAO_HOLD_BACK);
    CHECK(!h.menu);

    /* a missing option cannot be chosen: turning towards it stays on BACK */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, false, true, &ticks, P, CW, CW, R) == MAO_HOLD_BACK && ticks == 0);
    mao_hold_reset(&h);
    CHECK(SEQ(&h, true, false, NULL, P, CCW, CCW, R) == MAO_HOLD_BACK);
    mao_hold_reset(&h);
    CHECK(SEQ(&h, false, true, NULL, P, CCW, R) == MAO_HOLD_LEFT);   /* NEW page: only FORGET... */
    mao_hold_reset(&h);
    CHECK(SEQ(&h, false, false, NULL, P, CW, CCW, CCW, R) == MAO_HOLD_BACK);   /* ...or nothing */

    /* after letting go everything is the page's again */
    mao_hold_reset(&h);
    (void)SEQ(&h, true, true, NULL, P, CW, R);
    CHECK(!h.down && !h.menu && h.sel == -1);
    CHECK(SEQ(&h, true, true, NULL, CW) == MAO_HOLD_PASS);
    CHECK(SEQ(&h, true, true, NULL, R) == MAO_HOLD_PASS);

    /* a new press starts clean even if the last release never came */
    mao_hold_reset(&h);
    (void)SEQ(&h, true, true, NULL, P, CW);
    CHECK(SEQ(&h, true, true, NULL, P, R) == MAO_HOLD_PASS);

    printf("hold gesture: %d checks passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
