/*
 * MAO battery: fuel gauge, charger and USB status, battery events, and the
 * charge temperature limit. (Was A0's mao_power; its ACTIVE / IDLE / DROWSY
 * state machine is gone: the M4.1 ladder in mao_app is MAO's only power
 * state machine, see mao_power.h.)
 *
 *   MAX17048 gauge (polled; ALRT not wired) + BQ25185 charger (STAT1 / STAT2,
 *   /CE) + VBUS_SENSE  ->  mao_battery task  ->  MAO events:
 *       USB_CONNECTED / USB_DISCONNECTED, CHARGING_STARTED / CHARGING_DONE,
 *       BATTERY_LOW (value = SOC %), BATTERY_CRITICAL (value = SOC %)
 *
 * Charge temperature limit: while USB is present the board temperature (a
 * source registered with mao_battery_set_temp_source(): mao_sense, the
 * ICM-42670-P die) is read every 10 s; charging pauses at >= 43 C and
 * resumes at <= 40 C (cell window 0-45 C). No temperature = charging left
 * enabled (the charger's TS input still protects the cell). While the chip
 * rests in light sleep, mao_app's power task calls mao_battery_poll() at
 * least every 10 s on USB, so the limit keeps running.
 *
 * Critical battery: on battery, CONFIG_MAO_BATTERY_CRITICAL_MV (provisional
 * 3550 mV from M5 Gate C, VERIFY AT BRING-UP) or SOC <= 2 %, three gauge
 * readings in a row: MAO_EVENT_BATTERY_CRITICAL. The application acts on it
 * through the power ladder (mao_app_power.c: a deep sleep only the press,
 * or a timer, ends).
 *
 * Boards without battery wiring (LCDkit): mao_battery_init() returns
 * ESP_ERR_NOT_SUPPORTED and every other call is a harmless no-op.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool gauge;                   /* fuel gauge answered */
    uint16_t voltage_mv;          /* cell voltage */
    float soc_pct;                /* state of charge */
    float rate_pct_per_h;         /* + charging, - discharging */
    bool usb_present;
    bool charging;                /* the charger's STAT lines (or the estimate without them) */
    bool charger_fault;           /* STAT says fault (recoverable or latched) */
    bool charge_paused;           /* the charge temperature limit (or the console) holds the charger off */
    bool temp_valid;              /* temp_c is a fresh reading (USB present, source answered) */
    float temp_c;                 /* board temperature used by the charge limit */
    bool low;
    bool critical;
    uint32_t updated_ms;          /* ms since boot of the last gauge reading */
} mao_battery_status_t;

/* Gauge + charger + USB monitoring, events, charge limit. Requires
 * mao_board_init(). ESP_ERR_NOT_SUPPORTED on boards without battery wiring. */
esp_err_t mao_battery_init(void);

bool mao_battery_available(void);
void mao_battery_get_status(mao_battery_status_t *out);
bool mao_battery_usb_present(void);

/* Housekeeping now, from any task (the light-sleep loop): USB and charger
 * lines, the charge limit, and the gauge if a reading is due. Never blocks
 * for long; a no-op without battery wiring. */
void mao_battery_poll(void);

/* Read the fuel gauge now (normally every 30 s, 5 s when low). */
esp_err_t mao_battery_refresh(void);

/* Diagnostics: the fuel gauge's VERSION register (MAX17048: 0x001x). */
esp_err_t mao_battery_gauge_version_get(uint16_t *version);

/* Board temperature for the charge limit, degrees C. Called from the
 * battery task or the light-sleep loop; must not block for long. */
typedef esp_err_t (*mao_battery_temp_source_t)(float *celsius);

/* Register the board-temperature source (mao_sense: the IMU die). May be
 * called before or after mao_battery_init(); NULL removes it. */
void mao_battery_set_temp_source(mao_battery_temp_source_t source);

#ifdef __cplusplus
}
#endif
