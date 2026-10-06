/* Private to mao_haptics: DRV2605L register-level driver. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Auto-calibration results (what NVS keeps). */
typedef struct {
    uint8_t comp;           /* A_CAL_COMP (0x18) */
    uint8_t bemf;           /* A_CAL_BEMF (0x19) */
    uint8_t bemf_gain;      /* FEEDBACK_CONTROL[1:0] */
} mao_drv_cal_t;

/* Add the device, check DEVICE_ID with EN briefly high, leave it off. */
esp_err_t mao_drv_init(void);

/* EN high + full register setup (the driver is reconfigured on every
 * power-up rather than trusting retained registers), or EN low. */
esp_err_t mao_drv_power(bool on);
bool mao_drv_is_powered(void);

/* Calibration values used by the next mao_drv_power(true). */
void mao_drv_set_cal(const mao_drv_cal_t *cal);

/* Load up to 8 waveform-sequencer slots (effect ids, or 0x80|n = wait
 * n x 10 ms) and fire GO. Interrupts anything playing. */
esp_err_t mao_drv_play(const uint8_t *seq, size_t len);
esp_err_t mao_drv_busy(bool *busy);
esp_err_t mao_drv_stop(void);

/* Run auto-calibration (about 1.2 s, the actuator buzzes). Leaves the
 * driver powered and in standby. lra_period (optional) gets LRA_PERIOD
 * (98.46 us per LSB) measured during the calibration. */
esp_err_t mao_drv_calibrate(mao_drv_cal_t *out, uint8_t *lra_period);

/* STATUS[7:5] device id (powers the driver if needed; leaves it as it was). */
esp_err_t mao_drv_device_id(uint8_t *id);
