/* Power policy (ACTIVE / IDLE / DROWSY / DEEP SLEEP) decisions, the charge
 * temperature limit and the charging estimate. */
#include <math.h>
#include "mini_test.h"
#include "mao_power_policy.h"

static mao_policy_config_t cfg(void)
{
    return (mao_policy_config_t) {
        .enabled = true,
        .idle_after_ms = 30000,
        .drowsy_after_ms = 180000,
        .deep_after_ms = 30u * 60u * 1000u,
        .redrowse_ms = 20000,
        .sleep_with_console = false,
        .deep_on_usb = false,
    };
}

static mao_policy_state_t at(const mao_policy_config_t *c, uint32_t idle_ms, bool usb, bool console)
{
    const mao_policy_input_t in = {
        .now_ms = 5000000,
        .last_activity_ms = 5000000 - idle_ms,
        .usb_present = usb,
        .console_attached = console,
    };
    return mao_policy_decide(c, &in);
}

static void test_progression(void)
{
    const mao_policy_config_t c = cfg();
    CHECK_EQ(at(&c, 0, false, false), MAO_POLICY_ACTIVE);
    CHECK_EQ(at(&c, 29999, false, false), MAO_POLICY_ACTIVE);
    CHECK_EQ(at(&c, 30000, false, false), MAO_POLICY_IDLE);
    CHECK_EQ(at(&c, 179999, false, false), MAO_POLICY_IDLE);
    CHECK_EQ(at(&c, 180000, false, false), MAO_POLICY_DROWSY);
    CHECK_EQ(at(&c, 4000000, false, false), MAO_POLICY_DROWSY);   /* deep sleep comes from DROWSY */
}

static void test_console_never_sleeps(void)
{
    mao_policy_config_t c = cfg();
    CHECK_EQ(at(&c, 4000000, true, true), MAO_POLICY_IDLE);
    CHECK(!mao_policy_drowsy_to_deep(&c, 99999999, false, true));
    c.sleep_with_console = true;
    CHECK_EQ(at(&c, 4000000, true, true), MAO_POLICY_DROWSY);
}

static void test_usb_power_no_deep(void)
{
    mao_policy_config_t c = cfg();
    CHECK_EQ(at(&c, 200000, true, false), MAO_POLICY_DROWSY);      /* on a charger: may doze */
    CHECK(!mao_policy_drowsy_to_deep(&c, 99999999, true, false));
    CHECK(!mao_policy_drowsy_to_deep(&c, 30u * 60u * 1000u - 1, false, false));
    CHECK(mao_policy_drowsy_to_deep(&c, 30u * 60u * 1000u, false, false));
    c.deep_on_usb = true;
    CHECK(mao_policy_drowsy_to_deep(&c, 30u * 60u * 1000u, true, false));
    c.deep_after_ms = 0;
    CHECK(!mao_policy_drowsy_to_deep(&c, 99999999, false, false));
}

static void test_hold_awake_and_disabled(void)
{
    mao_policy_config_t c = cfg();
    mao_policy_input_t in = { .now_ms = 1000000, .last_activity_ms = 0, .hold_awake = true };
    CHECK_EQ(mao_policy_decide(&c, &in), MAO_POLICY_ACTIVE);
    in.hold_awake = false;
    CHECK_EQ(mao_policy_decide(&c, &in), MAO_POLICY_DROWSY);
    c.enabled = false;
    CHECK_EQ(mao_policy_decide(&c, &in), MAO_POLICY_ACTIVE);
    CHECK(!mao_policy_drowsy_to_deep(&c, 99999999, false, false));
}

static void test_redrowse_after_idle_wake(void)
{
    const mao_policy_config_t c = cfg();
    mao_policy_input_t in = {
        .now_ms = 1000000,
        .last_activity_ms = 100,          /* long ago */
        .last_wake_ms = 1000000,
        .woke_idle = true,
    };
    CHECK_EQ(mao_policy_decide(&c, &in), MAO_POLICY_ACTIVE);   /* look around first */
    in.now_ms += 19999;
    CHECK_EQ(mao_policy_decide(&c, &in), MAO_POLICY_ACTIVE);
    in.now_ms += 1;
    CHECK_EQ(mao_policy_decide(&c, &in), MAO_POLICY_DROWSY);
    /* Someone was there after all: normal timing from that moment. */
    in.woke_idle = false;
    in.last_activity_ms = in.now_ms;
    CHECK_EQ(mao_policy_decide(&c, &in), MAO_POLICY_ACTIVE);
}

static void test_wraparound(void)
{
    const mao_policy_config_t c = cfg();
    const mao_policy_input_t in = { .now_ms = 10000, .last_activity_ms = 0xFFFFFFFFu - 50000u };
    CHECK_EQ(mao_policy_decide(&c, &in), MAO_POLICY_IDLE);   /* 60 s idle across the wrap */
}

/* ---- Charging ------------------------------------------------------------ */

static void test_charge_pause_hysteresis(void)
{
    /* Rising: enabled up to 42.9 C, paused from 43.0 C. */
    CHECK(!mao_policy_charge_pause(true, false, true, 25.0f));
    CHECK(!mao_policy_charge_pause(true, false, true, 42.9f));
    CHECK(mao_policy_charge_pause(true, false, true, 43.0f));
    CHECK(mao_policy_charge_pause(true, false, true, 60.0f));
    /* Falling: stays paused down to 40.1 C, resumes at 40.0 C. */
    CHECK(mao_policy_charge_pause(true, true, true, 42.0f));
    CHECK(mao_policy_charge_pause(true, true, true, 40.1f));
    CHECK(!mao_policy_charge_pause(true, true, true, 40.0f));
    CHECK(!mao_policy_charge_pause(true, true, true, 20.0f));
    /* Inside the band the decision keeps the current state. */
    CHECK(!mao_policy_charge_pause(true, false, true, 41.5f));
    CHECK(mao_policy_charge_pause(true, true, true, 41.5f));
}

static void test_charge_pause_fail_safe(void)
{
    /* No temperature: never paused, even if it was (the hardware NTC window
     * stays the limit). No USB: back to the hardware default. */
    CHECK(!mao_policy_charge_pause(true, true, false, 99.0f));
    CHECK(!mao_policy_charge_pause(true, false, false, 99.0f));
    CHECK(!mao_policy_charge_pause(false, true, true, 60.0f));
    CHECK(!mao_policy_charge_pause(false, false, true, 60.0f));
    /* A NaN reading from a broken source never pauses. */
    CHECK(!mao_policy_charge_pause(true, false, true, NAN));
    CHECK(!mao_policy_charge_pause(true, true, true, NAN));
}

static void test_charge_thermal_sequence(void)
{
    /* A charger warming the board, then cooling: one pause, one resume. */
    static const float kTemps[] = { 30, 38, 41, 42.9f, 43.2f, 44, 42, 40.5f, 40.0f, 39, 42, 42.9f };
    static const bool kPaused[] = { 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 };
    bool paused = false;
    int transitions = 0;
    for (unsigned i = 0; i < sizeof(kTemps) / sizeof(kTemps[0]); i++) {
        const bool next = mao_policy_charge_pause(true, paused, true, kTemps[i]);
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
    CHECK(mao_policy_charging(&in));                 /* CC phase */
    in.rate_pct_per_h = -4.0f;
    CHECK(mao_policy_charging(&in));                 /* just plugged in: CRATE has not turned yet */
    in.soc_pct = 100.0f;
    in.rate_pct_per_h = 0.0f;
    CHECK(!mao_policy_charging(&in));                /* full and idle: done */
    in.rate_pct_per_h = 1.0f;
    CHECK(!mao_policy_charging(&in));                /* noise at the threshold is not charging */
    in.rate_pct_per_h = 1.3f;
    CHECK(mao_policy_charging(&in));                 /* a top-up past 100 % */
    in.soc_pct = 99.9f;
    in.rate_pct_per_h = 0.0f;
    CHECK(mao_policy_charging(&in));                 /* not full yet */
    in.charge_paused = true;
    CHECK(!mao_policy_charging(&in));                /* held off by the temperature limit */
    in.charge_paused = false;
    in.usb_present = false;
    in.rate_pct_per_h = 20.0f;
    CHECK(!mao_policy_charging(&in));                /* no USB, a stale positive CRATE */
    in.usb_present = true;
    in.gauge_valid = false;
    in.soc_pct = 100.0f;
    in.rate_pct_per_h = -10.0f;
    CHECK(mao_policy_charging(&in));                 /* no gauge: USB and enabled */
    in.charge_paused = true;
    CHECK(!mao_policy_charging(&in));
}

void suite_policy(void)
{
    RUN(test_progression);
    RUN(test_console_never_sleeps);
    RUN(test_usb_power_no_deep);
    RUN(test_hold_awake_and_disabled);
    RUN(test_redrowse_after_idle_wake);
    RUN(test_wraparound);
    RUN(test_charge_pause_hysteresis);
    RUN(test_charge_pause_fail_safe);
    RUN(test_charge_thermal_sequence);
    RUN(test_charging_estimate);
}
