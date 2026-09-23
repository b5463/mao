#include "mao_board.h"
#include "mao_board_pins.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/i2s_pdm.h"
#include "esp_chip_info.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "esp_log.h"

static const char *TAG = "MAO_BOARD";

#define BACKLIGHT_LEDC_TIMER     LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_RES       LEDC_TIMER_10_BIT
#define BACKLIGHT_LEDC_FREQ_HZ   5000

static bool s_backlight_ready;

esp_err_t mao_board_init(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_RETURN_ON_FALSE(chip.model == CHIP_ESP32C3, ESP_ERR_NOT_SUPPORTED, TAG,
                        "unexpected chip model %d", (int)chip.model);
    ESP_LOGI(TAG, "board: %s", MAO_BOARD_NAME);
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Display                                                                  */
/* ------------------------------------------------------------------------ */

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BACKLIGHT_LEDC_RES,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");

    const ledc_channel_config_t channel = {
        .gpio_num = MAO_PIN_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight channel");
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0), TAG, "backlight fade");
    s_backlight_ready = true;
    return ESP_OK;
}

esp_err_t mao_board_backlight_set(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(s_backlight_ready, ESP_ERR_INVALID_STATE, TAG, "backlight not initialised");
    if (percent > 100) {
        percent = 100;
    }
    const uint32_t max_duty = (1u << BACKLIGHT_LEDC_RES) - 1;
    const uint32_t duty = (max_duty * percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL, duty), TAG, "duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL);
}

void mao_board_display_get_resolution(uint16_t *h_res, uint16_t *v_res)
{
    if (h_res) {
        *h_res = MAO_LCD_H_RES;
    }
    if (v_res) {
        *v_res = MAO_LCD_V_RES;
    }
}

esp_err_t mao_board_display_init(size_t max_transfer_bytes, mao_board_display_t *out)
{
    ESP_RETURN_ON_FALSE(out && max_transfer_bytes > 0, ESP_ERR_INVALID_ARG, TAG, "bad args");

    /* Backlight first, at 0 %, so the panel's power-on garbage is never visible. */
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    const spi_bus_config_t bus = {
        .mosi_io_num = MAO_PIN_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = MAO_PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)max_transfer_bytes,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(MAO_PIN_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = MAO_PIN_LCD_CS,
        .dc_gpio_num = MAO_PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = MAO_LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)MAO_PIN_LCD_SPI_HOST,
                                                 &io_cfg, &io), TAG, "panel io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = MAO_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(io, &panel_cfg, &panel), TAG, "gc9a01");

    /* No reset line: esp_lcd_panel_reset() issues the software reset command. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel, false), TAG, "swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel, true, false), TAG, "mirror");

    *out = (mao_board_display_t) {
        .io = io,
        .panel = panel,
        .h_res = MAO_LCD_H_RES,
        .v_res = MAO_LCD_V_RES,
        .mirror_x = true,
        .mirror_y = false,
        .swap_xy = false,
        .swap_bytes = true,
    };
    ESP_LOGI(TAG, "display: GC9A01 %dx%d, SPI2 @ %d MHz, max transfer %u B",
             MAO_LCD_H_RES, MAO_LCD_V_RES, MAO_LCD_PCLK_HZ / 1000000, (unsigned)max_transfer_bytes);
    return ESP_OK;
}

esp_err_t mao_board_backlight_fade(uint8_t percent, uint32_t fade_ms)
{
    ESP_RETURN_ON_FALSE(s_backlight_ready, ESP_ERR_INVALID_STATE, TAG, "backlight not initialised");
    if (fade_ms == 0) {
        return mao_board_backlight_set(percent);
    }
    if (percent > 100) {
        percent = 100;
    }
    const uint32_t duty = (((1u << BACKLIGHT_LEDC_RES) - 1) * percent) / 100;
    /* Hardware fade: no CPU involvement, retargets if a fade is running. */
    return ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL, duty, fade_ms,
                                        LEDC_FADE_NO_WAIT);
}

/* ------------------------------------------------------------------------ */
/* Input                                                                    */
/* ------------------------------------------------------------------------ */

esp_err_t mao_board_input_init(mao_board_encoder_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    /* Input-only configuration. GPIO9 is the BOOT strap: never an output. */
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << MAO_PIN_ENC_A) | (1ULL << MAO_PIN_ENC_B) | (1ULL << MAO_PIN_ENC_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "encoder gpio");

    *out = (mao_board_encoder_t) {
        .gpio_a = MAO_PIN_ENC_A,
        .gpio_b = MAO_PIN_ENC_B,
        .gpio_switch = MAO_PIN_ENC_SW,
        .switch_active_low = true,
        .transitions_per_detent = MAO_ENC_TRANSITIONS_PER_DETENT,
        .rest_mask = MAO_ENC_REST_MASK,
        .detents_per_rev = MAO_ENC_DETENTS_PER_REV,
        .reverse = MAO_ENC_REVERSE,
    };
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Audio                                                                    */
/* ------------------------------------------------------------------------ */

esp_err_t mao_board_audio_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    /* Small DMA ring: 3 x 10 ms at 16 kHz keeps latency low and RAM tiny.
     * auto_clear makes the DMA emit silence whenever no data is queued, so
     * the channel can stay enabled permanently (no pops from start/stop). */
    chan_cfg.dma_desc_num = 3;
    chan_cfg.dma_frame_num = sample_rate_hz / 100;
    chan_cfg.auto_clear = true;

    i2s_chan_handle_t tx = NULL;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, NULL), TAG, "i2s channel");

    const i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = GPIO_NUM_NC,
            .dout = MAO_PIN_AUDIO_PDM_DOUT,
            .invert_flags = { .clk_inv = false },
        },
    };
    esp_err_t err = i2s_channel_init_pdm_tx_mode(tx, &pdm_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(tx);
        ESP_LOGE(TAG, "pdm tx init: %s", esp_err_to_name(err));
        return err;
    }
    *out = tx;
    ESP_LOGI(TAG, "audio: I2S0 PDM TX mono 16-bit @ %u Hz", (unsigned)sample_rate_hz);
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* RGB LED                                                                  */
/* ------------------------------------------------------------------------ */

esp_err_t mao_board_led_init(led_strip_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    const led_strip_config_t strip_cfg = {
        .strip_gpio_num = MAO_PIN_LED_RGB,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false },
    };
    const led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,          /* driver default for this chip */
        .flags = { .with_dma = false },  /* ESP32-C3 RMT has no DMA */
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, out), TAG, "led strip");
    ESP_LOGI(TAG, "led: WS2812 x1 on RMT");
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* IR                                                                       */
/* ------------------------------------------------------------------------ */

void mao_board_ir_get(mao_board_ir_t *out)
{
    if (out) {
        out->gpio = MAO_PIN_IR;
        out->mode_known = false;
    }
}
