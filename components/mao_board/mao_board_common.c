/*
 * Board-agnostic parts of mao_board: names for the descriptor enums and the
 * "mao board" development report. Everything here goes through the public
 * mao_board API, so it works unchanged on every board.
 */
#include "mao_board.h"
#include "mao_board_priv.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mao_system.h"

const char *mao_board_rail_name(mao_board_rail_t rail)
{
    switch (rail) {
    case MAO_RAIL_DISPLAY: return "display";
    case MAO_RAIL_AMP:     return "amp";
    case MAO_RAIL_HAPTIC:  return "haptic";
    case MAO_RAIL_TOF:     return "tof";
    case MAO_RAIL_IR_RX:   return "ir_rx";
    default:               return "?";
    }
}

const char *mao_board_i2c_name(mao_board_i2c_dev_t dev)
{
    switch (dev) {
    case MAO_I2C_TOF:        return "tof";
    case MAO_I2C_FUEL_GAUGE: return "fuel_gauge";
    case MAO_I2C_HAPTIC:     return "haptic";
    case MAO_I2C_IMU:        return "imu";
    default:                 return "?";
    }
}

const char *mao_board_charger_status_name(mao_charger_status_t s)
{
    switch (s) {
    case MAO_CHARGER_IDLE:          return "idle (done / no input)";
    case MAO_CHARGER_CHARGING:      return "charging";
    case MAO_CHARGER_FAULT:         return "FAULT (recoverable)";
    case MAO_CHARGER_FAULT_LATCHED: return "FAULT (latched)";
    default:                        return "?";
    }
}

#if CONFIG_MAO_DEV_CONSOLE

static const char *TAG = "MAO_BOARD";

/* "board"              the report
 * "board rev <name>"   manufacturing: store the board revision (A1: NVS hw_rev) */
static void devcmd_board(char *arg)
{
    if (arg && strncmp(arg, "rev ", 4) == 0) {
        const esp_err_t err = mao_board_store_revision(arg + 4);
        ESP_LOGI(TAG, "board revision '%s' stored: %s", arg + 4, esp_err_to_name(err));
        return;
    }
    mao_board_caps_t c;
    mao_board_get_caps(&c);
    ESP_LOGI(TAG, "board: %s, revision %s", MAO_BOARD_NAME, mao_board_revision());
    ESP_LOGI(TAG, "caps: led=%d display_power=%d i2c=%d amp_switch=%d pdm_line=%d ir=%d power_status=%d "
             "charge_control=%d hall_fast=%d sleep=%d deep_wake_knob=%d",
             c.rgb_led, c.display_power, c.i2c, c.amp_switch, c.audio_pdm_line, c.ir, c.power_status,
             c.charge_control, c.hall_fast, c.sleep, c.deep_wake_knob);
    if (!c.i2c) {
        return;
    }

    for (int d = 0; d < MAO_I2C_DEV_COUNT; d++) {
        mao_board_i2c_info_t info;
        if (mao_board_i2c_device((mao_board_i2c_dev_t)d, &info) == ESP_OK && info.fitted) {
            ESP_LOGI(TAG, "i2c 0x%02X %-10s %s", info.address, mao_board_i2c_name((mao_board_i2c_dev_t)d),
                     info.present ? "present at boot" : "MISSING at boot");
        }
    }

    uint8_t found[16];
    size_t count = 0;
    const esp_err_t err = mao_board_i2c_scan(found, sizeof(found), &count);
    char line[16 * 5 + 1];
    int n = 0;
    line[0] = '\0';
    for (size_t i = 0; i < count && i < sizeof(found); i++) {
        n += snprintf(line + n, sizeof(line) - n, " 0x%02X", found[i]);
    }
    ESP_LOGI(TAG, "i2c scan (%s): %u device(s):%s", esp_err_to_name(err), (unsigned)count, line);

    n = 0;
    char rails[96];
    rails[0] = '\0';
    for (int r = 0; r < MAO_RAIL_COUNT; r++) {
        bool rb = false;
        const bool have_rb = mao_board_rail_readback((mao_board_rail_t)r, &rb) == ESP_OK;
        n += snprintf(rails + n, sizeof(rails) - n, " %s=%s%s", mao_board_rail_name((mao_board_rail_t)r),
                      mao_board_rail_is_on((mao_board_rail_t)r) ? "on" : "off",
                      have_rb && rb != mao_board_rail_is_on((mao_board_rail_t)r) ? "(!)" : "");
    }
    bool usb = false;
    mao_board_line_get(MAO_LINE_USB_PRESENT, &usb);
    mao_charger_status_t chg = MAO_CHARGER_IDLE;
    const bool chg_ok = mao_board_charger_status(&chg) == ESP_OK;
    ESP_LOGI(TAG, "rails:%s; usb=%d charger=%s", rails, usb, chg_ok ? mao_board_charger_status_name(chg) : "-");
}

void mao_board_common_init(void)
{
    mao_devcmd_register("board", devcmd_board);
}

#else

void mao_board_common_init(void)
{
}

#endif
