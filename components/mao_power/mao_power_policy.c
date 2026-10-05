/*
 * MAO power policy (pure logic). See mao_power_policy.h.
 */
#include "mao_power_policy.h"

mao_policy_state_t mao_policy_decide(const mao_policy_config_t *cfg, const mao_policy_input_t *in)
{
    if (!cfg->enabled || in->hold_awake) {
        return MAO_POLICY_ACTIVE;
    }
    mao_policy_state_t want = MAO_POLICY_ACTIVE;
    if (in->woke_idle) {
        /* Woken by something that did not turn into company: look around
         * for a moment, then doze off again. */
        if (in->now_ms - in->last_wake_ms >= cfg->redrowse_ms) {
            want = MAO_POLICY_DROWSY;
        }
    } else {
        const uint32_t idle = in->now_ms - in->last_activity_ms;   /* wrap-safe */
        if (idle >= cfg->drowsy_after_ms) {
            want = MAO_POLICY_DROWSY;
        } else if (idle >= cfg->idle_after_ms) {
            want = MAO_POLICY_IDLE;
        }
    }
    /* Light sleep would cut the USB-Serial/JTAG link under a host's feet. */
    if (in->console_attached && !cfg->sleep_with_console && want > MAO_POLICY_IDLE) {
        want = MAO_POLICY_IDLE;
    }
    return want;
}

bool mao_policy_drowsy_to_deep(const mao_policy_config_t *cfg, uint32_t drowsy_ms, bool usb_present,
                               bool console_attached)
{
    if (!cfg->enabled || cfg->deep_after_ms == 0) {
        return false;
    }
    if (console_attached && !cfg->sleep_with_console) {
        return false;
    }
    if (usb_present && !cfg->deep_on_usb) {
        return false;
    }
    return drowsy_ms >= cfg->deep_after_ms;
}

const char *mao_policy_state_name(mao_policy_state_t state)
{
    switch (state) {
    case MAO_POLICY_ACTIVE:     return "ACTIVE";
    case MAO_POLICY_IDLE:       return "IDLE";
    case MAO_POLICY_DROWSY:     return "DROWSY";
    case MAO_POLICY_DEEP_SLEEP: return "DEEP_SLEEP";
    default:                    return "?";
    }
}

bool mao_policy_charge_pause(bool usb_present, bool paused, bool temp_valid, float temp_c)
{
    if (!usb_present || !temp_valid) {
        return false;
    }
    if (paused) {
        return temp_c > MAO_CHARGE_RESUME_C;
    }
    return temp_c >= MAO_CHARGE_PAUSE_C;
}

bool mao_policy_charging(const mao_charge_input_t *in)
{
    if (!in->usb_present || in->charge_paused) {
        return false;
    }
    if (!in->gauge_valid) {
        return true;
    }
    return in->rate_pct_per_h > MAO_CHARGE_RATE_MIN_PCT_H || in->soc_pct < 100.0f;
}
