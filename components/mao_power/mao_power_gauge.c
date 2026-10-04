/*
 * MAX17048 fuel gauge (ModelGauge, 1S LiPo ~465-500 mAh), I2C 0x36.
 *
 * Registers used (16-bit, MSB first):
 *   VCELL 0x02   78.125 uV / LSB
 *   SOC   0x04   1/256 % / LSB
 *   VERSION 0x08 0x001x
 *   HIBRT 0x0A   hibernate thresholds (0x8030 = datasheet default; 0x0000 disables)
 *   CONFIG 0x0C  RCOMP[15:8], SLEEP[7], ALSC[6], ALRT[5], ATHD[4:0] (empty alert at 32-ATHD %)
 *   VALRT 0x14   MIN[15:8] / MAX[7:0], 20 mV / LSB
 *   CRATE 0x16   signed, 0.208 %/h / LSB
 *   STATUS 0x1A  RI VH VL VR HD SC (+ EnVR)
 * The gauge needs no sense resistor and keeps tracking through MAO's deep
 * sleep, so everything here is configuration plus reads.
 */
#include "mao_power_priv.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mao_board.h"

static const char *TAG = "MAO_POWER";

#define REG_VCELL       0x02
#define REG_SOC         0x04
#define REG_VERSION     0x08
#define REG_HIBRT       0x0A
#define REG_CONFIG      0x0C
#define REG_VALRT       0x14
#define REG_CRATE       0x16
#define REG_STATUS      0x1A

#define I2C_TIMEOUT_MS  50

#define EMPTY_ALERT_PCT     5                          /* CONFIG.ATHD: alert at SOC <= 5 % */
#define VALRT_MIN_MV        3300                       /* low-voltage alert */
#define VALRT_MAX_MV        5100                       /* 0xFF: high alert effectively off */
#define HIBRT_DEFAULT       0x8030                     /* enable automatic hibernation */
#define CONFIG_ALRT         (1u << 5)
#define CONFIG_ALSC         (1u << 6)
#define CONFIG_ATHD_MASK    0x1Fu
#define STATUS_FLAGS        (MAO_GAUGE_ST_RI | MAO_GAUGE_ST_VH | MAO_GAUGE_ST_VL | MAO_GAUGE_ST_VR | \
                             MAO_GAUGE_ST_HD | MAO_GAUGE_ST_SC)

static i2c_master_dev_handle_t s_dev;

static esp_err_t rd16(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, buf, 2, I2C_TIMEOUT_MS), TAG, "rd 0x%02x", reg);
    *value = (uint16_t)((buf[0] << 8) | buf[1]);
    return ESP_OK;
}

static esp_err_t wr16(uint8_t reg, uint16_t value)
{
    const uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)value };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

esp_err_t mao_gauge_init(void)
{
    mao_board_i2c_info_t info;
    ESP_RETURN_ON_ERROR(mao_board_i2c_device(MAO_I2C_FUEL_GAUGE, &info), TAG, "no gauge on this board");
    ESP_RETURN_ON_FALSE(info.present, ESP_ERR_NOT_FOUND, TAG, "fuel gauge did not answer at 0x%02X", info.address);
    ESP_RETURN_ON_ERROR(mao_board_i2c_add(MAO_I2C_FUEL_GAUGE, &s_dev), TAG, "add");

    uint16_t version = 0;
    ESP_RETURN_ON_ERROR(rd16(REG_VERSION, &version), TAG, "version");
    ESP_RETURN_ON_FALSE((version & 0xFFF0) == 0x0010, ESP_ERR_INVALID_VERSION, TAG,
                        "unexpected VERSION 0x%04x", version);

    /* Keep the factory RCOMP; set the empty alert, no 1 % change alerts, and
     * clear a pending alert. VERIFY AT BRING-UP: RCOMP / custom model for
     * the actual cell if SOC tracks poorly. */
    uint16_t config = 0;
    ESP_RETURN_ON_ERROR(rd16(REG_CONFIG, &config), TAG, "config");
    config &= (uint16_t)~(CONFIG_ALRT | CONFIG_ALSC | CONFIG_ATHD_MASK);
    config |= (uint16_t)(32 - EMPTY_ALERT_PCT);
    ESP_RETURN_ON_ERROR(wr16(REG_CONFIG, config), TAG, "config");

    const uint16_t valrt = (uint16_t)(((VALRT_MIN_MV / 20) << 8) | (VALRT_MAX_MV / 20));
    ESP_RETURN_ON_ERROR(wr16(REG_VALRT, valrt), TAG, "valrt");
    ESP_RETURN_ON_ERROR(wr16(REG_HIBRT, HIBRT_DEFAULT), TAG, "hibrt");

    uint16_t status = 0;
    ESP_RETURN_ON_ERROR(rd16(REG_STATUS, &status), TAG, "status");
    if (status & MAO_GAUGE_ST_RI) {
        ESP_LOGI(TAG, "gauge: power-on reset seen (fresh cell or first boot); SOC settles over minutes");
    }
    ESP_RETURN_ON_ERROR(wr16(REG_STATUS, (uint16_t)(status & ~STATUS_FLAGS)), TAG, "status clear");
    ESP_LOGI(TAG, "gauge: MAX17048 v0x%04x, empty alert %d %%, low-voltage alert %d mV",
             version, EMPTY_ALERT_PCT, VALRT_MIN_MV);
    return ESP_OK;
}

esp_err_t mao_gauge_read(mao_gauge_reading_t *out)
{
    ESP_RETURN_ON_FALSE(s_dev && out, ESP_ERR_INVALID_STATE, TAG, "gauge not ready");
    uint16_t vcell = 0, soc = 0, crate = 0;
    ESP_RETURN_ON_ERROR(rd16(REG_VCELL, &vcell), TAG, "vcell");
    ESP_RETURN_ON_ERROR(rd16(REG_SOC, &soc), TAG, "soc");
    ESP_RETURN_ON_ERROR(rd16(REG_CRATE, &crate), TAG, "crate");
    out->voltage_mv = (uint16_t)(((uint32_t)vcell * 78125u) / 1000000u);
    out->soc_pct = (float)soc / 256.0f;
    out->rate_pct_per_h = (float)(int16_t)crate * 0.208f;
    return ESP_OK;
}

esp_err_t mao_gauge_version(uint16_t *version)
{
    ESP_RETURN_ON_FALSE(s_dev && version, ESP_ERR_INVALID_STATE, TAG, "gauge not ready");
    return rd16(REG_VERSION, version);
}

esp_err_t mao_gauge_take_alert(uint16_t *flags)
{
    ESP_RETURN_ON_FALSE(s_dev && flags, ESP_ERR_INVALID_STATE, TAG, "gauge not ready");
    uint16_t status = 0, config = 0;
    ESP_RETURN_ON_ERROR(rd16(REG_STATUS, &status), TAG, "status");
    ESP_RETURN_ON_ERROR(rd16(REG_CONFIG, &config), TAG, "config");
    *flags = status & STATUS_FLAGS;
    if (*flags) {
        ESP_RETURN_ON_ERROR(wr16(REG_STATUS, (uint16_t)(status & ~STATUS_FLAGS)), TAG, "status clear");
    }
    if (config & CONFIG_ALRT) {
        ESP_RETURN_ON_ERROR(wr16(REG_CONFIG, (uint16_t)(config & ~CONFIG_ALRT)), TAG, "alrt clear");
    }
    return ESP_OK;
}
