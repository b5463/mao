/*
 * IMU: ST LSM6DSOX at 0x6A (SA0 = GND); the pin-compatible LSM6DS3TR-C is
 * accepted as a fallback (WHO_AM_I 0x6C vs 0x6A).
 *
 * Accelerometer 104 Hz in normal (not high-performance) mode, +-4 g; gyro
 * off unless asked for. The embedded functions do the event detection in
 * the sensor, so the CPU only reads when something happened:
 *   INT1: wake-up (activity), single tap, double tap  -> also the deep-sleep
 *         wake line (ext1 ANY_LOW)
 *   INT2: 6D orientation, free-fall
 * Interrupts are latched (LIR) and active low, push-pull (CTRL3_C
 * H_LACTIVE = 1, PP_OD = 0); reading WAKE_UP_SRC / TAP_SRC / D6D_SRC clears
 * them. In sleep the accelerometer drops to 12.5 Hz low-power with only
 * wake-up routed to INT1.
 *
 * The two parts share every register used here except the tap enables:
 * LSM6DSOX has TAP_CFG0/1/2 at 0x56..0x58, LSM6DS3TR-C a single TAP_CFG at
 * 0x58. VERIFY AT BRING-UP: tap / double-tap thresholds and timing at
 * 104 Hz (ST's tap examples run at 417 Hz).
 */
#include "mao_sense_priv.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mao_board.h"

static const char *TAG = "MAO_SENSE";

#define REG_WHO_AM_I        0x0F
#define REG_CTRL1_XL        0x10
#define REG_CTRL2_G         0x11
#define REG_CTRL3_C         0x12
#define REG_CTRL6_C         0x15
#define REG_CTRL7_G         0x16
#define REG_CTRL9_XL        0x18
#define REG_WAKE_UP_SRC     0x1B     /* then TAP_SRC 0x1C, D6D_SRC 0x1D */
#define REG_OUT_TEMP_L      0x20     /* then OUT_TEMP_H 0x21 */
#define REG_OUTX_L_G        0x22
#define REG_OUTX_L_A        0x28
#define REG_DSOX_TAP_CFG0   0x56
#define REG_DSOX_TAP_CFG1   0x57
#define REG_DSOX_TAP_CFG2   0x58
#define REG_DS3_TAP_CFG     0x58
#define REG_TAP_THS_6D      0x59
#define REG_INT_DUR2        0x5A
#define REG_WAKE_UP_THS     0x5B
#define REG_WAKE_UP_DUR     0x5C
#define REG_FREE_FALL       0x5D
#define REG_MD1_CFG         0x5E
#define REG_MD2_CFG         0x5F

#define WHO_LSM6DSOX        0x6C
#define WHO_LSM6DS3TRC      0x6A

/* CTRL3_C: BDU | H_LACTIVE (active low) | IF_INC; PP_OD = 0 (push-pull). */
#define CTRL3_C_VALUE       0x64
#define CTRL3_C_SW_RESET    0x01
#define CTRL6_C_XL_HM_OFF   0x10     /* XL_HM_MODE = 1: low-power / normal modes */
#define CTRL7_G_HM_OFF      0x80     /* G_HM_MODE = 1 */
#define CTRL9_XL_I3C_OFF    0x02     /* LSM6DSOX only: I3C disabled */

#define ODR_104HZ           0x40
#define ODR_12HZ5           0x10
#define FS_XL_4G            0x08
#define GYRO_104HZ_250DPS   0x40

#define ACC_G_PER_LSB       0.000122f    /* +-4 g */
#define GYRO_DPS_PER_LSB    0.00875f     /* 250 dps */
/* Both parts: 256 LSB / C, 0 = 25 C (typical; the LSM6DSOX offset is
 * specified only as +-15 C, so this is a board-temperature estimate). */
#define TEMP_LSB_PER_C      256.0f
#define TEMP_ZERO_C         25.0f

/* Thresholds at +-4 g (VERIFY AT BRING-UP). */
#define TAP_THS             6            /* 6 x FS/32 = 750 mg */
#define SIXD_THS_60DEG      (2u << 5)
/* INT_DUR2 at 104 Hz: DUR 2 (2 x 32/ODR = 615 ms between taps), QUIET 1
 * (38 ms), SHOCK 1 (77 ms). */
#define INT_DUR2_VALUE      ((2u << 4) | (1u << 2) | 1u)
#define WK_THS              2            /* 2 x FS/64 = 125 mg */
#define SINGLE_DOUBLE_TAP   0x80
#define FREE_FALL_VALUE     ((6u << 3) | 3u)   /* 6 samples (58 ms), 312 mg */

#define MD_SINGLE_TAP       0x40
#define MD_WU               0x20
#define MD_FF               0x10
#define MD_DOUBLE_TAP       0x08
#define MD_6D               0x04

#define I2C_TIMEOUT_MS      50

static i2c_master_dev_handle_t s_dev;
static bool s_dsox;
static bool s_gyro;
static bool s_powered_down;
static mao_orientation_t s_orientation;

static esp_err_t wr(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t rd(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
}

const char *sense_imu_part(void)
{
    return s_dsox ? "LSM6DSOX" : "LSM6DS3TR-C";
}

static esp_err_t write_tap_enable(bool taps)
{
    if (s_dsox) {
        /* TAP_CFG0: INT_CLR_ON_READ | TAP_X/Y/Z_EN | LIR. */
        ESP_RETURN_ON_ERROR(wr(REG_DSOX_TAP_CFG0, taps ? 0x4F : 0x41), TAG, "tap_cfg0");
        ESP_RETURN_ON_ERROR(wr(REG_DSOX_TAP_CFG1, TAP_THS), TAG, "tap_cfg1");
        /* TAP_CFG2: INTERRUPTS_ENABLE (also needed for wake-up / 6D / FF). */
        return wr(REG_DSOX_TAP_CFG2, 0x80 | TAP_THS);
    }
    /* TAP_CFG: INTERRUPTS_ENABLE | TAP_X/Y/Z_EN | LIR. */
    return wr(REG_DS3_TAP_CFG, taps ? 0x8F : 0x81);
}

static esp_err_t clear_sources(void)
{
    uint8_t src[3];
    return rd(REG_WAKE_UP_SRC, src, sizeof(src));
}

esp_err_t sense_imu_mode(sense_mode_t mode)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "imu not ready");
    switch (mode) {
    case SENSE_MODE_ACTIVE:
    case SENSE_MODE_IDLE:
        ESP_RETURN_ON_ERROR(wr(REG_CTRL1_XL, ODR_104HZ | FS_XL_4G), TAG, "ctrl1");
        ESP_RETURN_ON_ERROR(wr(REG_CTRL2_G, s_gyro ? GYRO_104HZ_250DPS : 0x00), TAG, "ctrl2");
        ESP_RETURN_ON_ERROR(write_tap_enable(true), TAG, "tap");
        ESP_RETURN_ON_ERROR(wr(REG_TAP_THS_6D, SIXD_THS_60DEG | TAP_THS), TAG, "6d");
        ESP_RETURN_ON_ERROR(wr(REG_INT_DUR2, INT_DUR2_VALUE), TAG, "dur2");
        ESP_RETURN_ON_ERROR(wr(REG_WAKE_UP_THS, SINGLE_DOUBLE_TAP | WK_THS), TAG, "wu ths");
        ESP_RETURN_ON_ERROR(wr(REG_WAKE_UP_DUR, 0x00), TAG, "wu dur");
        ESP_RETURN_ON_ERROR(wr(REG_FREE_FALL, FREE_FALL_VALUE), TAG, "ff");
        ESP_RETURN_ON_ERROR(wr(REG_MD1_CFG, MD_WU | MD_SINGLE_TAP | MD_DOUBLE_TAP), TAG, "md1");
        ESP_RETURN_ON_ERROR(wr(REG_MD2_CFG, MD_6D | MD_FF), TAG, "md2");
        s_powered_down = false;
        break;
    case SENSE_MODE_DROWSY:
    case SENSE_MODE_SLEEP:
        /* Wake-on-motion only: 12.5 Hz low-power, nothing else routed. */
        ESP_RETURN_ON_ERROR(wr(REG_MD1_CFG, MD_WU), TAG, "md1");
        ESP_RETURN_ON_ERROR(wr(REG_MD2_CFG, 0x00), TAG, "md2");
        ESP_RETURN_ON_ERROR(wr(REG_CTRL2_G, 0x00), TAG, "gyro off");
        ESP_RETURN_ON_ERROR(write_tap_enable(false), TAG, "tap");
        ESP_RETURN_ON_ERROR(wr(REG_WAKE_UP_THS, WK_THS), TAG, "wu ths");
        ESP_RETURN_ON_ERROR(wr(REG_CTRL1_XL, ODR_12HZ5 | FS_XL_4G), TAG, "ctrl1");
        s_powered_down = false;
        break;
    case SENSE_MODE_OFF:
    default:
        ESP_RETURN_ON_ERROR(wr(REG_MD1_CFG, 0x00), TAG, "md1");
        ESP_RETURN_ON_ERROR(wr(REG_MD2_CFG, 0x00), TAG, "md2");
        ESP_RETURN_ON_ERROR(wr(REG_CTRL2_G, 0x00), TAG, "gyro off");
        ESP_RETURN_ON_ERROR(wr(REG_CTRL1_XL, 0x00), TAG, "power down");
        s_powered_down = true;
        break;
    }
    /* Start from released INT lines: a stale latch would mask the next
     * event or wake the chip at once. */
    return clear_sources();
}

esp_err_t sense_imu_gyro(bool on)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "imu not ready");
    s_gyro = on;
    if (s_powered_down) {
        return ESP_OK;
    }
    return wr(REG_CTRL2_G, on ? GYRO_104HZ_250DPS : 0x00);
}

static mao_orientation_t decode_6d(uint8_t d6d)
{
    if (d6d & 0x20) {
        return MAO_ORIENT_Z_UP;       /* ZH */
    }
    if (d6d & 0x10) {
        return MAO_ORIENT_Z_DOWN;     /* ZL */
    }
    if (d6d & 0x08) {
        return MAO_ORIENT_Y_UP;       /* YH */
    }
    if (d6d & 0x04) {
        return MAO_ORIENT_Y_DOWN;     /* YL */
    }
    if (d6d & 0x02) {
        return MAO_ORIENT_X_UP;       /* XH */
    }
    if (d6d & 0x01) {
        return MAO_ORIENT_X_DOWN;     /* XL */
    }
    return MAO_ORIENT_UNKNOWN;
}

static float axis(const uint8_t *p, float scale)
{
    return (float)(int16_t)(p[0] | (p[1] << 8)) * scale;
}

esp_err_t sense_imu_sample(mao_obs_imu_t *out)
{
    ESP_RETURN_ON_FALSE(s_dev && out, ESP_ERR_INVALID_STATE, TAG, "imu not ready");
    *out = (mao_obs_imu_t) { 0 };

    uint8_t src[3];   /* WAKE_UP_SRC, TAP_SRC, D6D_SRC */
    ESP_RETURN_ON_ERROR(rd(REG_WAKE_UP_SRC, src, sizeof(src)), TAG, "sources");
    if (src[0] & 0x08) {
        out->events |= MAO_IMU_EV_ACTIVITY;      /* WU_IA */
    }
    if (src[0] & 0x20) {
        out->events |= MAO_IMU_EV_FREE_FALL;     /* FF_IA */
    }
    if (src[1] & 0x20) {
        out->events |= MAO_IMU_EV_TAP;           /* SINGLE_TAP */
    }
    if (src[1] & 0x10) {
        out->events |= MAO_IMU_EV_DOUBLE_TAP;    /* DOUBLE_TAP */
    }
    if (src[2] & 0x40) {                         /* D6D_IA */
        const mao_orientation_t o = decode_6d(src[2]);
        if (o != MAO_ORIENT_UNKNOWN && o != s_orientation) {
            s_orientation = o;
            out->events |= MAO_IMU_EV_ORIENTATION;
        }
    }
    out->orientation = s_orientation;

    if (s_powered_down) {
        return ESP_OK;
    }
    uint8_t raw[12];
    if (s_gyro) {
        ESP_RETURN_ON_ERROR(rd(REG_OUTX_L_G, raw, 12), TAG, "gyro+accel");
        out->gyro_dps = (mao_vec3_t) { axis(&raw[0], GYRO_DPS_PER_LSB), axis(&raw[2], GYRO_DPS_PER_LSB),
                                       axis(&raw[4], GYRO_DPS_PER_LSB) };
        out->gyro_valid = true;
        out->accel_g = (mao_vec3_t) { axis(&raw[6], ACC_G_PER_LSB), axis(&raw[8], ACC_G_PER_LSB),
                                      axis(&raw[10], ACC_G_PER_LSB) };
    } else {
        ESP_RETURN_ON_ERROR(rd(REG_OUTX_L_A, raw, 6), TAG, "accel");
        out->accel_g = (mao_vec3_t) { axis(&raw[0], ACC_G_PER_LSB), axis(&raw[2], ACC_G_PER_LSB),
                                      axis(&raw[4], ACC_G_PER_LSB) };
    }
    return ESP_OK;
}

/* Quiet on failure: the caller (the charge limit, every 10 s on USB) reports
 * it once. Powered down, the sensor stops converting and the register would
 * be stale; in the low-power modes it converts at the accelerometer ODR. */
esp_err_t sense_imu_temperature(float *celsius)
{
    if (!s_dev || !celsius || s_powered_down) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t raw[2];
    const esp_err_t err = rd(REG_OUT_TEMP_L, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    *celsius = TEMP_ZERO_C + (float)(int16_t)(raw[0] | (raw[1] << 8)) / TEMP_LSB_PER_C;
    return ESP_OK;
}

esp_err_t sense_imu_id(uint32_t *id)
{
    ESP_RETURN_ON_FALSE(s_dev && id, ESP_ERR_INVALID_STATE, TAG, "imu not ready");
    uint8_t who = 0;
    ESP_RETURN_ON_ERROR(rd(REG_WHO_AM_I, &who, 1), TAG, "who_am_i");
    *id = who;
    return ESP_OK;
}

esp_err_t sense_imu_init(void)
{
    mao_board_i2c_info_t info;
    ESP_RETURN_ON_ERROR(mao_board_i2c_device(MAO_I2C_IMU, &info), TAG, "no imu on this board");
    ESP_RETURN_ON_FALSE(info.present, ESP_ERR_NOT_FOUND, TAG, "IMU did not answer at 0x%02X", info.address);
    ESP_RETURN_ON_ERROR(mao_board_i2c_add(MAO_I2C_IMU, &s_dev), TAG, "add");

    uint8_t who = 0;
    ESP_RETURN_ON_ERROR(rd(REG_WHO_AM_I, &who, 1), TAG, "who_am_i");
    ESP_RETURN_ON_FALSE(who == WHO_LSM6DSOX || who == WHO_LSM6DS3TRC, ESP_ERR_INVALID_VERSION, TAG,
                        "unknown WHO_AM_I 0x%02x", who);
    s_dsox = who == WHO_LSM6DSOX;

    ESP_RETURN_ON_ERROR(wr(REG_CTRL3_C, CTRL3_C_SW_RESET), TAG, "reset");
    uint8_t ctrl3 = CTRL3_C_SW_RESET;
    for (int i = 0; i < 10 && (ctrl3 & CTRL3_C_SW_RESET); i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
        ESP_RETURN_ON_ERROR(rd(REG_CTRL3_C, &ctrl3, 1), TAG, "reset poll");
    }
    ESP_RETURN_ON_ERROR(wr(REG_CTRL3_C, CTRL3_C_VALUE), TAG, "ctrl3");
    if (s_dsox) {
        uint8_t ctrl9 = 0;
        ESP_RETURN_ON_ERROR(rd(REG_CTRL9_XL, &ctrl9, 1), TAG, "ctrl9");
        ESP_RETURN_ON_ERROR(wr(REG_CTRL9_XL, ctrl9 | CTRL9_XL_I3C_OFF), TAG, "ctrl9");
    }
    ESP_RETURN_ON_ERROR(wr(REG_CTRL6_C, CTRL6_C_XL_HM_OFF), TAG, "ctrl6");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL7_G, CTRL7_G_HM_OFF), TAG, "ctrl7");
    ESP_RETURN_ON_ERROR(sense_imu_mode(SENSE_MODE_ACTIVE), TAG, "mode");
    ESP_LOGI(TAG, "imu: %s, accel 104 Hz +-4 g, gyro off; INT1 wake/tap, INT2 6D/free-fall (latched, active low)",
             sense_imu_part());
    return ESP_OK;
}
