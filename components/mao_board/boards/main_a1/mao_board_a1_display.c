/*
 * MAO_MAIN A1 display: a 1.28" round GC9A01 panel (240x240) on SPI2,
 * CONFIG_MAO_A1_LCD_PCLK_MHZ (80 MHz; 40 if the tail needs it), write-only:
 * SCLK, MOSI and CS on SPI2's IO_MUX pads (FSPICLK / FSPID / FSPICS0), DC a
 * plain GPIO. TE (tearing effect) is an input on MAO_PIN_LCD_TE; the flush
 * does not wait for it yet (VERIFY AT BRING-UP). The panel logic sits behind
 * a load switch (LCD_PWR_EN) and the panel reset is LCD_RST_N, both GPIOs
 * with pull-downs (panel unpowered and in reset from power-on).
 *
 * Backlight: LEDC PWM at 30 kHz on MAO_PIN_LCD_BL into the analogue current
 * sink's reference - the same LEDC set-up as the LCDkit (RC_FAST clock, kept
 * alive in light sleep, hardware fades), so the M4.1 rest level (3 %) and
 * the fades behave as there. The LEDs are not on the switched panel rail:
 * every rail-off path sets the backlight to 0 first.
 *
 * Power-up: rail on, 10 ms, reset low 10 ms, reset high, 120 ms, then the
 * GC9A01 init. With the rail off the bus is driven low (and held for deep
 * sleep): an idle-high CS or MOSI would back-power the unpowered panel. The
 * TE input has its internal pull-down on while the rail is off (nothing
 * drives it then) and off while the panel is powered (review N3).
 */
#include "mao_board.h"
#include "mao_board_a1_priv.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_private/gpio.h"     /* gpio_iomux_output(), gpio_func_sel(): give parked pins back to SPI2 */
#include "esp_rom_gpio.h"
#include "soc/spi_periph.h"
#include "esp_check.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "MAO_BOARD";

#define LCD_SPI_HOST             SPI2_HOST
#define BL_LEDC_TIMER            LEDC_TIMER_0
#define BL_LEDC_CHANNEL          LEDC_CHANNEL_0
/* RC_FAST (~17.5 MHz) is the only LEDC clock alive in light sleep (the
 * sleeping screen keeps its glow): 30 kHz x 9 bit = 15.4 MHz fits. */
#define BL_LEDC_RES              LEDC_TIMER_9_BIT

static const int kBusPins[] = { MAO_PIN_LCD_SCLK, MAO_PIN_LCD_MOSI, MAO_PIN_LCD_CS, MAO_PIN_LCD_DC };

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_backlight_ready;
static bool s_parked;
static bool s_oriented;      /* panel orientation set once; the driver keeps it since */

/* ------------------------------------------------------------------------ */
/* Backlight                                                                */
/* ------------------------------------------------------------------------ */

static uint32_t bl_duty(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    return (((1u << BL_LEDC_RES) - 1) * percent) / 100;
}

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = A1_BL_FREQ_HZ,
        .clk_cfg = LEDC_USE_RC_FAST_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    const ledc_channel_config_t channel = {
        .gpio_num = MAO_PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
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
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, bl_duty(percent)), TAG, "duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

esp_err_t mao_board_backlight_fade(uint8_t percent, uint32_t fade_ms)
{
    ESP_RETURN_ON_FALSE(s_backlight_ready, ESP_ERR_INVALID_STATE, TAG, "backlight not initialised");
    if (fade_ms == 0) {
        return mao_board_backlight_set(percent);
    }
    /* Hardware fade: no CPU involvement, retargets if a fade is running. */
    return ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, bl_duty(percent), fade_ms,
                                        LEDC_FADE_NO_WAIT);
}

void a1_backlight_hold(bool hold)
{
    if (hold) {
        if (s_backlight_ready) {
            ledc_stop(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, 0);
        }
        gpio_set_level((gpio_num_t)MAO_PIN_LCD_BL, 0);
        gpio_set_direction((gpio_num_t)MAO_PIN_LCD_BL, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)MAO_PIN_LCD_BL, 0);
        gpio_hold_en((gpio_num_t)MAO_PIN_LCD_BL);
    } else {
        gpio_hold_dis((gpio_num_t)MAO_PIN_LCD_BL);
    }
}

/* ------------------------------------------------------------------------ */
/* Rail sequencing                                                          */
/* ------------------------------------------------------------------------ */

void a1_display_park(bool deep)
{
    for (size_t i = 0; i < sizeof(kBusPins) / sizeof(kBusPins[0]); i++) {
        /* gpio_config() moves the pad from the SPI IO_MUX function to plain
         * GPIO, so the driven-low level wins over the idle bus. */
        const gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << kBusPins[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
        gpio_set_level((gpio_num_t)kBusPins[i], 0);
        if (deep) {
            gpio_hold_en((gpio_num_t)kBusPins[i]);   /* RTC pads: held low through deep sleep */
        }
    }
    s_parked = true;
}

static void unpark(void)
{
    /* SCLK and MOSI back on SPI2's native IO_MUX function (as
     * spi_bus_initialize() chose); CS through the GPIO matrix, which works
     * whichever routing the device was given. DC stays a plain GPIO that
     * esp_lcd drives per transaction. */
    const int func = spi_periph_signal[LCD_SPI_HOST].func;
    gpio_iomux_output((gpio_num_t)MAO_PIN_LCD_SCLK, func);
    gpio_iomux_output((gpio_num_t)MAO_PIN_LCD_MOSI, func);
    gpio_func_sel((gpio_num_t)MAO_PIN_LCD_CS, PIN_FUNC_GPIO);
    esp_rom_gpio_connect_out_signal(MAO_PIN_LCD_CS, spi_periph_signal[LCD_SPI_HOST].spics_out[0], false, false);
    s_parked = false;
}

void a1_display_te_pull(bool rail_off, bool hold)
{
    gpio_hold_dis((gpio_num_t)MAO_PIN_LCD_TE);
    if (rail_off) {
        gpio_pulldown_en((gpio_num_t)MAO_PIN_LCD_TE);
    } else {
        gpio_pulldown_dis((gpio_num_t)MAO_PIN_LCD_TE);
    }
    if (hold) {
        gpio_hold_en((gpio_num_t)MAO_PIN_LCD_TE);
    }
}

static esp_err_t rail_up(void)
{
    gpio_set_level((gpio_num_t)MAO_PIN_LCD_RST_N, 0);
    gpio_set_level((gpio_num_t)MAO_PIN_LCD_PWR_EN, 1);
    a1_display_te_pull(false, false);                  /* the panel drives TE from here */
    vTaskDelay(pdMS_TO_TICKS(A1_LCD_PWR_SETTLE_MS));
    vTaskDelay(pdMS_TO_TICKS(A1_LCD_RESET_LOW_MS));   /* reset held low with the rail up */
    gpio_set_level((gpio_num_t)MAO_PIN_LCD_RST_N, 1);
    vTaskDelay(pdMS_TO_TICKS(A1_LCD_RESET_WAIT_MS));
    return ESP_OK;
}

static esp_err_t panel_configure(void)
{
    /* The hardware reset already happened; the software reset esp_lcd
     * issues without a reset GPIO is redundant but harmless. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, A1_LCD_INVERT_COLOR), TAG, "invert");
    if (!s_oriented) {
        /* Only the first time: afterwards the display layer owns the
         * orientation (rotation), and the GC9A01 driver re-sends its last
         * MADCTL from esp_lcd_panel_init() after a power cycle. */
        ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, A1_LCD_PANEL_SWAP_XY), TAG, "swap_xy");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, A1_LCD_PANEL_MIRROR_X, A1_LCD_PANEL_MIRROR_Y), TAG,
                            "mirror");
        s_oriented = true;
    }
    return ESP_OK;
}

esp_err_t a1_display_power(bool on)
{
    if (!on) {
        if (s_backlight_ready) {
            mao_board_backlight_set(0);
        }
        a1_display_park(false);
        gpio_set_level((gpio_num_t)MAO_PIN_LCD_RST_N, 0);
        const esp_err_t err = gpio_set_level((gpio_num_t)MAO_PIN_LCD_PWR_EN, 0);
        a1_display_te_pull(true, false);               /* nothing drives TE now */
        return err;
    }
    if (gpio_get_level((gpio_num_t)MAO_PIN_LCD_PWR_EN)) {
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
        *h_res = A1_LCD_H_RES;
    }
    if (v_res) {
        *v_res = A1_LCD_V_RES;
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
        .pclk_hz = A1_LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,              /* reset sequenced by rail_up() */
        .rgb_ele_order = A1_LCD_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(s_io, &panel_cfg, &s_panel), TAG, "gc9a01");
    ESP_RETURN_ON_ERROR(panel_configure(), TAG, "panel");

    *out = (mao_board_display_t) {
        .io = s_io,
        .panel = s_panel,
        .h_res = A1_LCD_H_RES,
        .v_res = A1_LCD_V_RES,
        .mirror_x = A1_LCD_PANEL_MIRROR_X,
        .mirror_y = A1_LCD_PANEL_MIRROR_Y,
        .swap_xy = A1_LCD_PANEL_SWAP_XY,
        .swap_bytes = A1_LCD_SWAP_BYTES,
        .rotation = A1_LCD_ROTATION_DEG,
    };
    ESP_LOGI(TAG, "display: GC9A01 %dx%d, SPI2 @ %d MHz, max transfer %u B, switched rail, mounted at %d deg",
             A1_LCD_H_RES, A1_LCD_V_RES, A1_LCD_PCLK_HZ / 1000000, (unsigned)max_transfer_bytes,
             A1_LCD_ROTATION_DEG);
    return ESP_OK;
}
