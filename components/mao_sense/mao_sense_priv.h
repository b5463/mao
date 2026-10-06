/* Private to mao_sense: the sensor drivers and the shared publish path. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "mao_sense.h"

/* Sense-task notification bits (set from ISRs). */
#define SENSE_BIT_IMU           (1u << 0)
#define SENSE_BIT_TOF           (1u << 1)
#define SENSE_BIT_MODE          (1u << 3)

/* mao_sense.c */
void sense_publish(const mao_obs_t *obs);
uint32_t sense_now_ms(void);

/* IMU: ICM-42670-P (mao_sense_imu.c) */
esp_err_t sense_imu_init(void);
esp_err_t sense_imu_mode(mao_sense_level_t level);
esp_err_t sense_imu_gyro(bool on);
/* Vectors plus the events since the last call (reading INT_STATUS2 clears
 * the latched INT1). */
esp_err_t sense_imu_sample(mao_obs_imu_t *out);
esp_err_t sense_imu_id(uint32_t *id);
/* Die temperature; ESP_ERR_INVALID_STATE while powered off. */
esp_err_t sense_imu_temperature(float *celsius);

/* ToF: VL53L4CD through ST's ULD (mao_sense_tof.c) */
esp_err_t sense_tof_init(void);
esp_err_t sense_tof_mode(mao_sense_level_t level);
/* Reads a result if one is ready (and clears the sensor interrupt). */
esp_err_t sense_tof_read(mao_obs_tof_t *out, bool *ready);
uint32_t sense_tof_period_ms(void);
esp_err_t sense_tof_id(uint32_t *id);
