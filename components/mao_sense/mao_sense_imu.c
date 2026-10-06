/*
 * IMU: TDK InvenSense ICM-42670-P at 0x68 (AP_AD0 low), I2C, written from
 * datasheet DS-000451 rev 1.0.
 *
 * Wiring (A1): INT1 = MAO_IRQ_IMU_INT1, configured open drain, active low,
 * latched (INT_CONFIG = 0x04) with the board's 100 k pull-up; it is also a
 * deep-sleep wake line (ext1 ANY_LOW). INT2 is not wired. The part has no
 * I3C lock-out at power-up (unlike the A0's LSM6DSOX), so the pull-up is
 * safe.
 *
 * Awake: accelerometer in low-noise mode, 100 Hz, +-4 g (8192 LSB/g), UI
 * filter 53 Hz; gyro off unless asked for (+-500 dps, 100 Hz). Wake-on-
 * motion (WOM_MODE = compare with the previous sample, any axis over
 * WOM_THR_MG, second over-threshold sample) is routed to INT1 in every mode,
 * so movement while MAO sleeps leaves INT1 latched low.
 * The part has no tap detector; 6D orientation and free-fall are derived
 * from the samples (event flags MAO_IMU_EV_ORIENTATION / _FREE_FALL).
 *
 * Rest / deep: accelerometer in low-power mode on the wake-up oscillator,
 * 25 Hz with 4x averaging, WoM on; reading INT_STATUS2 clears INT1 before a
 * deep sleep so the line is idle (high) when ext1 is armed.
 *
 * Registers (bank 0): MCLK_RDY 0x00, SIGNAL_PATH_RESET 0x02, INT_CONFIG
 * 0x06, TEMP_DATA1/0 0x09/0x0A, ACCEL_DATA_X1.. 0x0B..0x10, GYRO_DATA_X1..
 * 0x11..0x16, PWR_MGMT0 0x1F, GYRO_CONFIG0 0x20, ACCEL_CONFIG0 0x21,
 * ACCEL_CONFIG1 0x24, WOM_CONFIG 0x27, INT_SOURCE0 0x2B, INT_SOURCE1 0x2C,
 * INT_STATUS 0x3A, INT_STATUS2 0x3B, WHO_AM_I 0x75 (0x67), BLK_SEL_W 0x79,
 * MADDR_W 0x7A, M_W 0x7B. MREG1: ACCEL_WOM_X/Y/Z_THR 0x4B/0x4C/0x4D
 * (1 g / 256 per LSB). Sensor data are big-endian (INTF_CONFIG0 reset 0x30).
 *
 * VERIFY AT BRING-UP: WoM threshold (desk bumps vs a hand), the free-fall
 * and orientation thresholds at 10 Hz sampling, the die temperature offset.
 */
#include "mao_sense_priv.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "mao_board.h"

static const char *TAG = "MAO_SENSE";

#define REG_MCLK_RDY            0x00
#define REG_SIGNAL_PATH_RESET   0x02
#define REG_INT_CONFIG          0x06
#define REG_TEMP_DATA1          0x09
#define REG_ACCEL_DATA_X1       0x0B
#define REG_GYRO_DATA_X1        0x11
#define REG_PWR_MGMT0           0x1F
#define REG_GYRO_CONFIG0        0x20
#define REG_ACCEL_CONFIG0       0x21
#define REG_ACCEL_CONFIG1       0x24
#define REG_WOM_CONFIG          0x27
#define REG_INT_SOURCE0         0x2B
#define REG_INT_SOURCE1         0x2C
#define REG_INT_STATUS          0x3A
#define REG_INT_STATUS2         0x3B
#define REG_WHO_AM_I            0x75
#define REG_BLK_SEL_W           0x79
#define REG_MADDR_W             0x7A
#define REG_M_W                 0x7B
#define MREG1_ACCEL_WOM_X_THR   0x4B
#define MREG1_ACCEL_WOM_Y_THR   0x4C
#define MREG1_ACCEL_WOM_Z_THR   0x4D

#define WHO_ICM42670P           0x67
#define MCLK_RDY_BIT            (1u << 3)
#define SOFT_RESET              (1u << 4)
/* INT_CONFIG: INT1 latched (bit 2), open drain (bit 1 = 0), active low (bit 0 = 0). */
#define INT_CONFIG_VALUE        0x04
/* PWR_MGMT0 */
#define PWR_IDLE                (1u << 4)     /* RC oscillator on with the sensors off (MREG access) */
#define PWR_ACCEL_LN            0x03
#define PWR_ACCEL_LP            0x02          /* bit 7 = 0: LP on the wake-up oscillator (lowest power) */
#define PWR_GYRO_LN             (0x03u << 2)
/* ACCEL_CONFIG0: FS +-4 g (10b << 5) | ODR */
#define ACCEL_FS_4G             (0x02u << 5)
#define ODR_100HZ               0x09
#define ODR_25HZ                0x0B
#define ACCEL_LSB_PER_G         8192.0f
/* ACCEL_CONFIG1: 4x averaging (LP mode; set before entering it) | UI filter 53 Hz */
#define ACCEL_CONFIG1_VALUE     ((0x01u << 4) | 0x04)
/* GYRO_CONFIG0: +-500 dps (10b << 5) | 100 Hz */
#define GYRO_CONFIG0_VALUE      ((0x02u << 5) | ODR_100HZ)
#define GYRO_LSB_PER_DPS        65.5f
/* WOM_CONFIG: assert on the 2nd over-threshold sample (01b << 3), OR of the
 * axes, compare with the previous sample (bit 1), enable (bit 0). The
 * duration / mode bits only change with WOM_EN = 0. */
#define WOM_CONFIG_SETUP        ((0x01u << 3) | (1u << 1))
#define WOM_CONFIG_ON           (WOM_CONFIG_SETUP | 1u)
/* INT_SOURCE1: WoM X / Y / Z to INT1. */
#define INT_SOURCE1_WOM         0x07
#define WOM_INT_MASK            0x07          /* INT_STATUS2 WOM_X/Y/Z */

/* VERIFY AT BRING-UP */
#define WOM_THR_MG              98            /* 25 LSB x 3.9 mg */
#define FREE_FALL_G             0.35f         /* |a| below this in a sample */
#define ORIENT_ENTER_G          0.80f         /* an axis this close to +-1 g owns the 6D position */

#define I2C_TIMEOUT_MS          50
#define RESET_WAIT_MS           2             /* start-up time for register access: 1 ms */
#define MODE_WAIT_US            250           /* "no register writes for 200 us" after a mode change */

static i2c_master_dev_handle_t s_dev;
static mao_sense_level_t s_level = MAO_SENSE_OFF;
static bool s_gyro;
static mao_orientation_t s_orient;
static bool s_falling;

/* ------------------------------------------------------------------------ */
/* Register access                                                          */
/* ------------------------------------------------------------------------ */

static esp_err_t wr(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

/* MREG1 single-byte write (datasheet "Accessing MREG1..."): the internal
 * clock must run (MCLK_RDY), and no other access for 10 us around it. */
static esp_err_t mreg1_write(uint8_t addr, uint8_t value)
{
    ESP_RETURN_ON_ERROR(wr(REG_BLK_SEL_W, 0x00), TAG, "blk_sel");
    ESP_RETURN_ON_ERROR(wr(REG_MADDR_W, addr), TAG, "maddr");
    ESP_RETURN_ON_ERROR(wr(REG_M_W, value), TAG, "m_w");
    esp_rom_delay_us(10);
    return ESP_OK;
}

static esp_err_t wait_mclk(void)
{
    for (int i = 0; i < 20; i++) {
        uint8_t v = 0;
        if (rd(REG_MCLK_RDY, &v, 1) == ESP_OK && (v & MCLK_RDY_BIT)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t set_power(uint8_t pwr)
{
    ESP_RETURN_ON_ERROR(wr(REG_PWR_MGMT0, pwr), TAG, "pwr_mgmt0");
    esp_rom_delay_us(MODE_WAIT_US);
    return ESP_OK;
}

static int16_t be16(const uint8_t *p)
{
    return (int16_t)((p[0] << 8) | p[1]);
}

/* ------------------------------------------------------------------------ */
/* Modes                                                                    */
/* ------------------------------------------------------------------------ */

esp_err_t sense_imu_mode(mao_sense_level_t level)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "imu not ready");
    uint8_t st = 0;
    switch (level) {
    case MAO_SENSE_AWAKE:
        ESP_RETURN_ON_ERROR(wr(REG_ACCEL_CONFIG0, ACCEL_FS_4G | ODR_100HZ), TAG, "accel cfg");
        ESP_RETURN_ON_ERROR(set_power(PWR_ACCEL_LN | (s_gyro ? PWR_GYRO_LN : 0)), TAG, "awake");
        break;
    case MAO_SENSE_REST:
    case MAO_SENSE_DEEP:
        /* LP mode on the wake-up oscillator: the 4x averaging was set while
         * not in LP (it cannot change there). The gyro goes off. */
        ESP_RETURN_ON_ERROR(wr(REG_ACCEL_CONFIG0, ACCEL_FS_4G | ODR_25HZ), TAG, "accel cfg");
        ESP_RETURN_ON_ERROR(set_power(PWR_ACCEL_LP), TAG, "rest");
        if (level == MAO_SENSE_DEEP) {
            rd(REG_INT_STATUS2, &st, 1);       /* release a latched INT1 before ext1 is armed */
        }
        break;
    default:
        ESP_RETURN_ON_ERROR(set_power(0x00), TAG, "off");
        rd(REG_INT_STATUS2, &st, 1);
        break;
    }
    s_level = level;
    return ESP_OK;
}

esp_err_t sense_imu_gyro(bool on)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "imu not ready");
    s_gyro = on;
    if (s_level == MAO_SENSE_AWAKE) {
        return set_power(PWR_ACCEL_LN | (on ? PWR_GYRO_LN : 0));
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Samples                                                                  */
/* ------------------------------------------------------------------------ */

static mao_orientation_t orientation_of(const mao_vec3_t *a)
{
    if (a->x > ORIENT_ENTER_G) {
        return MAO_ORIENT_X_UP;
    }
    if (a->x < -ORIENT_ENTER_G) {
        return MAO_ORIENT_X_DOWN;
    }
    if (a->y > ORIENT_ENTER_G) {
        return MAO_ORIENT_Y_UP;
    }
    if (a->y < -ORIENT_ENTER_G) {
        return MAO_ORIENT_Y_DOWN;
    }
    if (a->z > ORIENT_ENTER_G) {
        return MAO_ORIENT_Z_UP;
    }
    if (a->z < -ORIENT_ENTER_G) {
        return MAO_ORIENT_Z_DOWN;
    }
    return MAO_ORIENT_UNKNOWN;     /* between positions: keep the last one */
}

esp_err_t sense_imu_sample(mao_obs_imu_t *out)
{
    ESP_RETURN_ON_FALSE(s_dev && out, ESP_ERR_INVALID_STATE, TAG, "imu not ready");
    *out = (mao_obs_imu_t) { 0 };

    /* Reading INT_STATUS2 clears the latched WoM flags and releases INT1. */
    uint8_t st2 = 0;
    ESP_RETURN_ON_ERROR(rd(REG_INT_STATUS2, &st2, 1), TAG, "int status2");
    if (st2 & WOM_INT_MASK) {
        out->events |= MAO_IMU_EV_ACTIVITY;
    }
    if (s_level == MAO_SENSE_OFF) {
        out->orientation = s_orient;
        return ESP_OK;           /* no vectors from a powered-off sensor (all zero) */
    }

    uint8_t raw[12];
    const size_t n = (s_gyro && s_level == MAO_SENSE_AWAKE) ? 12 : 6;
    ESP_RETURN_ON_ERROR(rd(REG_ACCEL_DATA_X1, raw, n), TAG, "data");
    out->accel_g = (mao_vec3_t) {
        .x = (float)be16(&raw[0]) / ACCEL_LSB_PER_G,
        .y = (float)be16(&raw[2]) / ACCEL_LSB_PER_G,
        .z = (float)be16(&raw[4]) / ACCEL_LSB_PER_G,
    };
    if (n == 12) {
        out->gyro_dps = (mao_vec3_t) {
            .x = (float)be16(&raw[6]) / GYRO_LSB_PER_DPS,
            .y = (float)be16(&raw[8]) / GYRO_LSB_PER_DPS,
            .z = (float)be16(&raw[10]) / GYRO_LSB_PER_DPS,
        };
        out->gyro_valid = true;
    }

    /* Derived events (the part has no 6D / free-fall engine without its
     * DMP): a new dominant axis, and a sample in free fall. */
    const mao_orientation_t o = orientation_of(&out->accel_g);
    if (o != MAO_ORIENT_UNKNOWN && o != s_orient) {
        if (s_orient != MAO_ORIENT_UNKNOWN) {
            out->events |= MAO_IMU_EV_ORIENTATION;
        }
        s_orient = o;
    }
    out->orientation = s_orient;
    const float mag = sqrtf(out->accel_g.x * out->accel_g.x + out->accel_g.y * out->accel_g.y +
                            out->accel_g.z * out->accel_g.z);
    const bool falling = mag < FREE_FALL_G;
    if (falling && !s_falling) {
        out->events |= MAO_IMU_EV_FREE_FALL;
    }
    s_falling = falling;
    return ESP_OK;
}

esp_err_t sense_imu_temperature(float *celsius)
{
    ESP_RETURN_ON_FALSE(s_dev && celsius, ESP_ERR_INVALID_STATE, TAG, "imu not ready");
    if (s_level == MAO_SENSE_OFF) {
        return ESP_ERR_INVALID_STATE;      /* the register would be stale */
    }
    uint8_t raw[2];
    const esp_err_t err = rd(REG_TEMP_DATA1, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    *celsius = (float)be16(raw) / 128.0f + 25.0f;
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

/* ------------------------------------------------------------------------ */
/* Init                                                                     */
/* ------------------------------------------------------------------------ */

static esp_err_t configure(void)
{
    /* Soft reset, then the RC oscillator on (IDLE) for the MREG1 writes. */
    ESP_RETURN_ON_ERROR(wr(REG_SIGNAL_PATH_RESET, SOFT_RESET), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_MS));
    uint8_t v = 0;
    rd(REG_INT_STATUS, &v, 1);            /* clears RESET_DONE */

    uint8_t who = 0;
    ESP_RETURN_ON_ERROR(rd(REG_WHO_AM_I, &who, 1), TAG, "who_am_i");
    ESP_RETURN_ON_FALSE(who == WHO_ICM42670P, ESP_ERR_INVALID_VERSION, TAG, "unexpected WHO_AM_I 0x%02x", who);

    ESP_RETURN_ON_ERROR(set_power(PWR_IDLE), TAG, "idle");
    ESP_RETURN_ON_ERROR(wait_mclk(), TAG, "mclk");
    const uint8_t thr = (uint8_t)((WOM_THR_MG * 256 + 500) / 1000);
    ESP_RETURN_ON_ERROR(mreg1_write(MREG1_ACCEL_WOM_X_THR, thr), TAG, "wom x");
    ESP_RETURN_ON_ERROR(mreg1_write(MREG1_ACCEL_WOM_Y_THR, thr), TAG, "wom y");
    ESP_RETURN_ON_ERROR(mreg1_write(MREG1_ACCEL_WOM_Z_THR, thr), TAG, "wom z");

    ESP_RETURN_ON_ERROR(wr(REG_INT_CONFIG, INT_CONFIG_VALUE), TAG, "int config");
    ESP_RETURN_ON_ERROR(wr(REG_ACCEL_CONFIG1, ACCEL_CONFIG1_VALUE), TAG, "accel cfg1");
    ESP_RETURN_ON_ERROR(wr(REG_GYRO_CONFIG0, GYRO_CONFIG0_VALUE), TAG, "gyro cfg0");
    ESP_RETURN_ON_ERROR(wr(REG_ACCEL_CONFIG0, ACCEL_FS_4G | ODR_100HZ), TAG, "accel cfg0");
    ESP_RETURN_ON_ERROR(set_power(PWR_ACCEL_LN), TAG, "accel on");
    vTaskDelay(pdMS_TO_TICKS(2));         /* a first sample for WoM's reference */

    ESP_RETURN_ON_ERROR(wr(REG_WOM_CONFIG, WOM_CONFIG_SETUP), TAG, "wom setup");
    ESP_RETURN_ON_ERROR(wr(REG_WOM_CONFIG, WOM_CONFIG_ON), TAG, "wom on");
    ESP_RETURN_ON_ERROR(wr(REG_INT_SOURCE0, 0x00), TAG, "int source0");
    ESP_RETURN_ON_ERROR(wr(REG_INT_SOURCE1, INT_SOURCE1_WOM), TAG, "int source1");
    rd(REG_INT_STATUS2, &v, 1);
    s_level = MAO_SENSE_AWAKE;
    return ESP_OK;
}

esp_err_t sense_imu_init(void)
{
    mao_board_i2c_info_t info;
    ESP_RETURN_ON_ERROR(mao_board_i2c_device(MAO_I2C_IMU, &info), TAG, "no imu on this board");
    ESP_RETURN_ON_FALSE(info.present, ESP_ERR_NOT_FOUND, TAG, "IMU did not answer at 0x%02X", info.address);
    ESP_RETURN_ON_ERROR(mao_board_i2c_add(MAO_I2C_IMU, &s_dev), TAG, "add");
    esp_err_t err = configure();
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err;
    }
    ESP_LOGI(TAG, "imu: ICM-42670-P, accel 100 Hz LN +-4 g, wake-on-motion %d mg on INT1 (open drain, "
             "active low, latched), gyro off", WOM_THR_MG);
    return ESP_OK;
}
