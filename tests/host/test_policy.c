/* Power policy (ACTIVE / IDLE / DROWSY / DEEP SLEEP) decisions. */
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

void suite_policy(void)
{
    RUN(test_progression);
    RUN(test_console_never_sleeps);
    RUN(test_usb_power_no_deep);
    RUN(test_hold_awake_and_disabled);
    RUN(test_redrowse_after_idle_wake);
    RUN(test_wraparound);
}
