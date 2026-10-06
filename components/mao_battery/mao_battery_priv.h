/* Private to mao_battery: MAX17048 fuel-gauge driver. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint16_t voltage_mv;
    float soc_pct;
    float rate_pct_per_h;
} mao_gauge_reading_t;

/* Status register flags (high byte of STATUS, 0x1A). */
#define MAO_GAUGE_ST_RI     (1u << 8)    /* reset indicator (power-on / POR) */
#define MAO_GAUGE_ST_VH     (1u << 9)    /* voltage high alert */
#define MAO_GAUGE_ST_VL     (1u << 10)   /* voltage low alert */
#define MAO_GAUGE_ST_VR     (1u << 11)   /* voltage reset */
#define MAO_GAUGE_ST_HD     (1u << 12)   /* SOC low (empty alert threshold) */
#define MAO_GAUGE_ST_SC     (1u << 13)   /* 1 % SOC change */

/* Probe, check VERSION, program alert thresholds and hibernation. */
esp_err_t mao_battery_gauge_init(void);
esp_err_t mao_battery_gauge_read(mao_gauge_reading_t *out);
esp_err_t mao_battery_gauge_version(uint16_t *version);

/* Read STATUS and clear the alert flags plus CONFIG.ALRT. *flags gets the
 * flags found. */
esp_err_t mao_battery_gauge_take_alert(uint16_t *flags);
