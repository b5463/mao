/*
 * ESP32-C3-LCDkit board implementation (the M0-M4.1 development platform).
 * Hardware this kit does not have is answered in mao_board_lcdkit_ext.c.
 */
#include "mao_board.h"
#include "mao_board_pins.h"
#include "mao_board_priv.h"

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
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "MAO_BOARD";

#define BACKLIGHT_LEDC_TIMER     LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL   LEDC_CHANNEL_0
/* Above hearing: at 5 kHz the backlight's switching current rode the shared
 * supply into the always-on NS4150 (no enable pin on the C3 board) as an
 * audible whine, so it runs at 30 kHz.
 * M4.1 sleep: the sleeping screen keeps its dim backlight while the chip is
 * in light sleep. Only the RC_FAST clock (~17.5 MHz) runs in light sleep, so
 * the timer uses it (LEDC_SLEEP_MODE_KEEP_ALIVE requires that): 30 kHz x
 * 9 bit = 15.4 MHz fits (10 bit at 30 kHz would not). */
#define BACKLIGHT_LEDC_RES       LEDC_TIMER_9_BIT
#define BACKLIGHT_LEDC_FREQ_HZ   30000

static bool s_backlight_ready;

esp_err_t mao_board_init(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_RETURN_ON_FALSE(chip.model == CHIP_ESP32C3, ESP_ERR_NOT_SUPPORTED, TAG,
                        "unexpected chip model %d", (int)chip.model);
    ESP_LOGI(TAG, "board: %s", MAO_BOARD_NAME);
    mao_board_deep_sleep_hold(false);   /* after MAO's own deep sleep: release the held pins */
    mao_board_common_init();
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
        .clk_cfg = LEDC_USE_RC_FAST_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");

    const ledc_channel_config_t channel = {
        .gpio_num = MAO_PIN_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE,   /* the sleeping screen stays lit (dimly) in light sleep */
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
        .rotation = 0,          /* the reference mounting */
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
        .press_turn_guard_ms = 0,    /* the EC11 is turned from the side */
    };
    return ESP_OK;
}

/* Light-sleep wake from the knob (M4.1): any GPIO can wake the C3 from LIGHT
 * sleep (level-triggered). Each encoder line is armed at the level it is not
 * at now, so any movement wakes; the switch wakes when pressed (low). The
 * encoder pins (IO6 / IO9 / IO10) are not RTC IOs: they cannot wake the C3
 * from DEEP sleep (only GPIO0-5 can) - see mao_power.h. */
esp_err_t mao_board_knob_wake_arm(bool arm)
{
    const int pins[3] = { MAO_PIN_ENC_A, MAO_PIN_ENC_B, MAO_PIN_ENC_SW };
    for (int i = 0; i < 3; i++) {
        if (arm) {
            const gpio_int_type_t lvl = i == 2 ? GPIO_INTR_LOW_LEVEL
                                        : (gpio_get_level(pins[i]) ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
            ESP_RETURN_ON_ERROR(gpio_wakeup_enable(pins[i], lvl), TAG, "wake arm");
        } else {
            gpio_wakeup_disable(pins[i]);
        }
    }
    return ESP_OK;
}

bool mao_board_switch_down(void)
{
    return gpio_get_level(MAO_PIN_ENC_SW) == 0;
}

/* Light sleep (M4.1 power): the NS4150 is always on (no enable pin), so the
 * PDM line (IO3) must stay driven low through the sleep - when the chip
 * sleeps IDF switches the pads to their sleep configuration, and that step
 * on an undriven line is a loud click (on entry and again at the wake). The
 * hold keeps the pad exactly as it is (the parked stream: low). */
void mao_board_audio_hold(bool hold)
{
    if (hold) {
        gpio_hold_en(MAO_PIN_AUDIO_PDM_DOUT);
    } else {
        gpio_hold_dis(MAO_PIN_AUDIO_PDM_DOUT);
    }
}

/* The PDM line at rest (M4.1 power). The C3's PDM modulator cannot make a
 * still line: its "floor" is a pulse train high ~38 % of the time (measured
 * on the pad; no gain setting goes below ~25 %), which the always-on NS4150
 * sees as a DC level. Stopping the channel drops that to 0 % in one step - a
 * loud click, and another when it starts again. So the line is handed to an
 * LEDC PWM at the same average level, which glides to 0 (and back) slowly
 * enough that the amplifier cannot pass it on; I2S stops and starts behind
 * it, off the pin. */
#define LINE_LEDC_TIMER    LEDC_TIMER_1
#define LINE_LEDC_CHANNEL  LEDC_CHANNEL_1
#define LINE_LEDC_RES      LEDC_TIMER_9_BIT       /* RC_FAST / 512 = ~34 kHz, above hearing */
#define LINE_LEDC_MAX      512
#define LINE_RAMP_MS       800                    /* the hardware fade, slow: its single steps stay below hearing */
static int s_line_idle_permille = 382;            /* the PDM floor's density (DEV: "power idle <n>") */

void mao_board_audio_line_idle(int permille)
{
    if (permille > 0 && permille < 1000) {
        s_line_idle_permille = permille;
    }
}

static void line_take(uint32_t duty)
{
    static bool timer_ready;
    if (!timer_ready) {
        const ledc_timer_config_t t = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LINE_LEDC_RES,
            .timer_num = LINE_LEDC_TIMER,
            .freq_hz = 34000,
            .clk_cfg = LEDC_USE_RC_FAST_CLK,      /* the C3's timers share one clock: the backlight's */
        };
        timer_ready = ledc_timer_config(&t) == ESP_OK;
    }
    const ledc_channel_config_t c = {
        .gpio_num = MAO_PIN_AUDIO_PDM_DOUT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LINE_LEDC_CHANNEL,
        .timer_sel = LINE_LEDC_TIMER,
        .duty = duty,
        .hpoint = 0,
    };
    ledc_channel_config(&c);                       /* the pin is the PWM's from here */
}

static void line_ramp(float to)
{
    /* the LEDC's own fade (installed with the backlight) steps the duty in
     * hardware, cycle by cycle: a software loop made a 100 Hz staircase the
     * amplifier played as a buzz */
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LINE_LEDC_CHANNEL, (uint32_t)lrintf(to * LINE_LEDC_MAX), LINE_RAMP_MS);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LINE_LEDC_CHANNEL, LEDC_FADE_WAIT_DONE);
}

/* The PDM stream is idling on its floor: take the line and bring it to rest. */
void mao_board_audio_line_rest(void)
{
    const float idle = (float)s_line_idle_permille / 1000.0f;
    line_take((uint32_t)lrintf(idle * LINE_LEDC_MAX));
    line_ramp(0.0f);
    ledc_stop(LEDC_LOW_SPEED_MODE, LINE_LEDC_CHANNEL, 0);   /* still, low */
}

/* Before the PDM stream starts again: the line glides up to its floor level. */
void mao_board_audio_line_rise(void)
{
    const float idle = (float)s_line_idle_permille / 1000.0f;
    line_take(0);
    line_ramp(idle);
}

/* The PDM stream runs on its floor again: give it the line back. */
void mao_board_audio_line_attach(void)
{
    esp_rom_gpio_connect_out_signal(MAO_PIN_AUDIO_PDM_DOUT, I2SO_SD_OUT_IDX, false, false);
    ledc_stop(LEDC_LOW_SPEED_MODE, LINE_LEDC_CHANNEL, 0);   /* off the pin now: stopped quietly */
}

/* DEV: the idle PDM line's pulse density, read back from its own pad
 * (the input path works while the pad is an output). 0..1000 = per mille. */
int mao_board_audio_duty_permille(void)
{
    gpio_input_enable(MAO_PIN_AUDIO_PDM_DOUT);
    uint32_t hi = 0;
    const uint32_t n = 200000;
    for (uint32_t i = 0; i < n; i++) {
        hi += (uint32_t)gpio_get_level(MAO_PIN_AUDIO_PDM_DOUT);
    }
    return (int)(hi * 1000u / n);
}

/* Before the DEV-only deep sleep: the backlight (IO5) and the PDM line to
 * the amplifier (IO3) are RTC IOs - held low through deep sleep, so the
 * screen is dark and the speaker quiet. Released at the next boot. */
void mao_board_deep_sleep_hold(bool hold)
{
    const int pins[2] = { MAO_PIN_LCD_BACKLIGHT, MAO_PIN_AUDIO_PDM_DOUT };
    for (int i = 0; i < 2; i++) {
        if (hold) {
            gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
            gpio_set_level(pins[i], 0);
            gpio_hold_en(pins[i]);
        } else {
            gpio_hold_dis(pins[i]);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Audio                                                                    */
/* ------------------------------------------------------------------------ */

esp_err_t mao_board_audio_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    /* Small DMA ring: 3 x 10 ms at 16 kHz keeps latency low and RAM tiny.
     * The channel stays enabled permanently and mao_audio keeps it fed (it
     * idles at a low floor, see mao_audio.c); auto_clear only covers an
     * unexpected underrun. */
    chan_cfg.dma_desc_num = 3;
    chan_cfg.dma_frame_num = sample_rate_hz / 100;
    chan_cfg.auto_clear = true;

    i2s_chan_handle_t tx = NULL;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, NULL), TAG, "i2s channel");

    i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = GPIO_NUM_NC,
            .dout = MAO_PIN_AUDIO_PDM_DOUT,
            .invert_flags = { .clk_inv = false },
        },
    };
    /* The audio engine idles at the bottom of the range (see mao_audio.c);
     * the PDM high-pass would drag that back to mid scale. */
    pdm_cfg.slot_cfg.hp_en = false;
    /* No modulator dither either: with it the floor is never truly static
     * and the idle line rings. */
    pdm_cfg.slot_cfg.sd_dither = 0;
    pdm_cfg.slot_cfg.sd_dither2 = 0;
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
        *out = (mao_board_ir_t) {
            .gpio = MAO_PIN_IR,
            .mode_known = false,
            .tx_gpio = -1,
            .rx_gpio = -1,
        };
    }
}
