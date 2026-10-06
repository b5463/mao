/* Battery policy (mao_battery_policy.c): the charge temperature limit, the
 * charging estimate, and what counts as a critical reading. (A0's
 * ACTIVE / IDLE / DROWSY / DEEP decisions are gone: the M4.1 ladder in
 * mao_app is tested in tests/power.) */
#include <math.h>
#include "mini_test.h"
#include "mao_battery_policy.h"

/* ---- Charging ------------------------------------------------------------ */

static void test_charge_pause_hysteresis(void)
{
    /* Rising: enabled up to 42.9 C, paused from 43.0 C. */
    CHECK(!mao_battery_charge_pause(true, false, true, 25.0f));
    CHECK(!mao_battery_charge_pause(true, false, true, 42.9f));
    CHECK(mao_battery_charge_pause(true, false, true, 43.0f));
    CHECK(mao_battery_charge_pause(true, false, true, 60.0f));
    /* Falling: stays paused down to 40.1 C, resumes at 40.0 C. */
    CHECK(mao_battery_charge_pause(true, true, true, 42.0f));
    CHECK(mao_battery_charge_pause(true, true, true, 40.1f));
    CHECK(!mao_battery_charge_pause(true, true, true, 40.0f));
    CHECK(!mao_battery_charge_pause(true, true, true, 20.0f));
    /* Inside the band the decision keeps the current state. */
    CHECK(!mao_battery_charge_pause(true, false, true, 41.5f));
    CHECK(mao_battery_charge_pause(true, true, true, 41.5f));
}

static void test_charge_pause_fail_safe(void)
{
    /* No temperature: never paused, even if it was (the hardware NTC window
     * stays the limit). No USB: back to the hardware default. */
    CHECK(!mao_battery_charge_pause(true, true, false, 99.0f));
    CHECK(!mao_battery_charge_pause(true, false, false, 99.0f));
    CHECK(!mao_battery_charge_pause(false, true, true, 60.0f));
    CHECK(!mao_battery_charge_pause(false, false, true, 60.0f));
    /* A NaN reading from a broken source never pauses. */
    CHECK(!mao_battery_charge_pause(true, false, true, NAN));
    CHECK(!mao_battery_charge_pause(true, true, true, NAN));
}

static void test_charge_thermal_sequence(void)
{
    /* A charger warming the board, then cooling: one pause, one resume. */
    static const float kTemps[] = { 30, 38, 41, 42.9f, 43.2f, 44, 42, 40.5f, 40.0f, 39, 42, 42.9f };
    static const bool kPaused[] = { 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 };
    bool paused = false;
    int transitions = 0;
    for (unsigned i = 0; i < sizeof(kTemps) / sizeof(kTemps[0]); i++) {
        const bool next = mao_battery_charge_pause(true, paused, true, kTemps[i]);
        transitions += next != paused;
        paused = next;
        CHECK_EQ(paused, kPaused[i]);
    }
    CHECK_EQ(transitions, 2);
}

static void test_charging_estimate(void)
{
    mao_charge_input_t in = {
        .usb_present = true, .charge_paused = false, .gauge_valid = true, .soc_pct = 60.0f, .rate_pct_per_h = 35.0f,
    };
    CHECK(mao_battery_charging_estimate(&in));                 /* CC phase */
    in.rate_pct_per_h = -4.0f;
    CHECK(mao_battery_charging_estimate(&in));                 /* just plugged in: CRATE has not turned yet */
    in.soc_pct = 100.0f;
    in.rate_pct_per_h = 0.0f;
    CHECK(!mao_battery_charging_estimate(&in));                /* full and idle: done */
    in.rate_pct_per_h = 1.0f;
    CHECK(!mao_battery_charging_estimate(&in));                /* noise at the threshold is not charging */
    in.rate_pct_per_h = 1.3f;
    CHECK(mao_battery_charging_estimate(&in));                 /* a top-up past 100 % */
    in.soc_pct = 99.9f;
    in.rate_pct_per_h = 0.0f;
    CHECK(mao_battery_charging_estimate(&in));                 /* not full yet */
    in.charge_paused = true;
    CHECK(!mao_battery_charging_estimate(&in));                /* held off by the temperature limit */
    in.charge_paused = false;
    in.usb_present = false;
    in.rate_pct_per_h = 20.0f;
    CHECK(!mao_battery_charging_estimate(&in));                /* no USB, a stale positive CRATE */
    in.usb_present = true;
    in.gauge_valid = false;
    in.soc_pct = 100.0f;
    in.rate_pct_per_h = -10.0f;
    CHECK(mao_battery_charging_estimate(&in));                 /* no gauge: USB and enabled */
    in.charge_paused = true;
    CHECK(!mao_battery_charging_estimate(&in));
}

/* ---- Critical battery ------------------------------------------------------ */

static void test_critical_reading(void)
{
    /* Provisional floor 3550 mV (M5 Gate C), SOC 2 %. */
    CHECK(!mao_battery_critical_reading(false, 3700, 40.0f, 3550, 2.0f));
    CHECK(!mao_battery_critical_reading(false, 3550, 10.0f, 3550, 2.0f));   /* at the floor: not below */
    CHECK(mao_battery_critical_reading(false, 3549, 10.0f, 3550, 2.0f));
    CHECK(mao_battery_critical_reading(false, 3800, 2.0f, 3550, 2.0f));     /* SOC at the limit */
    CHECK(mao_battery_critical_reading(false, 3800, 0.5f, 3550, 2.0f));
    /* On USB the cell is being charged: never critical, whatever it reads. */
    CHECK(!mao_battery_critical_reading(true, 3000, 0.0f, 3550, 2.0f));
}

void suite_policy(void)
{
    RUN(test_critical_reading);
    RUN(test_charge_pause_hysteresis);
    RUN(test_charge_pause_fail_safe);
    RUN(test_charge_thermal_sequence);
    RUN(test_charging_estimate);
}
