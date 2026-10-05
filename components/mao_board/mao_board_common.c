/*
 * Board-agnostic parts of mao_board: names for the descriptor enums and the
 * "mao board" development report. Everything here goes through the public
 * mao_board API, so it works unchanged on every board.
 */
#include "mao_board.h"
#include "mao_board_priv.h"

#include <stdio.h>
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
    case MAO_RAIL_MIC:     return "mic";
    default:               return "?";
    }
}

const char *mao_board_i2c_name(mao_board_i2c_dev_t dev)
{
    switch (dev) {
    case MAO_I2C_EXPANDER:   return "expander";
    case MAO_I2C_TOF:        return "tof";
    case MAO_I2C_FUEL_GAUGE: return "fuel_gauge";
    case MAO_I2C_ALS:        return "als";
    case MAO_I2C_HAPTIC:     return "haptic";
    case MAO_I2C_IMU:        return "imu";
    default:                 return "?";
    }
}

const char *mao_board_touch_zone_name(mao_touch_zone_t zone)
{
    switch (zone) {
    case MAO_TOUCH_RIGHT: return "right";
    case MAO_TOUCH_LEFT:  return "left";
    case MAO_TOUCH_TOP:   return "top";
    case MAO_TOUCH_REAR:  return "rear";
    default:              return "?";
    }
}

#if CONFIG_MAO_DEV_CONSOLE

static const char *TAG = "MAO_BOARD";

static void devcmd_board(const char *args)
{
    (void)args;
    mao_board_caps_t c;
    mao_board_get_caps(&c);
    ESP_LOGI(TAG, "board: %s, revision %s (%d mV)", MAO_BOARD_NAME,
             mao_board_revision(), mao_board_revision_mv());
    ESP_LOGI(TAG, "caps: led=%d display_power=%d i2c=%d expander=%d amp_switch=%d mic=%d touch=%d "
             "ir=%d power_status=%d charge_control=%d hall_fast=%d sleep=%d",
             c.rgb_led, c.display_power, c.i2c, c.expander, c.amp_switch, c.mic, c.touch,
             c.ir, c.power_status, c.charge_control, c.hall_fast, c.sleep);
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
    for (size_t i = 0; i < count && i < sizeof(found); i++) {
        n += snprintf(line + n, sizeof(line) - n, " 0x%02X", found[i]);
    }
    line[n] = '\0';
    ESP_LOGI(TAG, "i2c scan (%s): %u device(s):%s", esp_err_to_name(err), (unsigned)count, line);

    n = 0;
    char rails[96];
    for (int r = 0; r < MAO_RAIL_COUNT; r++) {
        n += snprintf(rails + n, sizeof(rails) - n, " %s=%s", mao_board_rail_name((mao_board_rail_t)r),
                      mao_board_rail_is_on((mao_board_rail_t)r) ? "on" : "off");
    }
    bool usb = false, chg = false, alert = false;
    mao_board_line_get(MAO_LINE_USB_PRESENT, &usb);
    const bool chg_wired = mao_board_line_get(MAO_LINE_CHARGING, &chg) == ESP_OK;
    mao_board_line_get(MAO_LINE_SENSE_ALERT, &alert);
    ESP_LOGI(TAG, "rails:%s; lines: usb=%d charging=%s sense_alert=%d", rails, usb,
             chg_wired ? (chg ? "1" : "0") : "- (see mao power)", alert);
}

void mao_board_common_init(void)
{
    mao_devcmd_register("board", "board  (caps, revision, I2C scan, rails)", devcmd_board);
}

#else

void mao_board_common_init(void)
{
}

#endif
