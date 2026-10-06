/*
 * MAO_MAIN A1: the shared I2C bus (I2C0, SDA 47 / SCL 48, 4.7 k pull-ups,
 * 400 kHz): ICM-42670-P IMU 0x68, VL53L4CD ToF 0x29, MAX17048 gauge 0x36,
 * DRV2605L haptic driver 0x5A. There is no expander any more: the ToF XSHUT
 * and the haptic EN are GPIOs (MAO_RAIL_TOF / MAO_RAIL_HAPTIC), raised only
 * for the boot probe and a scan.
 */
#include "mao_board.h"
#include "mao_board_a1_priv.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "MAO_BOARD";

/* Settle time after XSHUT / EN goes high before the device answers I2C
 * (VL53L4CD boot 1.2 ms max; DRV2605L a few hundred us). */
#define RAIL_PROBE_SETTLE_MS   2
#define PROBE_TIMEOUT_MS       20

static const uint8_t kAddress[MAO_I2C_DEV_COUNT] = {
    [MAO_I2C_TOF] = MAO_I2C_ADDR_TOF,
    [MAO_I2C_FUEL_GAUGE] = MAO_I2C_ADDR_FUEL_GAUGE,
    [MAO_I2C_HAPTIC] = MAO_I2C_ADDR_HAPTIC,
    [MAO_I2C_IMU] = MAO_I2C_ADDR_IMU,
};

static i2c_master_bus_handle_t s_bus;
static bool s_present[MAO_I2C_DEV_COUNT];

/* Power the switchable devices for a probe. Returns the rails switched on
 * here (bit per mao_board_rail_t), so that only those are switched off again. */
static uint32_t probe_power_begin(void)
{
    uint32_t added = 0;
    static const mao_board_rail_t kRails[] = { MAO_RAIL_TOF, MAO_RAIL_HAPTIC };
    for (size_t i = 0; i < sizeof(kRails) / sizeof(kRails[0]); i++) {
        if (!mao_board_rail_is_on(kRails[i]) && mao_board_rail_set(kRails[i], true) == ESP_OK) {
            added |= 1u << kRails[i];
        }
    }
    if (added) {
        vTaskDelay(pdMS_TO_TICKS(RAIL_PROBE_SETTLE_MS));
    }
    return added;
}

static void probe_power_end(uint32_t added)
{
    for (int r = 0; r < MAO_RAIL_COUNT; r++) {
        if (added & (1u << r)) {
            mao_board_rail_set((mao_board_rail_t)r, false);
        }
    }
}

esp_err_t a1_i2c_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = A1_I2C_PORT,
        .sda_io_num = MAO_PIN_I2C_SDA,
        .scl_io_num = MAO_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   /* external 4.7 k pull-ups */
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "i2c bus");

    const uint32_t added = probe_power_begin();
    int found = 0;
    for (int d = 0; d < MAO_I2C_DEV_COUNT; d++) {
        s_present[d] = i2c_master_probe(s_bus, kAddress[d], PROBE_TIMEOUT_MS) == ESP_OK;
        found += s_present[d] ? 1 : 0;
        if (!s_present[d]) {
            ESP_LOGW(TAG, "i2c: %s (0x%02X) did not answer", mao_board_i2c_name((mao_board_i2c_dev_t)d), kAddress[d]);
        }
    }
    probe_power_end(added);

    ESP_LOGI(TAG, "i2c: bus %d @ %d kHz, SDA %d SCL %d, %d/%d devices answered",
             A1_I2C_PORT, A1_I2C_HZ / 1000, MAO_PIN_I2C_SDA, MAO_PIN_I2C_SCL, found, MAO_I2C_DEV_COUNT);
    return ESP_OK;
}

esp_err_t mao_board_i2c_bus(i2c_master_bus_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(s_bus, ESP_ERR_INVALID_STATE, TAG, "no bus");
    *out = s_bus;
    return ESP_OK;
}

esp_err_t mao_board_i2c_device(mao_board_i2c_dev_t dev, mao_board_i2c_info_t *out)
{
    ESP_RETURN_ON_FALSE(out && dev < MAO_I2C_DEV_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad args");
    *out = (mao_board_i2c_info_t) {
        .address = kAddress[dev],
        .fitted = true,
        .present = s_present[dev],
    };
    return ESP_OK;
}

esp_err_t mao_board_i2c_add(mao_board_i2c_dev_t dev, i2c_master_dev_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out && dev < MAO_I2C_DEV_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(s_bus, ESP_ERR_INVALID_STATE, TAG, "no bus");
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = kAddress[dev],
        .scl_speed_hz = A1_I2C_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, out);
}

esp_err_t mao_board_i2c_scan(uint8_t *found, size_t max, size_t *count)
{
    ESP_RETURN_ON_FALSE(count, ESP_ERR_INVALID_ARG, TAG, "bad args");
    *count = 0;
    ESP_RETURN_ON_FALSE(s_bus, ESP_ERR_INVALID_STATE, TAG, "no bus");
    const uint32_t added = probe_power_begin();
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_bus, addr, PROBE_TIMEOUT_MS) == ESP_OK) {
            if (found && *count < max) {
                found[*count] = addr;
            }
            (*count)++;
        }
    }
    probe_power_end(added);
    return ESP_OK;
}
