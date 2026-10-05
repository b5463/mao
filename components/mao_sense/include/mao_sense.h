/*
 * MAO sense: raw perception inputs as small, timestamped observations.
 *
 *   IMU    LSM6DSOX (or LSM6DS3TR-C): accel / gyro vectors, embedded events
 *          (activity, tap, double tap, 6D orientation, free-fall)
 *   ToF    VL53L4CD: distance, range status, signal
 *   ALS    OPT3004: lux
 *   touch  four ESP32-S3 capacitive zones: raw, baseline, normalised delta,
 *          touched
 *   mic    PDM microphone: RMS and peak (dBFS) per 32 ms block, onset flag
 *
 * Observations are facts, not interpretation: nothing here decides what a
 * tap or an approaching hand means. A later perception layer subscribes and
 * does that; the character still never reads hardware.
 *
 * Delivery: subscribers get a callback or a queue item per observation,
 * rate-limited per subscriber and kind (min_interval_ms). Observations that
 * carry an event (IMU event flags, touch change, mic onset, ToF threshold)
 * always pass the rate limit. Callbacks run in the sense tasks: copy what
 * you need and return.
 *
 * Every sensor is optional: a sensor that is not fitted or did not answer
 * is simply absent from mao_sense_available() and never reported.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "mao_board.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_OBS_IMU = 0,
    MAO_OBS_TOF,
    MAO_OBS_ALS,
    MAO_OBS_TOUCH,
    MAO_OBS_MIC,
    MAO_OBS_KIND_COUNT,
} mao_obs_kind_t;

#define MAO_OBS_MASK(kind)      (1u << (kind))
#define MAO_OBS_ALL             ((1u << MAO_OBS_KIND_COUNT) - 1u)

typedef struct {
    float x;
    float y;
    float z;
} mao_vec3_t;

/* IMU embedded-function events since the previous IMU observation. */
#define MAO_IMU_EV_ACTIVITY     (1u << 0)   /* wake-up threshold crossed */
#define MAO_IMU_EV_TAP          (1u << 1)
#define MAO_IMU_EV_DOUBLE_TAP   (1u << 2)
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
    float lux;
} mao_obs_als_t;

typedef struct {
    uint32_t raw;               /* smoothed channel reading */
    uint32_t baseline;          /* the driver's benchmark */
    float delta;                /* (raw - baseline) normalised: 0 = idle, 1 = firm touch (clamped) */
    bool touched;
} mao_obs_touch_zone_t;

typedef struct {
    mao_obs_touch_zone_t zone[MAO_TOUCH_ZONE_COUNT];
    uint8_t changed;            /* bit per zone whose touched state just changed */
} mao_obs_touch_t;

typedef struct {
    float rms_dbfs;
    float peak_dbfs;
    bool onset;                 /* level jumped against the recent floor */
} mao_obs_mic_t;

typedef struct {
    mao_obs_kind_t kind;
    uint32_t time_ms;           /* ms since boot when observed */
    bool event;                 /* carries an event (bypassed rate limiting) */
    union {
        mao_obs_imu_t imu;
        mao_obs_tof_t tof;
        mao_obs_als_t als;
        mao_obs_touch_t touch;
        mao_obs_mic_t mic;
    };
} mao_obs_t;

typedef void (*mao_sense_cb_t)(const mao_obs_t *obs, void *ctx);

/* Bring up every fitted sensor that answers, start the sense tasks.
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

/* Gyro on (104 Hz) / off. Off by default: the accelerometer and the
 * embedded functions cover attention-level motion. */
esp_err_t mao_sense_imu_gyro(bool on);

/* Microphone on / off (clock and supply), outside the power states: used
 * by the self-test. ESP_ERR_NOT_SUPPORTED without a mic. */
esp_err_t mao_sense_mic_enable(bool on);

/* Read a sensor's identity register(s) now (diagnostics / factory test):
 *   IMU  WHO_AM_I (0x6C LSM6DSOX, 0x6A LSM6DS3TR-C)
 *   ToF  model id (0xEBAA)
 *   ALS  manufacturer << 16 | device (0x54493001)
 * ESP_ERR_NOT_SUPPORTED for kinds without an id or sensors not running. */
esp_err_t mao_sense_identify(mao_obs_kind_t kind, uint32_t *id);

/* IMU die temperature in C, read now under the sense device lock (any task).
 * A board-temperature estimate: typical 256 LSB / C, offset +-15 C per the
 * LSM6DSOX datasheet. mao_power uses it for the charge temperature limit.
 * ESP_ERR_NOT_SUPPORTED without a running IMU, ESP_ERR_INVALID_STATE while
 * it is powered down (critical battery). */
esp_err_t mao_sense_imu_temperature(float *celsius);

const char *mao_obs_kind_name(mao_obs_kind_t kind);

#ifdef __cplusplus
}
#endif
