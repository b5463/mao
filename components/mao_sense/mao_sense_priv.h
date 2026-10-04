/* Private to mao_sense: the sensor drivers and the shared publish path. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "mao_sense.h"

/* How awake the sensors are; follows the power state. */
typedef enum {
    SENSE_MODE_ACTIVE = 0,
    SENSE_MODE_IDLE,            /* awake, lower rates */
    SENSE_MODE_DROWSY,          /* wake-on-motion, ToF approach threshold, mic off */
    SENSE_MODE_SLEEP,           /* deep sleep: wake-on-motion only, ToF / ALS / mic off */
    SENSE_MODE_OFF,             /* critical battery: everything off */
} sense_mode_t;

/* Sense-task notification bits (set from ISRs and callbacks). */
#define SENSE_BIT_IMU           (1u << 0)
#define SENSE_BIT_TOF           (1u << 1)
#define SENSE_BIT_TOUCH         (1u << 2)
#define SENSE_BIT_MODE          (1u << 3)

/* mao_sense.c */
void sense_publish(const mao_obs_t *obs);
uint32_t sense_now_ms(void);
/* Notify the sense task from an ISR or driver callback; returns true when a
 * higher-priority task was woken (the caller yields / reports it). */
bool sense_notify_from_isr(uint32_t bits);

/* IMU: LSM6DSOX / LSM6DS3TR-C (mao_sense_imu.c) */
esp_err_t sense_imu_init(void);
esp_err_t sense_imu_mode(sense_mode_t mode);
esp_err_t sense_imu_gyro(bool on);
/* Vectors plus the embedded events latched since the last call (reading
 * the source registers clears the latched interrupt lines). */
esp_err_t sense_imu_sample(mao_obs_imu_t *out);
const char *sense_imu_part(void);
esp_err_t sense_imu_id(uint32_t *id);

/* ToF: VL53L4CD through ST's ULD (mao_sense_tof.c) */
esp_err_t sense_tof_init(void);
esp_err_t sense_tof_mode(sense_mode_t mode);
/* Reads a result if one is ready (and clears the sensor interrupt). */
esp_err_t sense_tof_read(mao_obs_tof_t *out, bool *ready);
uint32_t sense_tof_period_ms(void);
esp_err_t sense_tof_id(uint32_t *id);
/* XSHUT was pulled low behind the driver's back (expander reset): boot the
 * sensor again into the given mode. */
esp_err_t sense_tof_recover(sense_mode_t mode);

/* ALS: OPT3004 (mao_sense_als.c) */
esp_err_t sense_als_init(void);
esp_err_t sense_als_mode(sense_mode_t mode);
/* Reads lux, re-centres the interrupt window and clears the INT latch. */
esp_err_t sense_als_read(mao_obs_als_t *out);
esp_err_t sense_als_id(uint32_t *id);

/* Touch: ESP32-S3 touch sensor v2 (mao_sense_touch.c) */
esp_err_t sense_touch_init(void);
esp_err_t sense_touch_read(mao_obs_touch_t *out);

/* Mic: PDM level meter task (mao_sense_mic.c) */
esp_err_t sense_mic_init(void);
/* Start / stop the mic clock; returns once applied (bounded wait). */
void sense_mic_run(bool run);
