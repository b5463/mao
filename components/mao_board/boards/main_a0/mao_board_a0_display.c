/*
 * MAO_MAIN A0 display: Limito LH128R-IG01 (GC9A01, 240x240) on SPI2 via the
 * IO_MUX pins, 80 MHz, write-only. The panel and backlight sit behind a load
 * switch (expander LCD_PWR_EN) and the panel reset is expander LCD_RST_N.
 *
 * Power-up: rail on, 10 ms, reset low 10 ms, reset high, 120 ms, then the
 * GC9A01 init. With the rail off, the bus is driven low (or isolated for
 * deep sleep): an idle-high CS or MOSI would otherwise back-power the
 * unpowered panel through its input protection.
 */
#include "mao_board.h"
#include "mao_board_a0_priv.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "esp_private/gpio.h"     /* gpio_iomux_output(): give parked pins back to SPI2 */
#include "soc/spi_periph.h"
#include "esp_check.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "MAO_BOARD";

#define LCD_SPI_HOST             SPI2_HOST
#define BACKLIGHT_LEDC_TIMER     LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_RES       LEDC_TIMER_10_BIT
#define BACKLIGHT_LEDC_FREQ_HZ   5000
#define SLEEP_OUT_WAIT_MS        120     /* GC9A01: sleep-out to next sleep-in / full operation */

static const int kBusPins[] = { MAO_PIN_LCD_SCLK, MAO_PIN_LCD_MOSI, MAO_PIN_LCD_CS, MAO_PIN_LCD_DC };

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_backlight_ready;
static bool s_parked;
static bool s_oriented;      /* panel orientation set once; the driver keeps it since */

/* ------------------------------------------------------------------------ */
/* Backlight                                                                */
/* ------------------------------------------------------------------------ */

static esp_err_t backlight_init(void)
{
    /* GPIO46 is a strap; its 100 k pull-down keeps the backlight off until
     * LEDC takes the pin here, long after boot, at 0 % duty. */
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BACKLIGHT_LEDC_RES,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    const ledc_channel_config_t channel = {
        .gpio_num = MAO_PIN_LCD_BL_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight channel");
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

/* ------------------------------------------------------------------------ */
/* Rail sequencing                                                          */
/* ------------------------------------------------------------------------ */

void a0_display_park(bool deep)
{
    for (size_t i = 0; i < sizeof(kBusPins) / sizeof(kBusPins[0]); i++) {
        if (deep) {
            /* High-Z, no pulls, held through deep sleep (all four are RTC
             * pads). Released by a0_sleep_release_pads() at the next boot. */
            rtc_gpio_isolate((gpio_num_t)kBusPins[i]);
        } else {
            /* gpio_config() moves the pad from the SPI IO_MUX function to
             * plain GPIO, so the driven-low level wins over the idle bus. */
            const gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << kBusPins[i],
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&cfg);
            gpio_set_level((gpio_num_t)kBusPins[i], 0);
        }
    }
    s_parked = true;
}

static void unpark(void)
{
    /* Same routing spi_bus_initialize() / spi_bus_add_device() chose: the
     * native IO_MUX function of SPI2. DC stays a plain GPIO that esp_lcd
     * drives per transaction. */
    const int func = spi_periph_signal[LCD_SPI_HOST].func;
    gpio_iomux_output((gpio_num_t)MAO_PIN_LCD_SCLK, func);
    gpio_iomux_output((gpio_num_t)MAO_PIN_LCD_MOSI, func);
    gpio_iomux_output((gpio_num_t)MAO_PIN_LCD_CS, func);
    s_parked = false;
}

static esp_err_t rail_up(void)
{
    ESP_RETURN_ON_FALSE(a0_expander_ok(), ESP_ERR_INVALID_STATE, TAG, "display rail: expander missing");
    ESP_RETURN_ON_ERROR(a0_expander_write_bit(MAO_EXP_LCD_RST_N, false), TAG, "reset low");
    ESP_RETURN_ON_ERROR(a0_expander_write_bit(MAO_EXP_LCD_PWR_EN, true), TAG, "rail on");
    vTaskDelay(pdMS_TO_TICKS(A0_LCD_PWR_SETTLE_MS));
    vTaskDelay(pdMS_TO_TICKS(A0_LCD_RESET_LOW_MS));   /* reset held low with the rail up */
    ESP_RETURN_ON_ERROR(a0_expander_write_bit(MAO_EXP_LCD_RST_N, true), TAG, "reset high");
    vTaskDelay(pdMS_TO_TICKS(A0_LCD_RESET_WAIT_MS));
    return ESP_OK;
}

static esp_err_t panel_configure(void)
{
    /* The hardware reset already happened; the software reset that esp_lcd
     * issues without a reset GPIO is redundant but harmless. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, A0_LCD_INVERT_COLOR), TAG, "invert");
    if (!s_oriented) {
        /* Only the first time: afterwards the display layer owns the
         * orientation (rotation), and the GC9A01 driver re-sends its last
         * MADCTL from esp_lcd_panel_init() after a power cycle. */
        ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, A0_LCD_PANEL_SWAP_XY), TAG, "swap_xy");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, A0_LCD_PANEL_MIRROR_X, A0_LCD_PANEL_MIRROR_Y), TAG,
                            "mirror");
        s_oriented = true;
    }
    return ESP_OK;
}

esp_err_t a0_display_power(bool on)
{
    if (!on) {
        if (s_backlight_ready) {
            mao_board_backlight_set(0);
        }
        a0_display_park(false);
        a0_expander_write_bit(MAO_EXP_LCD_RST_N, false);
        return a0_expander_write_bit(MAO_EXP_LCD_PWR_EN, false);
    }
    if (a0_expander_bit(MAO_EXP_LCD_PWR_EN)) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(rail_up(), TAG, "rail up");
    if (s_panel) {
        /* Powered again after a runtime power-down: the controller lost its
         * registers and RAM. It comes back display-off and blank; the owner
         * switches output on and redraws. */
        if (s_parked) {
            unpark();
        }
        ESP_RETURN_ON_ERROR(panel_configure(), TAG, "panel");
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Public display API                                                       */
/* ------------------------------------------------------------------------ */

void mao_board_display_get_resolution(uint16_t *h_res, uint16_t *v_res)
{
    if (h_res) {
        *h_res = A0_LCD_H_RES;
    }
    if (v_res) {
        *v_res = A0_LCD_V_RES;
    }
}

esp_err_t mao_board_display_init(size_t max_transfer_bytes, mao_board_display_t *out)
{
    ESP_RETURN_ON_FALSE(out && max_transfer_bytes > 0, ESP_ERR_INVALID_ARG, TAG, "bad args");

    /* Backlight first, at 0 %, so the panel's power-on garbage is never visible. */
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(rail_up(), TAG, "display rail");

    const spi_bus_config_t bus = {
        .mosi_io_num = MAO_PIN_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = MAO_PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)max_transfer_bytes,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = MAO_PIN_LCD_CS,
        .dc_gpio_num = MAO_PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = A0_LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,              /* reset is on the expander */
        .rgb_ele_order = A0_LCD_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(s_io, &panel_cfg, &s_panel), TAG, "gc9a01");
    ESP_RETURN_ON_ERROR(panel_configure(), TAG, "panel");

    *out = (mao_board_display_t) {
        .io = s_io,
        .panel = s_panel,
        .h_res = A0_LCD_H_RES,
        .v_res = A0_LCD_V_RES,
        .mirror_x = A0_LCD_PANEL_MIRROR_X,
        .mirror_y = A0_LCD_PANEL_MIRROR_Y,
        .swap_xy = A0_LCD_PANEL_SWAP_XY,
        .swap_bytes = A0_LCD_SWAP_BYTES,
        .rotation = A0_LCD_ROTATION_DEG,
    };
    ESP_LOGI(TAG, "display: GC9A01 %dx%d, SPI2 @ %d MHz, max transfer %u B, rail via expander, mounted at %d deg",
             A0_LCD_H_RES, A0_LCD_V_RES, A0_LCD_PCLK_HZ / 1000000, (unsigned)max_transfer_bytes,
             A0_LCD_ROTATION_DEG);
    return ESP_OK;
}

esp_err_t mao_board_display_reinit(void)
{
    ESP_RETURN_ON_FALSE(s_panel, ESP_ERR_INVALID_STATE, TAG, "display not initialised");
    ESP_RETURN_ON_FALSE(a0_expander_bit(MAO_EXP_LCD_PWR_EN), ESP_ERR_INVALID_STATE, TAG, "display rail off");
    /* The expander reprogram already released RST_N and restored the rail:
     * give the controller its reset time, then the full init (the driver
     * re-sends the current MADCTL, so the rotation is kept). */
    vTaskDelay(pdMS_TO_TICKS(A0_LCD_RESET_WAIT_MS));
    return panel_configure();
}

esp_err_t mao_board_display_sleep(bool sleep)
{
    ESP_RETURN_ON_FALSE(s_io, ESP_ERR_INVALID_STATE, TAG, "display not initialised");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, sleep ? LCD_CMD_SLPIN : LCD_CMD_SLPOUT, NULL, 0),
                        TAG, "sleep cmd");
    if (!sleep) {
        vTaskDelay(pdMS_TO_TICKS(SLEEP_OUT_WAIT_MS));
    }
    return ESP_OK;
}
