/*
 * MAO sense: raw perception inputs as small, timestamped observations.
 *
 *   IMU    ICM-42670-P (0x68): accel / gyro vectors, events (wake-on-motion
 *          activity in the sensor; 6D orientation and free-fall derived from
 *          the samples here: the part has no tap detector)
 *   ToF    VL53L4CD: distance, range status, signal
 *
 * (A0 also had an ambient-light sensor, four touch zones and a PDM mic; the
 * A1 has none of them.)
 *
 * Observations are facts, not interpretation: nothing here decides what a
 * movement or an approaching hand means. mao_perception subscribes and does
 * that; the character never reads hardware.
 *
 * Delivery: subscribers get a callback or a queue item per observation,
 * rate-limited per subscriber and kind (min_interval_ms). Observations that
 * carry an event (IMU event flags, ToF threshold) always pass the rate
 * limit. Callbacks run in the sense task: copy what you need and return.
 *
 * Every sensor is optional: a sensor that is not fitted or did not answer is
 * simply absent from mao_sense_available() and never reported.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_OBS_IMU = 0,
    MAO_OBS_TOF,
    MAO_OBS_KIND_COUNT,
} mao_obs_kind_t;

#define MAO_OBS_MASK(kind)      (1u << (kind))
#define MAO_OBS_ALL             ((1u << MAO_OBS_KIND_COUNT) - 1u)

typedef struct {
    float x;
    float y;
    float z;
} mao_vec3_t;

/* IMU events since the previous IMU observation. */
#define MAO_IMU_EV_ACTIVITY     (1u << 0)   /* wake-on-motion threshold crossed */
#define MAO_IMU_EV_TAP          (1u << 1)   /* (never on the ICM-42670-P) */
#define MAO_IMU_EV_DOUBLE_TAP   (1u << 2)   /* (never on the ICM-42670-P) */
#define MAO_IMU_EV_ORIENTATION  (1u << 3)   /* 6D position changed */
#define MAO_IMU_EV_FREE_FALL    (1u << 4)

/* 6D position: which sensor axis points up (gravity reaction). */
typedef enum {
    MAO_ORIENT_UNKNOWN = 0,
    MAO_ORIENT_X_UP,
    MAO_ORIENT_X_DOWN,
    MAO_ORIENT_Y_UP,
    MAO_ORIENT_Y_DOWN,
    MAO_ORIENT_Z_UP,
    MAO_ORIENT_Z_DOWN,
} mao_orientation_t;

typedef struct {
    mao_vec3_t accel_g;
    mao_vec3_t gyro_dps;
    bool gyro_valid;            /* gyro is off unless asked for */
    uint8_t events;             /* MAO_IMU_EV_* */
    mao_orientation_t orientation;
} mao_obs_imu_t;

typedef struct {
    uint16_t distance_mm;
    uint8_t range_status;       /* VL53L4CD status: 0 = valid */
    uint16_t signal_kcps;
    uint16_t sigma_mm;
    bool threshold;             /* from the low-power approach threshold */
} mao_obs_tof_t;

typedef struct {
    mao_obs_kind_t kind;
    uint32_t time_ms;           /* ms since boot when observed */
    bool event;                 /* carries an event (bypassed rate limiting) */
    union {
        mao_obs_imu_t imu;
        mao_obs_tof_t tof;
    };
} mao_obs_t;

typedef void (*mao_sense_cb_t)(const mao_obs_t *obs, void *ctx);

/* Bring up every fitted sensor that answers, start the sense task.
 * ESP_ERR_NOT_SUPPORTED on boards without sensors. Individual sensors are
 * reported on the boot log; a missing one never fails init. */
esp_err_t mao_sense_init(void);

/* MAO_OBS_MASK bits of the sensors that are running. */
uint32_t mao_sense_available(void);

/* kinds: MAO_OBS_MASK bits. min_interval_ms: per kind; 0 = everything. */
esp_err_t mao_sense_subscribe(uint32_t kinds, uint32_t min_interval_ms, mao_sense_cb_t cb, void *ctx);

/* Same, delivering mao_obs_t items to a queue (never blocks; a full queue
 * drops the observation). */
esp_err_t mao_sense_subscribe_queue(uint32_t kinds, uint32_t min_interval_ms, QueueHandle_t queue);

/* Latest observation of one kind; false if none yet. */
bool mao_sense_latest(mao_obs_kind_t kind, mao_obs_t *out);

/* Gyro on (100 Hz) / off. Off by default. */
esp_err_t mao_sense_imu_gyro(bool on);

/* How awake the sensors are (mao_app's power ladder decides):
 *   AWAKE   IMU 100 Hz low-noise, sampled every 100 ms; ToF ranging 200 ms
 *   REST    light sleep: IMU low-power 25 Hz with wake-on-motion (latched,
 *           not a wake source of the ladder); ToF off
 *   DEEP    deep sleep next: as REST with INT1 cleared, so IMU INT1 can wake
 *           the chip (ext1); ToF off
 *   OFF     critical battery: IMU and ToF off (no motion wake)
 * Synchronous: the sensors are in the new mode when it returns. */
typedef enum {
    MAO_SENSE_AWAKE = 0,
    MAO_SENSE_REST,
    MAO_SENSE_DEEP,
    MAO_SENSE_OFF,
} mao_sense_level_t;
esp_err_t mao_sense_set_level(mao_sense_level_t level);

/* Read a sensor's identity register(s) now (diagnostics / factory test):
 *   IMU  WHO_AM_I (0x67 ICM-42670-P)
 *   ToF  model id (0xEBAA)
 * ESP_ERR_NOT_SUPPORTED for sensors not running. */
esp_err_t mao_sense_identify(mao_obs_kind_t kind, uint32_t *id);

/* IMU die temperature in C, read now under the sense device lock (any task):
 * TEMP_DATA / 128 + 25. A board-temperature estimate (VERIFY AT BRING-UP
 * against a thermocouple); mao_battery uses it for the charge limit.
 * ESP_ERR_NOT_SUPPORTED without a running IMU, ESP_ERR_INVALID_STATE while
 * it is powered off. */
esp_err_t mao_sense_imu_temperature(float *celsius);

const char *mao_obs_kind_name(mao_obs_kind_t kind);

#ifdef __cplusplus
}
#endif
