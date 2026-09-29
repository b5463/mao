/*
 * Host tests for MAO's power ladder (components/mao_app/mao_power.c): when it
 * rests, how deeply, what keeps it awake, what the first touch means, and
 * whether a boot is a cold start or MAO waking from its own deep sleep.
 */
#include <stdio.h>
#include "mao_power.h"

static int s_fail, s_pass;
#define CHECK(cond) do { if (cond) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    mao_power_t p;
    const mao_pwr_busy_t idle = { 0 };

    /* ACTIVE -> SLEEP DISPLAY at the sleep time, not before */
    mao_power_init(&p);
    CHECK(mao_power_idle(&p, 0, &idle) == MAO_PWR_DO_NOTHING);
    CHECK(mao_power_idle(&p, MAO_PWR_SLEEP_MS - 1, &idle) == MAO_PWR_DO_NOTHING && p.st == MAO_PWR_ACTIVE);
    CHECK(mao_power_idle(&p, MAO_PWR_SLEEP_MS, &idle) == MAO_PWR_DO_SLEEP_DISPLAY && p.st == MAO_PWR_SLEEP_DISPLAY);
    CHECK(mao_power_idle(&p, MAO_PWR_SLEEP_MS + 1000, &idle) == MAO_PWR_DO_NOTHING);   /* once */
    /* SLEEP DISPLAY -> NIGHT at the night time */
    CHECK(mao_power_idle(&p, MAO_PWR_NIGHT_MS - 1, &idle) == MAO_PWR_DO_NOTHING);
    CHECK(mao_power_idle(&p, MAO_PWR_NIGHT_MS, &idle) == MAO_PWR_DO_NIGHT && p.st == MAO_PWR_NIGHT);
    CHECK(mao_power_idle(&p, 10u * MAO_PWR_NIGHT_MS, &idle) == MAO_PWR_DO_NOTHING);  /* NIGHT is the bottom */
    /* input wakes from any resting state, once */
    CHECK(mao_power_input(&p) == MAO_PWR_DO_WAKE && p.st == MAO_PWR_ACTIVE);
    CHECK(mao_power_input(&p) == MAO_PWR_DO_NOTHING);
    mao_power_init(&p);
    mao_power_idle(&p, MAO_PWR_SLEEP_MS, &idle);
    CHECK(mao_power_input(&p) == MAO_PWR_DO_WAKE);
    /* the night never comes straight from ACTIVE (the sleeping screen is shown first) */
    mao_power_init(&p);
    CHECK(mao_power_idle(&p, MAO_PWR_NIGHT_MS, &idle) == MAO_PWR_DO_SLEEP_DISPLAY);
    CHECK(mao_power_idle(&p, MAO_PWR_NIGHT_MS, &idle) == MAO_PWR_DO_NIGHT);

    /* busy: nothing rests, however long idle */
    const mao_pwr_busy_t busy[4] = { { .pairing = true }, { .forgetting = true }, { .action_pending = true },
                                     { .transfer = true } };
    for (int k = 0; k < 4; k++) {
        mao_power_init(&p);
        CHECK(mao_power_busy(&busy[k]));
        CHECK(mao_power_idle(&p, 10u * MAO_PWR_NIGHT_MS, &busy[k]) == MAO_PWR_DO_NOTHING && p.st == MAO_PWR_ACTIVE);
        CHECK(!mao_power_deep_allowed(&busy[k]));
        /* and a resting MAO does not go deeper while busy */
        mao_power_idle(&p, MAO_PWR_SLEEP_MS, &idle);
        CHECK(mao_power_idle(&p, MAO_PWR_NIGHT_MS, &busy[k]) == MAO_PWR_DO_NOTHING && p.st == MAO_PWR_SLEEP_DISPLAY);
    }
    CHECK(!mao_power_busy(&idle) && !mao_power_busy(NULL) && mao_power_deep_allowed(&idle));

    /* --- the first touch after a knob wake --- */
    mao_wake_eat_t w;
    /* a quick tap woke it (already released at wake): its click is eaten, the next press acts */
    mao_wake_eat_reset(&w);
    mao_wake_eat_knob(&w, false, 1000);
    CHECK(mao_wake_eat(&w, MAO_IN_RELEASE, false, 1010));
    CHECK(mao_wake_eat(&w, MAO_IN_CLICK, false, 1011));
    CHECK(!mao_wake_eat(&w, MAO_IN_PRESS, false, 2000));
    CHECK(!mao_wake_eat(&w, MAO_IN_RELEASE, false, 2100));
    CHECK(!mao_wake_eat(&w, MAO_IN_CLICK, false, 2101));
    /* woken by a press still held: held, turned while held, released, its click - all eaten */
    mao_wake_eat_reset(&w);
    mao_wake_eat_knob(&w, true, 1000);
    CHECK(mao_wake_eat(&w, MAO_IN_PRESS, false, 1005));
    CHECK(mao_wake_eat(&w, MAO_IN_TURN, false, 1600));     /* long after the quiet: still the same press */
    CHECK(mao_wake_eat(&w, MAO_IN_LONG, false, 1900));
    CHECK(mao_wake_eat(&w, MAO_IN_RELEASE, false, 2000));
    CHECK(mao_wake_eat(&w, MAO_IN_CLICK, false, 2001));
    CHECK(!mao_wake_eat(&w, MAO_IN_TURN, false, 3000));   /* the next touch acts */
    /* woken by a turn: the turn's detents in the quiet window are eaten, later ones act */
    mao_wake_eat_reset(&w);
    mao_wake_eat_knob(&w, false, 1000);
    CHECK(mao_wake_eat(&w, MAO_IN_TURN, false, 1020));
    CHECK(mao_wake_eat(&w, MAO_IN_TURN, false, 1000 + MAO_PWR_WAKE_QUIET_MS - 1));
    CHECK(!mao_wake_eat(&w, MAO_IN_TURN, false, 1000 + MAO_PWR_WAKE_QUIET_MS + 50));
    /* a click long after an eaten release is a new click */
    mao_wake_eat_reset(&w);
    mao_wake_eat_knob(&w, true, 1000);
    mao_wake_eat(&w, MAO_IN_RELEASE, false, 1500);
    CHECK(!mao_wake_eat(&w, MAO_IN_CLICK, false, 1500 + MAO_WAKE_CLICK_MS + 1000));
    /* a dimmed MAO (no chip wake): the first press is eaten whole, the first turn is eaten */
    mao_wake_eat_reset(&w);
    CHECK(mao_wake_eat(&w, MAO_IN_PRESS, true, 5000));
    CHECK(mao_wake_eat(&w, MAO_IN_RELEASE, false, 5100));
    CHECK(mao_wake_eat(&w, MAO_IN_CLICK, false, 5101));
    CHECK(!mao_wake_eat(&w, MAO_IN_CLICK, false, 6000));
    mao_wake_eat_reset(&w);
    CHECK(mao_wake_eat(&w, MAO_IN_TURN, true, 5000));
    CHECK(!mao_wake_eat(&w, MAO_IN_TURN, false, 5050));
    /* awake and nothing armed: nothing is eaten */
    mao_wake_eat_reset(&w);
    CHECK(!mao_wake_eat(&w, MAO_IN_PRESS, false, 0) && !mao_wake_eat(&w, MAO_IN_CLICK, false, 10));

    /* --- boots --- */
    CHECK(mao_power_boot_kind(false, 0) == MAO_BOOT_COLD);
    CHECK(mao_power_boot_kind(false, MAO_PWR_RTC_MAGIC) == MAO_BOOT_COLD);   /* a reset keeps RTC memory: not a wake */
    CHECK(mao_power_boot_kind(true, 0) == MAO_BOOT_COLD);                    /* a wake without our marker */
    CHECK(mao_power_boot_kind(true, 0x12345678u) == MAO_BOOT_COLD);
    CHECK(mao_power_boot_kind(true, MAO_PWR_RTC_MAGIC) == MAO_BOOT_FROM_DEEP);

    /* the timings the board relies on */
    CHECK(MAO_PWR_NIGHT_MS > MAO_PWR_SLEEP_MS && MAO_PWR_SLEEP_PCT > 0 && MAO_PWR_SLEEP_PCT <= 10);

    printf("power: %d checks passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
