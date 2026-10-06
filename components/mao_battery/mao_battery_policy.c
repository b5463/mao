/*
 * MAO battery policy (pure logic). See mao_battery_policy.h.
 */
#include "mao_battery_policy.h"

bool mao_battery_charge_pause(bool usb_present, bool paused, bool temp_valid, float temp_c)
{
    if (!usb_present || !temp_valid || temp_c != temp_c) {   /* NaN fails safe too */
        return false;
    }
    if (paused) {
        return temp_c > MAO_CHARGE_RESUME_C;
    }
    return temp_c >= MAO_CHARGE_PAUSE_C;
}

bool mao_battery_charging_estimate(const mao_charge_input_t *in)
{
    if (!in->usb_present || in->charge_paused) {
        return false;
    }
    if (!in->gauge_valid) {
        return true;
    }
    return in->rate_pct_per_h > MAO_CHARGE_RATE_MIN_PCT_H || in->soc_pct < 100.0f;
}

bool mao_battery_critical_reading(bool usb_present, uint16_t voltage_mv, float soc_pct, uint16_t critical_mv,
                                  float critical_pct)
{
    if (usb_present) {
        return false;
    }
    return voltage_mv < critical_mv || soc_pct <= critical_pct;
}
