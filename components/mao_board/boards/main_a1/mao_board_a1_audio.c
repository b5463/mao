/*
 * MAO_MAIN A1 audio: MAX98357A speaker amplifier on I2S0 (standard Philips
 * TX: BCLK, LRCLK, DIN), 16-bit mono written to both slots.
 *
 * SD_MODE is MAO_RAIL_AMP (GPIO, 100 k pull-down + the amplifier's own
 * 100 k): high = on, left channel; low = shutdown. The MAX98357A needs its
 * clocks before SD_MODE rises and takes 7-7.5 ms to turn on, so mao_audio
 * starts the stream at silence, raises SD_MODE, waits >= 7.5 ms, plays, and
 * does it in reverse to stop. A standard I2S stream has a true zero, so none
 * of the LCDkit's PDM-line tricks (floor, LEDC glide) are needed here: those
 * entry points are no-ops on this board.
 */
#include "mao_board.h"
#include "mao_board_a1_priv.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "MAO_BOARD";

/* The full-scale gain, never above A1_AUDIO_GAIN_MAX (0.8 W into the 8 ohm
 * speaker at VSYS 4.5 V on USB power: mao_board_a1_priv.h). */
float mao_board_audio_gain(void)
{
    const float gain = A1_AUDIO_GAIN;
    return gain < A1_AUDIO_GAIN_MAX ? gain : A1_AUDIO_GAIN_MAX;
}

esp_err_t mao_board_audio_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(A1_AMP_I2S_PORT, I2S_ROLE_MASTER);
    /* Same DMA shape as the LCDkit: 3 x 10 ms; auto_clear plays silence (a
     * true zero on I2S) if the engine ever underruns. */
    chan_cfg.dma_desc_num = 3;
    chan_cfg.dma_frame_num = sample_rate_hz / 100;
    chan_cfg.auto_clear = true;

    i2s_chan_handle_t tx = NULL;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, NULL), TAG, "i2s channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = MAO_PIN_AMP_BCLK,
            .ws = MAO_PIN_AMP_LRCLK,
            .dout = MAO_PIN_AMP_DIN,
            .din = GPIO_NUM_NC,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    /* Mono sample in both slots: the amplifier plays the left one. */
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    esp_err_t err = i2s_channel_init_std_mode(tx, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(tx);
        ESP_LOGE(TAG, "std tx init: %s", esp_err_to_name(err));
        return err;
    }
    *out = tx;
    ESP_LOGI(TAG, "audio: I2S0 standard TX mono 16-bit @ %u Hz -> MAX98357A (left slot)", (unsigned)sample_rate_hz);
    return ESP_OK;
}

/* Light sleep: SD_MODE is low (mao_audio suspended); hold it there so the
 * pad's sleep configuration cannot lift it. */
void mao_board_audio_hold(bool hold)
{
    if (hold) {
        gpio_hold_en((gpio_num_t)MAO_PIN_AMP_SD);
    } else {
        gpio_hold_dis((gpio_num_t)MAO_PIN_AMP_SD);
    }
}

void mao_board_audio_line_rest(void)
{
}

void mao_board_audio_line_rise(void)
{
}

void mao_board_audio_line_attach(void)
{
}

void mao_board_audio_line_idle(int permille)
{
    (void)permille;
}

int mao_board_audio_duty_permille(void)
{
    return -1;
}
