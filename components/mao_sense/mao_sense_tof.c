/*
 * Proximity: ST VL53L4CD at 0x29, through ST's Ultra Lite Driver.
 * XSHUT is the board's ToF rail; GPIO1 (open-drain, active low) is the
 * board's ToF interrupt line, data-ready by default.
 *
 * Ranging runs in the sensor's autonomous low-power mode: a 20 ms timing
 * budget every 200 ms while MAO is awake, one interrupt per result. While
 * MAO rests (light or deep sleep) the sensor is switched off (XSHUT low) and
 * booted again on the wake. The approach-threshold mode (ULD threshold
 * window "below low") is kept for a later rest mode that watches for a hand.
 */
#include "mao_sense_priv.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mao_board.h"
#include "vl53l4cd/vl53l4cd_api.h"

static const char *TAG = "MAO_SENSE";

#define MODEL_ID                0xEBAA
#define BOOT_MS                 2         /* XSHUT high -> I2C ready (1.2 ms max) */
#define TIMING_BUDGET_MS        20
#define PERIOD_ACTIVE_MS        200
#define PERIOD_APPROACH_MS      500
/* VERIFY AT BRING-UP: approach distance through the window / cover. */
#define APPROACH_MM             150
#define WINDOW_BELOW_LOW        0         /* ULD: interrupt when distance < low */
#define INT_NEW_SAMPLE          0x20      /* SYSTEM__INTERRUPT default: every result */

static mao_vl53l4cd_dev_t s_dev;
static bool s_on;                         /* powered and initialised */
static bool s_threshold;
static uint32_t s_period_ms = PERIOD_ACTIVE_MS;

uint32_t sense_tof_period_ms(void)
{
    return s_period_ms;
}

static esp_err_t uld(VL53L4CD_Error st, const char *what)
{
    if (st != VL53L4CD_ERROR_NONE) {
        ESP_LOGW(TAG, "tof: %s failed (ULD status %u)", what, st);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* Stop, apply timing and interrupt mode, clear, start. */
static esp_err_t configure(uint32_t period_ms, bool threshold)
{
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_StopRanging(&s_dev), "stop"), TAG, "tof");
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_SetRangeTiming(&s_dev, TIMING_BUDGET_MS, period_ms), "timing"), TAG, "tof");
    if (threshold) {
        ESP_RETURN_ON_ERROR(uld(VL53L4CD_SetDetectionThresholds(&s_dev, APPROACH_MM, APPROACH_MM,
                                                                 WINDOW_BELOW_LOW), "thresholds"), TAG, "tof");
    } else {
        ESP_RETURN_ON_ERROR(uld(VL53L4CD_WrByte(&s_dev, VL53L4CD_SYSTEM__INTERRUPT, INT_NEW_SAMPLE),
                                "interrupt mode"), TAG, "tof");
    }
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_ClearInterrupt(&s_dev), "clear"), TAG, "tof");
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_StartRanging(&s_dev), "start"), TAG, "tof");
    s_period_ms = period_ms;
    s_threshold = threshold;
    return ESP_OK;
}

static esp_err_t power_up(void)
{
    ESP_RETURN_ON_ERROR(mao_board_rail_set(MAO_RAIL_TOF, true), TAG, "tof rail");
    vTaskDelay(pdMS_TO_TICKS(BOOT_MS));
    uint16_t id = 0;
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_GetSensorId(&s_dev, &id), "id"), TAG, "tof");
    ESP_RETURN_ON_FALSE(id == MODEL_ID, ESP_ERR_INVALID_VERSION, TAG, "unexpected model id 0x%04x", id);
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_SensorInit(&s_dev), "init"), TAG, "tof");
    s_on = true;
    return ESP_OK;
}

esp_err_t sense_tof_mode(mao_sense_level_t level)
{
    ESP_RETURN_ON_FALSE(s_dev.i2c, ESP_ERR_INVALID_STATE, TAG, "tof not ready");
    if (level != MAO_SENSE_AWAKE) {
        if (s_on) {
            VL53L4CD_StopRanging(&s_dev);
        }
        s_on = false;
        return mao_board_rail_set(MAO_RAIL_TOF, false);
    }
    if (!s_on) {
        ESP_RETURN_ON_ERROR(power_up(), TAG, "power up");
    }
    return configure(PERIOD_ACTIVE_MS, false);
}

esp_err_t sense_tof_read(mao_obs_tof_t *out, bool *ready)
{
    ESP_RETURN_ON_FALSE(out && ready, ESP_ERR_INVALID_ARG, TAG, "bad args");
    *ready = false;
    if (!s_on) {
        return ESP_OK;
    }
    uint8_t is_ready = 0;
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_CheckForDataReady(&s_dev, &is_ready), "data ready"), TAG, "tof");
    if (!is_ready) {
        return ESP_OK;
    }
    VL53L4CD_ResultsData_t r;
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_GetResult(&s_dev, &r), "result"), TAG, "tof");
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_ClearInterrupt(&s_dev), "clear"), TAG, "tof");
    *out = (mao_obs_tof_t) {
        .distance_mm = r.distance_mm,
        .range_status = r.range_status,
        .signal_kcps = r.signal_rate_kcps,
        .sigma_mm = r.sigma_mm,
        .threshold = s_threshold,
    };
    *ready = true;
    return ESP_OK;
}

esp_err_t sense_tof_id(uint32_t *id)
{
    ESP_RETURN_ON_FALSE(id, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(s_on, ESP_ERR_INVALID_STATE, TAG, "tof is off");
    uint16_t model = 0;
    ESP_RETURN_ON_ERROR(uld(VL53L4CD_GetSensorId(&s_dev, &model), "id"), TAG, "tof");
    *id = model;
    return ESP_OK;
}

esp_err_t sense_tof_init(void)
{
    mao_board_i2c_info_t info;
    ESP_RETURN_ON_ERROR(mao_board_i2c_device(MAO_I2C_TOF, &info), TAG, "no tof on this board");
    ESP_RETURN_ON_FALSE(info.present, ESP_ERR_NOT_FOUND, TAG, "VL53L4CD did not answer at 0x%02X", info.address);
    ESP_RETURN_ON_ERROR(mao_board_i2c_add(MAO_I2C_TOF, &s_dev.i2c), TAG, "add");
    esp_err_t err = sense_tof_mode(MAO_SENSE_AWAKE);
    if (err != ESP_OK) {
        mao_board_rail_set(MAO_RAIL_TOF, false);
        s_on = false;
        return err;
    }
    VL53L4CD_Version_t v;
    VL53L4CD_GetSWVersion(&v);
    ESP_LOGI(TAG, "tof: VL53L4CD (ULD %u.%u.%u), %d ms budget every %d ms; off while MAO rests",
             v.major, v.minor, v.build, TIMING_BUDGET_MS, PERIOD_ACTIVE_MS);
    return ESP_OK;
}
