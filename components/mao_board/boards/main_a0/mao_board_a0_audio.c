/*
 * MAO_MAIN A0 audio: MAX98357A speaker amplifier on I2S1 (standard Philips
 * TX) and the Knowles SPH0641LU4H-1 PDM microphone on I2S0 (PDM RX with the
 * hardware PDM->PCM filter).
 *
 * The amplifier's SD_MODE pin is expander AMP_SD_N (MAO_RAIL_AMP): driven at
 * 3.3 V it plays the LEFT slot, low it shuts down (0.6 uA). Mono samples go
 * to both slots, so the slot choice cannot silence it.
 */
#include "mao_board.h"
#include "mao_board_a0_priv.h"

#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "MAO_BOARD";

#define MIC_DMA_DESC_NUM     4
#define MIC_DMA_FRAME_NUM    256     /* 16 ms per descriptor at 16 kHz */

float mao_board_audio_gain(void)
{
    return A0_AUDIO_GAIN;
}

esp_err_t mao_board_audio_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(A0_AMP_I2S_PORT, I2S_ROLE_MASTER);
    /* Same DMA shape as the LCDkit: 3 x 10 ms, auto_clear plays silence
     * between sounds so the channel can stay enabled (no pops). */
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
    ESP_LOGI(TAG, "audio: I2S1 standard TX mono 16-bit @ %u Hz -> MAX98357A (left slot)",
             (unsigned)sample_rate_hz);
    return ESP_OK;
}

esp_err_t mao_board_mic_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(A0_MIC_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = MIC_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = MIC_DMA_FRAME_NUM;

    i2s_chan_handle_t rx = NULL;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &rx), TAG, "i2s channel");

    /* Default down-sampling (DSR 8S): PDM clock = 64 x 16 kHz = 1.024 MHz,
     * inside the mic's standard-performance clock range. SELECT is tied low,
     * which IDF calls the left PDM slot (the mono default). */
    const i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = MAO_PIN_MIC_CLK,
            .din = MAO_PIN_MIC_DATA,
            .invert_flags = { .clk_inv = A0_MIC_CLK_INVERT },
        },
    };
    esp_err_t err = i2s_channel_init_pdm_rx_mode(rx, &pdm_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(rx);
        ESP_LOGE(TAG, "pdm rx init: %s", esp_err_to_name(err));
        return err;
    }
    *out = rx;
    ESP_LOGI(TAG, "mic: I2S0 PDM RX -> PCM mono 16-bit @ %u Hz, left slot (SELECT low)",
             (unsigned)sample_rate_hz);
    return ESP_OK;
}
