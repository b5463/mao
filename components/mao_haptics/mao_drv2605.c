/*
 * TI DRV2605L haptic driver, I2C 0x5A, EN on the board's haptic rail,
 * IN/TRIG tied to GND (internal trigger: effects start with the GO bit).
 * Supply is VSYS (3.0-4.4 V).
 *
 * Actuator: LRA LD0832AA-0099F (8 x 3.2 mm coin), f0 = 235 Hz, 1.8 Vrms rated.
 * Mode: LRA, closed loop, auto-resonance tracking, ROM library 6 (LRA).
 *
 * Rated voltage (datasheet, LRA closed loop):
 *   V_rms = 20.58 mV x RATED_VOLTAGE / sqrt(1 - (4 x t_SAMPLE + 300 us) x f_LRA)
 *   t_SAMPLE = 300 us (SAMPLE_TIME = 3), f_LRA = 235 Hz:
 *     1 - (1200 us + 300 us) x 235 Hz = 1 - 0.3525 = 0.6475, sqrt = 0.8047
 *   RATED_VOLTAGE = 1.8 V x 0.8047 / 20.58 mV = 70.4  -> 70 (0x46)
 * Overdrive clamp (LRA):
 *   V_od(peak) = 21.32 mV x OD_CLAMP x sqrt(1 - f_LRA x 800 us)
 *   sqrt(1 - 235 x 800 us) = sqrt(0.812) = 0.9011
 *   OD_CLAMP = 2.5 V / (21.32 mV x 0.9011) = 130.1  -> 130 (0x82), ~2.5 V peak
 * Drive time (LRA): half a period, 1 / 235 Hz / 2 = 2.13 ms;
 *   drive time = DRIVE_TIME x 0.1 ms + 0.5 ms  ->  DRIVE_TIME = 16 (2.1 ms)
 * VERIFY AT BRING-UP: auto-calibration must pass (STATUS.DIAG_RESULT = 0)
 * with the actuator mounted; the resonance period register (0x22) shows the
 * frequency it locked to.
 */
#include "mao_haptics_priv.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mao_board.h"

static const char *TAG = "MAO_HAPTICS";

#define REG_STATUS          0x00
#define REG_MODE            0x01
#define REG_LIBRARY         0x03
#define REG_SEQ1            0x04       /* 0x04..0x0B: 8 sequencer slots */
#define REG_GO              0x0C
#define REG_RATED_VOLTAGE   0x16
#define REG_OD_CLAMP        0x17
#define REG_A_CAL_COMP      0x18
#define REG_A_CAL_BEMF      0x19
#define REG_FEEDBACK        0x1A
#define REG_CONTROL1        0x1B
#define REG_CONTROL2        0x1C
#define REG_CONTROL3        0x1D
#define REG_CONTROL4        0x1E
#define REG_LRA_PERIOD      0x22

#define MODE_INTERNAL_TRIG  0x00
#define MODE_AUTO_CAL       0x07
#define MODE_STANDBY        0x40       /* STANDBY bit */
#define LIBRARY_LRA         6
#define STATUS_DIAG_FAIL    (1u << 3)
#define DEVICE_ID_DRV2605L  7          /* STATUS[7:5] */
#define DEVICE_ID_DRV2605   3

#define RATED_VOLTAGE       70         /* 1.8 Vrms, see above */
#define OD_CLAMP            130        /* ~2.5 V peak */
#define DRIVE_TIME          16         /* 2.1 ms = half the 235 Hz period */
#define FB_BRAKE_FACTOR     3          /* 4x (datasheet default) */
#define LOOP_GAIN           1          /* medium */
#define BEMF_GAIN_DEFAULT   2          /* until calibrated */
#define FEEDBACK_LRA        0x80       /* N_ERM_LRA */
/* CONTROL1: STARTUP_BOOST | DRIVE_TIME. */
#define CONTROL1_VALUE      (0x80 | DRIVE_TIME)
/* CONTROL2: BIDIR_INPUT, BRAKE_STABILIZER, SAMPLE_TIME 300 us, BLANKING 1, IDISS 1. */
#define CONTROL2_VALUE      0xF5
/* CONTROL3: NG_THRESH 4 %, closed-loop LRA with auto-resonance (LRA_OPEN_LOOP = 0). */
#define CONTROL3_VALUE      0xA0
/* CONTROL4: AUTO_CAL_TIME = 3 (1000-1200 ms), the longest and most accurate. */
#define CONTROL4_VALUE      0x30

#define EN_SETTLE_MS        1
#define CAL_TIMEOUT_MS      3000
#define I2C_TIMEOUT_MS      50

static i2c_master_dev_handle_t s_dev;
static bool s_powered;
static mao_drv_cal_t s_cal = { .comp = 0x0D, .bemf = 0x6D, .bemf_gain = BEMF_GAIN_DEFAULT };  /* datasheet defaults */

static esp_err_t wr(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t rd(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, I2C_TIMEOUT_MS);
}

static uint8_t feedback_value(uint8_t bemf_gain)
{
    return (uint8_t)(FEEDBACK_LRA | (FB_BRAKE_FACTOR << 4) | (LOOP_GAIN << 2) | (bemf_gain & 0x03));
}

static esp_err_t write_drive_config(void)
{
    ESP_RETURN_ON_ERROR(wr(REG_RATED_VOLTAGE, RATED_VOLTAGE), TAG, "rated voltage");
    ESP_RETURN_ON_ERROR(wr(REG_OD_CLAMP, OD_CLAMP), TAG, "od clamp");
    ESP_RETURN_ON_ERROR(wr(REG_FEEDBACK, feedback_value(s_cal.bemf_gain)), TAG, "feedback");
    ESP_RETURN_ON_ERROR(wr(REG_CONTROL1, CONTROL1_VALUE), TAG, "control1");
    ESP_RETURN_ON_ERROR(wr(REG_CONTROL2, CONTROL2_VALUE), TAG, "control2");
    ESP_RETURN_ON_ERROR(wr(REG_CONTROL3, CONTROL3_VALUE), TAG, "control3");
    ESP_RETURN_ON_ERROR(wr(REG_CONTROL4, CONTROL4_VALUE), TAG, "control4");
    return ESP_OK;
}

static esp_err_t enable(bool on)
{
    ESP_RETURN_ON_ERROR(mao_board_rail_set(MAO_RAIL_HAPTIC, on), TAG, "haptic rail");
    s_powered = on;
    if (on) {
        vTaskDelay(pdMS_TO_TICKS(EN_SETTLE_MS));
    }
    return ESP_OK;
}

esp_err_t mao_drv_init(void)
{
    mao_board_i2c_info_t info;
    ESP_RETURN_ON_ERROR(mao_board_i2c_device(MAO_I2C_HAPTIC, &info), TAG, "no haptic driver on this board");
    ESP_RETURN_ON_FALSE(info.present, ESP_ERR_NOT_FOUND, TAG, "DRV2605L did not answer at 0x%02X", info.address);
    ESP_RETURN_ON_ERROR(mao_board_i2c_add(MAO_I2C_HAPTIC, &s_dev), TAG, "add");

    ESP_RETURN_ON_ERROR(enable(true), TAG, "enable");
    uint8_t status = 0;
    esp_err_t err = rd(REG_STATUS, &status);
    enable(false);
    ESP_RETURN_ON_ERROR(err, TAG, "status");
    const uint8_t id = status >> 5;
    ESP_RETURN_ON_FALSE(id == DEVICE_ID_DRV2605L || id == DEVICE_ID_DRV2605, ESP_ERR_INVALID_VERSION, TAG,
                        "unexpected DEVICE_ID %u", id);
    return ESP_OK;
}

void mao_drv_set_cal(const mao_drv_cal_t *cal)
{
    if (cal) {
        s_cal = *cal;
    }
}

bool mao_drv_is_powered(void)
{
    return s_powered;
}

esp_err_t mao_drv_power(bool on)
{
    if (!on) {
        if (s_powered) {
            wr(REG_MODE, MODE_STANDBY);   /* best effort before EN drops */
        }
        return enable(false);
    }
    if (s_powered) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(enable(true), TAG, "enable");
    esp_err_t err = wr(REG_MODE, MODE_INTERNAL_TRIG);   /* leaves standby */
    if (err == ESP_OK) {
        err = write_drive_config();
    }
    if (err == ESP_OK) {
        err = wr(REG_A_CAL_COMP, s_cal.comp);
    }
    if (err == ESP_OK) {
        err = wr(REG_A_CAL_BEMF, s_cal.bemf);
    }
    if (err == ESP_OK) {
        err = wr(REG_LIBRARY, LIBRARY_LRA);
    }
    if (err != ESP_OK) {
        enable(false);
        ESP_LOGW(TAG, "drv2605l setup failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t mao_drv_play(const uint8_t *seq, size_t len)
{
    ESP_RETURN_ON_FALSE(s_powered && seq && len <= 8, ESP_ERR_INVALID_STATE, TAG, "not ready");
    ESP_RETURN_ON_ERROR(wr(REG_GO, 0), TAG, "stop");
    /* All 8 slots in one burst (auto-increment); a 0 ends the sequence. */
    uint8_t buf[1 + 8] = { REG_SEQ1 };
    for (size_t i = 0; i < len; i++) {
        buf[1 + i] = seq[i];
    }
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS), TAG, "sequence");
    return wr(REG_GO, 1);
}

esp_err_t mao_drv_busy(bool *busy)
{
    ESP_RETURN_ON_FALSE(busy, ESP_ERR_INVALID_ARG, TAG, "bad args");
    if (!s_powered) {
        *busy = false;
        return ESP_OK;
    }
    uint8_t go = 0;
    ESP_RETURN_ON_ERROR(rd(REG_GO, &go), TAG, "go");
    *busy = (go & 1) != 0;
    return ESP_OK;
}

esp_err_t mao_drv_stop(void)
{
    return s_powered ? wr(REG_GO, 0) : ESP_OK;
}

esp_err_t mao_drv_device_id(uint8_t *id)
{
    ESP_RETURN_ON_FALSE(s_dev && id, ESP_ERR_INVALID_STATE, TAG, "not ready");
    const bool was_on = s_powered;
    if (!was_on) {
        ESP_RETURN_ON_ERROR(enable(true), TAG, "enable");
    }
    uint8_t status = 0;
    const esp_err_t err = rd(REG_STATUS, &status);
    if (!was_on) {
        enable(false);
    }
    *id = (uint8_t)(status >> 5);
    return err;
}

esp_err_t mao_drv_calibrate(mao_drv_cal_t *out, uint8_t *lra_period)
{
    ESP_RETURN_ON_FALSE(s_dev && out, ESP_ERR_INVALID_STATE, TAG, "not ready");
    if (!s_powered) {
        ESP_RETURN_ON_ERROR(enable(true), TAG, "enable");
    }
    ESP_RETURN_ON_ERROR(wr(REG_MODE, MODE_AUTO_CAL), TAG, "mode");
    ESP_RETURN_ON_ERROR(write_drive_config(), TAG, "config");
    ESP_RETURN_ON_ERROR(wr(REG_GO, 1), TAG, "go");

    uint8_t go = 1;
    for (int waited = 0; (go & 1) && waited < CAL_TIMEOUT_MS; waited += 20) {
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_RETURN_ON_ERROR(rd(REG_GO, &go), TAG, "go poll");
    }
    ESP_RETURN_ON_FALSE((go & 1) == 0, ESP_ERR_TIMEOUT, TAG, "calibration did not finish");

    uint8_t status = 0, comp = 0, bemf = 0, fb = 0, period = 0;
    ESP_RETURN_ON_ERROR(rd(REG_STATUS, &status), TAG, "status");
    ESP_RETURN_ON_ERROR(rd(REG_A_CAL_COMP, &comp), TAG, "comp");
    ESP_RETURN_ON_ERROR(rd(REG_A_CAL_BEMF, &bemf), TAG, "bemf");
    ESP_RETURN_ON_ERROR(rd(REG_FEEDBACK, &fb), TAG, "feedback");
    rd(REG_LRA_PERIOD, &period);
    wr(REG_MODE, MODE_STANDBY);
    ESP_RETURN_ON_FALSE((status & STATUS_DIAG_FAIL) == 0, ESP_FAIL, TAG,
                        "auto-calibration failed (actuator not free or not connected?)");

    if (lra_period) {
        *lra_period = period;
    }
    *out = (mao_drv_cal_t) { .comp = comp, .bemf = bemf, .bemf_gain = (uint8_t)(fb & 0x03) };
    s_cal = *out;
    /* LRA period register: 98.46 us per LSB. */
    ESP_LOGI(TAG, "calibrated: A_CAL_COMP=0x%02x A_CAL_BEMF=0x%02x BEMF_GAIN=%u, resonance ~%u Hz",
             comp, bemf, out->bemf_gain, period ? (unsigned)(1000000u / (period * 9846u / 100u)) : 0u);
    return ESP_OK;
}
