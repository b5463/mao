/*
 * ESP32-C3-LCDkit: answers for the board API the kit has no hardware for.
 *
 * The kit has a display, the EC11 knob, a PDM speaker, one WS2812 and a
 * jumpered IR line, and nothing else: no I2C devices, rails, sensors,
 * battery or sleep wiring. Everything here reports "not fitted" and touches
 * no pin, so the kit behaves exactly as before the A0 work.
 */
#include <string.h>
#include "mao_board.h"

/* The NS4150 is loud; this is the M0-M2 level (see mao_audio). */
#define LCDKIT_AUDIO_GAIN   0.58f

void mao_board_get_caps(mao_board_caps_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
        out->rgb_led = true;
    }
}

const char *mao_board_revision(void)
{
    return "-";
}

int mao_board_revision_mv(void)
{
    return -1;
}

esp_err_t mao_board_display_sleep(bool sleep)
{
    (void)sleep;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_hall_fast_set(bool fast)
{
    (void)fast;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_touch_get(mao_board_touch_t *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

float mao_board_audio_gain(void)
{
    return LCDKIT_AUDIO_GAIN;
}

esp_err_t mao_board_mic_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out)
{
    (void)sample_rate_hz;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_i2c_bus(i2c_master_bus_handle_t *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_i2c_device(mao_board_i2c_dev_t dev, mao_board_i2c_info_t *out)
{
    (void)dev;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_i2c_add(mao_board_i2c_dev_t dev, i2c_master_dev_handle_t *out)
{
    (void)dev;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_i2c_scan(uint8_t *found, size_t max, size_t *count)
{
    (void)found;
    (void)max;
    if (count) {
        *count = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_rail_set(mao_board_rail_t rail, bool on)
{
    (void)rail;
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
}

bool mao_board_rail_is_on(mao_board_rail_t rail)
{
    (void)rail;
    return false;
}

esp_err_t mao_board_rail_readback(mao_board_rail_t rail, bool *on)
{
    (void)rail;
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_expander_test(uint8_t *failed_bits)
{
    if (failed_bits) {
        *failed_bits = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

uint32_t mao_board_expander_resets(void)
{
    return 0;
}

esp_err_t mao_board_expander_reset_test(uint8_t *config_after_pulse, uint8_t *failed_bits)
{
    if (config_after_pulse) {
        *config_after_pulse = 0;
    }
    if (failed_bits) {
        *failed_bits = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_display_reinit(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_line_get(mao_board_line_t line, bool *active)
{
    (void)line;
    (void)active;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_irq_get(mao_board_irq_t irq, mao_board_irq_desc_t *out)
{
    (void)irq;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_deep_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed)
{
    (void)want;
    if (armed) {
        memset(armed, 0, sizeof(*armed));
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mao_board_light_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed)
{
    (void)want;
    if (armed) {
        memset(armed, 0, sizeof(*armed));
    }
    return ESP_ERR_NOT_SUPPORTED;
}

void mao_board_light_sleep_done(void)
{
}

void mao_board_wake_decode(mao_board_wake_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}
