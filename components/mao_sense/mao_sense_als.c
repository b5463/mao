/*
 * Ambient light: TI OPT3004 at 0x44 (OPT3001 register set).
 *   0x00 RESULT     E[15:12] R[11:0], lux = 0.01 x 2^E x R
 *   0x01 CONFIG     RN[15:12] CT[11] M[10:9] OVF CRF FH FL L[4] POL[3] ME FC[1:0]
 *   0x02 LOW LIMIT, 0x03 HIGH LIMIT (same format as RESULT)
 *   0x7E manufacturer 0x5449 ("TI"), 0x7F device 0x3001
 *
 * Automatic full-scale (RN = 1100), 800 ms conversions, continuous. The INT
 * pin (open-drain, on the board's wired-OR sense-alert line with the fuel
 * gauge) is a latched window comparator re-centred on every reading, so it
 * only fires on a real change in light. Reading CONFIG clears the latch,
 * which every poll does, so the shared line is never held for long.
 */
#include "mao_sense_priv.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mao_board.h"

static const char *TAG = "MAO_SENSE";

#define REG_RESULT          0x00
#define REG_CONFIG          0x01
#define REG_LOW_LIMIT       0x02
#define REG_HIGH_LIMIT      0x03
#define REG_MANUFACTURER    0x7E
#define REG_DEVICE          0x7F

#define MANUFACTURER_TI     0x5449
#define DEVICE_OPT3001      0x3001

/* RN = auto (0xC), CT = 800 ms, latched window, INT active low, 2 faults. */
#define CONFIG_BASE         (0xC000u | 0x0800u | 0x0010u | 0x0001u)
#define CONFIG_CONTINUOUS   (0x3u << 9)
#define CONFIG_SHUTDOWN     (0x0u << 9)

#define WINDOW_RATIO        0.25f     /* INT outside +-25 % ... */
#define WINDOW_MIN_LUX      2.0f      /* ... or +-2 lux in the dark */
#define I2C_TIMEOUT_MS      50

static i2c_master_dev_handle_t s_dev;

static esp_err_t wr16(uint8_t reg, uint16_t value)
{
    const uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)value };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t rd16(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, buf, 2, I2C_TIMEOUT_MS), TAG, "rd");
    *value = (uint16_t)((buf[0] << 8) | buf[1]);
    return ESP_OK;
}

static float decode_lux(uint16_t raw)
{
    return 0.01f * (float)(1u << (raw >> 12)) * (float)(raw & 0x0FFF);
}

/* Smallest exponent whose 12-bit mantissa holds the value. */
static uint16_t encode_lux(float lux)
{
    if (lux < 0.0f) {
        lux = 0.0f;
    }
    for (uint16_t e = 0; e <= 11; e++) {
        const float m = lux / (0.01f * (float)(1u << e));
        if (m <= 4095.0f) {
            return (uint16_t)((e << 12) | (uint16_t)m);
        }
    }
    return 0xBFFF;   /* full scale */
}

esp_err_t sense_als_mode(sense_mode_t mode)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "als not ready");
    /* Off while drowsy too: its INT shares the expander wake line, and a
     * light change alone should not wake MAO. */
    const bool on = mode == SENSE_MODE_ACTIVE || mode == SENSE_MODE_IDLE;
    return wr16(REG_CONFIG, CONFIG_BASE | (on ? CONFIG_CONTINUOUS : CONFIG_SHUTDOWN));
}

esp_err_t sense_als_read(mao_obs_als_t *out)
{
    ESP_RETURN_ON_FALSE(s_dev && out, ESP_ERR_INVALID_STATE, TAG, "als not ready");
    uint16_t raw = 0, config = 0;
    ESP_RETURN_ON_ERROR(rd16(REG_RESULT, &raw), TAG, "result");
    ESP_RETURN_ON_ERROR(rd16(REG_CONFIG, &config), TAG, "config");   /* clears the INT latch */
    out->lux = decode_lux(raw);

    float span = out->lux * WINDOW_RATIO;
    span = span < WINDOW_MIN_LUX ? WINDOW_MIN_LUX : span;
    ESP_RETURN_ON_ERROR(wr16(REG_LOW_LIMIT, encode_lux(out->lux - span)), TAG, "low");
    ESP_RETURN_ON_ERROR(wr16(REG_HIGH_LIMIT, encode_lux(out->lux + span)), TAG, "high");
    return ESP_OK;
}

esp_err_t sense_als_id(uint32_t *id)
{
    ESP_RETURN_ON_FALSE(s_dev && id, ESP_ERR_INVALID_STATE, TAG, "als not ready");
    uint16_t man = 0, dev = 0;
    ESP_RETURN_ON_ERROR(rd16(REG_MANUFACTURER, &man), TAG, "manufacturer");
    ESP_RETURN_ON_ERROR(rd16(REG_DEVICE, &dev), TAG, "device");
    *id = ((uint32_t)man << 16) | dev;
    return ESP_OK;
}

esp_err_t sense_als_init(void)
{
    mao_board_i2c_info_t info;
    ESP_RETURN_ON_ERROR(mao_board_i2c_device(MAO_I2C_ALS, &info), TAG, "no als on this board");
    ESP_RETURN_ON_FALSE(info.present, ESP_ERR_NOT_FOUND, TAG, "OPT3004 did not answer at 0x%02X", info.address);
    ESP_RETURN_ON_ERROR(mao_board_i2c_add(MAO_I2C_ALS, &s_dev), TAG, "add");

    uint16_t man = 0, dev = 0;
    ESP_RETURN_ON_ERROR(rd16(REG_MANUFACTURER, &man), TAG, "manufacturer");
    ESP_RETURN_ON_ERROR(rd16(REG_DEVICE, &dev), TAG, "device");
    ESP_RETURN_ON_FALSE(man == MANUFACTURER_TI && dev == DEVICE_OPT3001, ESP_ERR_INVALID_VERSION, TAG,
                        "unexpected ids 0x%04x/0x%04x", man, dev);
    ESP_RETURN_ON_ERROR(sense_als_mode(SENSE_MODE_ACTIVE), TAG, "mode");
    ESP_LOGI(TAG, "als: OPT3004, auto range, 800 ms continuous, latched +-25 %% window on the alert line");
    return ESP_OK;
}
