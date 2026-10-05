/* AW9364 backlight: percent -> step, and the edge plan between any two
 * steps, checked against a model of the driver that refuses to count past
 * edge 16 (the datasheet does not say whether edge 17 wraps). */
#include "mini_test.h"
#include "aw9364_dimming.h"

static void test_percent_to_step(void)
{
    CHECK_EQ(aw9364_step_from_percent(0), 0);
    CHECK_EQ(aw9364_step_from_percent(1), 16);      /* dimmest step, never off */
    CHECK_EQ(aw9364_step_from_percent(6), 16);      /* 6 % <= 1/16 */
    CHECK_EQ(aw9364_step_from_percent(7), 15);
    CHECK_EQ(aw9364_step_from_percent(12), 15);     /* 12 % <= 2/16 (12.5 %) */
    CHECK_EQ(aw9364_step_from_percent(13), 14);
    CHECK_EQ(aw9364_step_from_percent(25), 13);     /* 4/16 */
    CHECK_EQ(aw9364_step_from_percent(50), 9);      /* 8/16 = 10 mA */
    CHECK_EQ(aw9364_step_from_percent(51), 8);
    CHECK_EQ(aw9364_step_from_percent(75), 5);      /* 12/16 */
    CHECK_EQ(aw9364_step_from_percent(93), 2);      /* 93 % <= 15/16 (93.75 %) */
    CHECK_EQ(aw9364_step_from_percent(94), 1);      /* above 15/16: full current */
    CHECK_EQ(aw9364_step_from_percent(100), 1);     /* 20 mA per channel, 40 mA total */
    CHECK_EQ(aw9364_step_from_percent(101), 1);
    CHECK_EQ(aw9364_step_from_percent(255), 1);
}

static void test_step_never_below_request(void)
{
    /* Every percent maps to a step in range whose current is at least the
     * requested share of 20 mA and less than one step (1.25 mA) above it;
     * brighter requests never give a dimmer step. */
    int bad = 0;
    uint8_t prev = 17;
    for (int p = 1; p <= 100; p++) {
        const uint8_t step = aw9364_step_from_percent((uint8_t)p);
        const uint32_t ua = aw9364_step_current_ua(step);
        const uint32_t want_ua = 200u * (uint32_t)p;   /* p % of 20 mA */
        if (step < 1 || step > 16 || ua < want_ua || ua >= want_ua + 1250u || step > prev) {
            bad++;
        }
        prev = step;
    }
    CHECK_EQ(bad, 0);
}

static void test_step_current_table(void)
{
    /* Datasheet table 1: edge n -> (17 - n) x 1.25 mA. */
    CHECK_EQ(aw9364_step_current_ua(0), 0);
    CHECK_EQ(aw9364_step_current_ua(1), 20000);
    CHECK_EQ(aw9364_step_current_ua(2), 18750);
    CHECK_EQ(aw9364_step_current_ua(9), 10000);
    CHECK_EQ(aw9364_step_current_ua(12), 6250);
    CHECK_EQ(aw9364_step_current_ua(16), 1250);
    CHECK_EQ(aw9364_step_current_ua(17), 0);
}

/* The driver as the datasheet describes it, minus the unknown wrap. */
typedef struct {
    uint8_t step;        /* 0 = off */
    bool overflow;       /* an edge 17 was asked for */
} model_t;

static void model_apply(model_t *m, aw9364_plan_t plan)
{
    if (plan.shutdown) {
        m->step = 0;     /* EN low >= 3 ms > TOFF */
    }
    for (uint8_t i = 0; i < plan.edges; i++) {
        if (m->step == 16) {
            m->overflow = true;
        } else {
            m->step++;   /* from off: the enable edge is step 1 */
        }
    }
}

static void test_plan_all_transitions(void)
{
    int wrong = 0, overflow = 0, restarts_dimmer = 0;
    for (uint8_t from = 0; from <= 16; from++) {
        for (uint8_t to = 0; to <= 16; to++) {
            model_t m = { .step = from };
            const aw9364_plan_t plan = aw9364_plan(from, to);
            model_apply(&m, plan);
            wrong += m.step != to;
            overflow += m.overflow;
            /* Dimming from an on state never blinks the panel dark. */
            restarts_dimmer += from != 0 && to > from && plan.shutdown;
        }
    }
    CHECK_EQ(wrong, 0);
    CHECK_EQ(overflow, 0);
    CHECK_EQ(restarts_dimmer, 0);
}

static void test_plan_cases(void)
{
    aw9364_plan_t p = aw9364_plan(0, 1);
    CHECK(p.shutdown);
    CHECK_EQ(p.edges, 1);                 /* just the enable edge */
    p = aw9364_plan(0, 16);
    CHECK_EQ(p.edges, 16);
    p = aw9364_plan(1, 16);               /* dimmer: 15 more edges */
    CHECK(!p.shutdown);
    CHECK_EQ(p.edges, 15);
    p = aw9364_plan(9, 8);                /* one step brighter: restart, 8 edges */
    CHECK(p.shutdown);
    CHECK_EQ(p.edges, 8);
    p = aw9364_plan(5, 0);                /* off */
    CHECK(p.shutdown);
    CHECK_EQ(p.edges, 0);
    p = aw9364_plan(7, 7);                /* unchanged: nothing on the wire */
    CHECK(!p.shutdown);
    CHECK_EQ(p.edges, 0);
    p = aw9364_plan(0, 0);
    CHECK(!p.shutdown);
    CHECK_EQ(p.edges, 0);
    p = aw9364_plan(40, 3);               /* out-of-range steps clamp to 16 */
    CHECK(p.shutdown);
    CHECK_EQ(p.edges, 3);
}

void suite_backlight(void)
{
    RUN(test_percent_to_step);
    RUN(test_step_never_below_request);
    RUN(test_step_current_table);
    RUN(test_plan_all_transitions);
    RUN(test_plan_cases);
}
