/*
 * MAO application: power states (brief section 31).
 *
 * The decision is the pure policy (mao_power_policy.h, host-tested); this
 * file feeds it once a second from the application's point of view:
 *   - activity = input or a presence percept (someone is here);
 *   - hold awake while the first encounter waits, a device view is open,
 *     the self-test or a stress run is going;
 *   - the USB console and USB power caps (no sleep while a host listens).
 * mao_power carries the state out (hooks: display, sense, haptics, IR) and
 * goes on from a long DROWSY to deep sleep by itself.
 *
 * Quiet wake: the deep-sleep housekeeping timer boots MAO with the screen
 * dark; if nobody shows up within CONFIG_MAO_POWER_QUIET_WAKE_S it goes
 * straight back to deep sleep, so the half-hourly check costs seconds, not
 * minutes of a lit screen.
 */
#include "mao_app_priv.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "mao_events.h"
#include "mao_power.h"
#include "mao_power_policy.h"
#include "mao_selftest.h"
#include "mao_system.h"

static const char *TAG = "MAO_APP";

#define TICK_PERIOD_US      (1000 * 1000)

#if CONFIG_MAO_POWER_AUTO
#define QUIET_WAKE_MS       ((uint32_t)CONFIG_MAO_POWER_QUIET_WAKE_S * 1000u)
#else
#define QUIET_WAKE_MS       8000u
#endif

static mao_policy_config_t s_cfg;
static esp_timer_handle_t s_timer;
static uint32_t s_last_activity_ms;
static uint32_t s_last_wake_ms;
static bool s_woke_idle;
static bool s_quiet;
static uint32_t s_quiet_deadline_ms;
static int s_requested = -1;               /* state asked for and not yet confirmed */
static mao_power_state_t s_state = MAO_POWER_ACTIVE;
static int64_t s_hold_until_us;

static uint32_t to_ms(int64_t us)
{
    return (uint32_t)(us / 1000);
}

static void tick_cb(void *arg)
{
    (void)arg;
    mao_event_post(MAO_EVENT_TICK, 0);
}

bool mao_app_power_init(bool quiet_wake, int64_t now_us)
{
    mao_power_policy_config(&s_cfg);
    s_last_activity_ms = to_ms(now_us);
    if (!s_cfg.enabled) {
        return false;   /* LCDkit, or automatic power states disabled */
    }
    const esp_timer_create_args_t args = { .callback = tick_cb, .name = "mao_app_tick" };
    if (esp_timer_create(&args, &s_timer) == ESP_OK) {
        esp_timer_start_periodic(s_timer, TICK_PERIOD_US);
    }
    if (quiet_wake) {
        s_quiet = true;
        s_quiet_deadline_ms = to_ms(now_us) + QUIET_WAKE_MS;
        ESP_LOGI(TAG, "housekeeping wake: dark, back to sleep in %u s unless someone is here",
                 (unsigned)(QUIET_WAKE_MS / 1000));
    }
    ESP_LOGI(TAG, "power policy: IDLE after %u s, DROWSY after %u s, deep sleep after %u min drowsy%s%s",
             (unsigned)(s_cfg.idle_after_ms / 1000), (unsigned)(s_cfg.drowsy_after_ms / 1000),
             (unsigned)(s_cfg.deep_after_ms / 60000), s_cfg.sleep_with_console ? "" : ", awake with a console",
             s_cfg.deep_on_usb ? "" : ", no deep sleep on USB power");
    return s_quiet;
}

bool mao_app_power_quiet(void)
{
    return s_quiet;
}

void mao_app_power_end_quiet(void)
{
    s_quiet = false;
}

void mao_app_power_hold(int64_t until_us)
{
    if (until_us > s_hold_until_us) {
        s_hold_until_us = until_us;
    }
}

static void request(mao_policy_state_t want)
{
    if ((int)want == s_requested || (mao_power_state_t)want == s_state) {
        return;
    }
    if (mao_power_request_state((mao_power_state_t)want) == ESP_OK) {
        s_requested = (int)want;
        ESP_LOGI(TAG, "power: %s -> %s", mao_policy_state_name((mao_policy_state_t)s_state),
                 mao_policy_state_name(want));
    }
}

void mao_app_power_activity(int64_t now_us)
{
    s_last_activity_ms = to_ms(now_us);
    s_woke_idle = false;
    if (s_cfg.enabled && s_state == MAO_POWER_IDLE) {
        request(MAO_POLICY_ACTIVE);   /* full sensor rates at once, not at the next tick */
    }
}

void mao_app_power_tick(int64_t now_us)
{
    if (!s_cfg.enabled || s_state == MAO_POWER_DEEP_SLEEP) {
        return;
    }
    const uint32_t now = to_ms(now_us);
    mao_power_status_t ps;
    mao_power_get_status(&ps);
    const bool console = mao_system_console_attached();

    if (s_quiet) {
        if ((int32_t)(now - s_quiet_deadline_ms) >= 0) {
            s_quiet = false;
            if (mao_policy_drowsy_to_deep(&s_cfg, s_cfg.deep_after_ms, ps.usb_present, console)) {
                ESP_LOGI(TAG, "housekeeping done, nobody around: back to deep sleep");
                mao_power_deep_sleep(0, MAO_SLEEP_REASON_IDLE);
                return;
            }
            /* Kept awake (USB, console): continue as if dozing. */
            s_woke_idle = true;
            s_last_wake_ms = now - s_cfg.redrowse_ms;
        }
        return;
    }

    const mao_view_t view = mao_state()->view;
    const mao_policy_input_t in = {
        .now_ms = now,
        .last_activity_ms = s_last_activity_ms,
        .last_wake_ms = s_last_wake_ms,
        .woke_idle = s_woke_idle,
        .usb_present = ps.usb_present,
        .console_attached = console,
        .hold_awake = view == MAO_VIEW_INTRO || view == MAO_VIEW_DEVICES || view == MAO_VIEW_DEVICE ||
                      mao_selftest_running() || now_us < s_hold_until_us,
    };
    request(mao_policy_decide(&s_cfg, &in));
}

/* MAO_EVENT_POWER_STATE: the state mao_power actually entered. */
mao_power_state_t mao_app_power_on_state(int32_t value, int64_t now_us)
{
    const mao_power_state_t prev = s_state;
    s_state = (mao_power_state_t)value;
    s_requested = -1;
    if (prev == MAO_POWER_DROWSY && s_state == MAO_POWER_ACTIVE) {
        /* Woken up. Until something real happens, this is only a peek. */
        s_woke_idle = true;
        s_last_wake_ms = to_ms(now_us);
    }
    return prev;
}
