/*
 * MAO battery policy: pure decisions, no ESP-IDF (host-tested in
 * tests/host: test_policy.c).
 *
 * Charge temperature limit (boards with caps.charge_control): the LiPo cell
 * may be charged at 0-45 C. While USB is present mao_battery reads the board
 * temperature (the IMU die) every ~10 s and mao_battery_charge_pause()
 * decides, with hysteresis, whether the charger is paused. Without a
 * temperature it never pauses: the charger's own TS window stays the limit.
 *
 * Charging estimate (boards without a charger status line):
 * mao_battery_charging_estimate(). The A1 has STAT1/STAT2 and does not need
 * it; it stays for boards that do and is tested.
 *
 * Critical battery: mao_battery_critical_reading() says whether one gauge
 * reading counts towards the critical-battery action (three in a row).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_CHARGE_PAUSE_C          43.0f   /* pause at or above (cell limit 45 C) */
#define MAO_CHARGE_RESUME_C         40.0f   /* resume at or below */
#define MAO_CHARGE_RATE_MIN_PCT_H   1.0f    /* gauge CRATE above this = the cell gains charge */

/* Should the charger be paused now? paused: what it is now. No USB: no
 * (the next plug-in starts from the hardware default). temp_valid false (no
 * sensor, a failed read, NaN): no, fail-safe to the hardware limit. */
bool mao_battery_charge_pause(bool usb_present, bool paused, bool temp_valid, float temp_c);

typedef struct {
    bool usb_present;
    bool charge_paused;            /* by mao_battery_charge_pause() */
    bool gauge_valid;              /* a fuel-gauge reading exists */
    float soc_pct;
    float rate_pct_per_h;          /* gauge CRATE, + = charging */
} mao_charge_input_t;

/* Charging, for a board without a charger status line: USB present, not
 * paused, and the gauge either sees the cell gain charge (CRATE above
 * MAO_CHARGE_RATE_MIN_PCT_H) or the cell is not full (SOC < 100 %). Without
 * a gauge it is USB present and not paused. */
bool mao_battery_charging_estimate(const mao_charge_input_t *in);

/* One gauge reading on battery: does it count as critical (cell below
 * critical_mv, or SOC at or below critical_pct)? On USB: never. */
bool mao_battery_critical_reading(bool usb_present, uint16_t voltage_mv, float soc_pct, uint16_t critical_mv,
                                  float critical_pct);

#ifdef __cplusplus
}
#endif
