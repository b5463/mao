/*
 * MAO_MAIN A0 display: a 1.28" round GC9A01 panel (240x240) with the 18-pin
 * plug-in tail (Winstar WF0128BTYAA4DNN0) in the J301 FPC connector, on SPI2,
 * 80 MHz, write-only: SCLK and MOSI on SPI2's IO_MUX pads, CS through the GPIO
 * matrix (the module pins follow the connector's pin order), DC a plain GPIO.
 * TE (tearing effect) is wired to MAO_PIN_LCD_TE as an input; the flush does
 * not wait for it yet (VERIFY AT BRING-UP). The panel logic sits behind a load
 * switch (expander LCD_PWR_EN) and the panel reset is expander LCD_RST_N.
 *
 * Backlight: the panel LEDs hang from VSYS into an Awinic AW9364 constant-
 * current sink (two 20 mA channels on the cathode: 40 mA is the hardware
 * maximum, so no firmware value can overdrive the panel). Its EN pin is
 * MAO_PIN_LCD_BL_CTRL and takes 1-wire pulse-count dimming, 16 steps
 * (aw9364_dimming.h). The LEDs are NOT on the switched panel rail: EN low is
 * the only thing that darkens them, so every rail-off path drops EN first.
 *
 * Power-up: rail on, 10 ms, reset low 10 ms, reset high, 120 ms, then the
 * GC9A01 init. With the rail off, the bus is driven low (or isolated for
 * deep sleep): an idle-high CS or MOSI would otherwise back-power the
 * unpowered panel through its input protection.
 */
#include "mao_board.h"
#include "mao_board_a0_priv.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "esp_private/gpio.h"     /* gpio_iomux_output(), gpio_func_sel(): give parked pins back to SPI2 */
#include "esp_rom_gpio.h"
#include "soc/spi_periph.h"
#include "esp_check.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "mao_system.h"
#include "aw9364_dimming.h"

static const char *TAG = "MAO_BOARD";

#define LCD_SPI_HOST             SPI2_HOST
#define BL_STEP_UNKNOWN          0xFF    /* after a raw bring-up edge: restart on the next set */
#define SLEEP_OUT_WAIT_MS        120     /* GC9A01: sleep-out to next sleep-in / full operation */

static const int kBusPins[] = { MAO_PIN_LCD_SCLK, MAO_PIN_LCD_MOSI, MAO_PIN_LCD_CS, MAO_PIN_LCD_DC };

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_backlight_ready;
static SemaphoreHandle_t s_bl_lock;     /* one pulse train at a time, from any task */
static portMUX_TYPE s_bl_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_bl_step;               /* AW9364 step now set: 0 = off, 1 = brightest .. 16, or unknown */
static int64_t s_bl_low_since_us;       /* when EN last went low (0: low since reset) */
static bool s_parked;
static bool s_oriented;      /* panel orientation set once; the driver keeps it since */

/* ------------------------------------------------------------------------ */
/* Backlight                                                                */
/* ------------------------------------------------------------------------ */

static void register_devcmd(void);

static esp_err_t backlight_init(void)
{
    /* GPIO45 is a strap (VDD_SPI). On this module the flash voltage comes
     * from eFuse, and the AW9364's 150 k EN pull-down holds the pin low
     * through reset anyway, so the backlight is dark from power-on. The pin
     * becomes a plain output only now, long after boot, latched low first. */
    if (!s_bl_lock) {
        s_bl_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_bl_lock, ESP_ERR_NO_MEM, TAG, "backlight mutex");
        register_devcmd();
    }
    gpio_set_level(MAO_PIN_LCD_BL_CTRL, 0);
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << MAO_PIN_LCD_BL_CTRL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "backlight pin");
    gpio_set_level(MAO_PIN_LCD_BL_CTRL, 0);
    if (s_bl_step != 0) {
        s_bl_low_since_us = esp_timer_get_time();
    }
    s_bl_step = 0;
    s_backlight_ready = true;
    return ESP_OK;
}

/* EN low: the driver shuts down after TOFF (0.8 .. 2.5 ms). */
static void bl_en_low(void)
{
    gpio_set_level(MAO_PIN_LCD_BL_CTRL, 0);
    s_bl_low_since_us = esp_timer_get_time();
}

/* Before an enable edge: EN must have been low for AW9364_T_SHUTDOWN_US,
 * or the edge would count as a dimming step of a driver still on. Waiting
 * longer is harmless, so the bulk sleeps and only the tail spins. */
static void bl_wait_shutdown(void)
{
    if (s_bl_low_since_us == 0) {
        return;                       /* low since reset */
    }
    for (;;) {
        const int64_t left_us = s_bl_low_since_us + AW9364_T_SHUTDOWN_US - esp_timer_get_time();
        if (left_us <= 0) {
            return;
        }
        if (left_us > 1000) {
            vTaskDelay(1);
        } else {
            esp_rom_delay_us((uint32_t)left_us);
        }
    }
}

/* Emit rising edges on EN. from_off: EN is low and the driver shut down,
 * so the first edge enables it (step 1) and is followed by the ready time.
 * Every low phase must stay under 500 us: the whole train runs in a
 * critical section (16 edges x 4 us + 30 us at most), so neither an
 * interrupt nor the other task on this core can stretch one. */
static void bl_edges(uint8_t edges, bool from_off)
{
    portENTER_CRITICAL(&s_bl_mux);
    for (uint8_t i = 0; i < edges; i++) {
        const bool enable_edge = from_off && i == 0;
        if (!enable_edge) {
            gpio_set_level(MAO_PIN_LCD_BL_CTRL, 0);
            esp_rom_delay_us(AW9364_T_LOW_US);
        }
        gpio_set_level(MAO_PIN_LCD_BL_CTRL, 1);
        esp_rom_delay_us(enable_edge ? AW9364_T_READY_US : AW9364_T_HIGH_US);
    }
    portEXIT_CRITICAL(&s_bl_mux);
}

esp_err_t mao_board_backlight_set(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(s_backlight_ready, ESP_ERR_INVALID_STATE, TAG, "backlight not initialised");
    const uint8_t step = aw9364_step_from_percent(percent);
    xSemaphoreTake(s_bl_lock, portMAX_DELAY);
    if (s_bl_step == BL_STEP_UNKNOWN) {
        bl_en_low();                  /* count from a known state again */
        s_bl_step = 0;
    }
    const aw9364_plan_t plan = aw9364_plan(s_bl_step, step);
    if (plan.shutdown) {
        if (s_bl_step != 0) {
            bl_en_low();
        }
        if (plan.edges) {
            bl_wait_shutdown();
        }
    }
    if (plan.edges) {
        bl_edges(plan.edges, plan.shutdown);
    }
    if (plan.shutdown || plan.edges) {
        ESP_LOGD(TAG, "backlight %u %% -> AW9364 step %u (%" PRIu32 " uA per channel)%s", percent, step,
                 aw9364_step_current_ua(step), plan.shutdown && plan.edges ? ", restarted" : "");
    }
    s_bl_step = step;
    xSemaphoreGive(s_bl_lock);
    return ESP_OK;
}

#if CONFIG_MAO_DEV_CONSOLE

/* Bring-up of the AW9364 (A0 only), bypassing mao_display: its own
 * brightness comes back with the next mao_display_set_brightness().
 *   backlight <0..100>     level through mao_board_backlight_set()
 *   backlight step <1..16> one AW9364 step (16 = dimmest)
 *   backlight edge         one more raw rising edge from the current state:
 *                          at step 16 this shows whether edge 17 wraps to
 *                          step 1 (full) or does nothing */
static void devcmd_backlight(const char *args)
{
    if (!s_backlight_ready) {
        ESP_LOGW(TAG, "backlight: display not initialised");
        return;
    }
    if (strncmp(args, "edge", 4) == 0) {
        xSemaphoreTake(s_bl_lock, portMAX_DELAY);
        const uint8_t was = s_bl_step;
        if (was != 0 && was != BL_STEP_UNKNOWN) {
            bl_edges(1, false);
            s_bl_step = BL_STEP_UNKNOWN;
        }
        xSemaphoreGive(s_bl_lock);
        if (was == 0 || was == BL_STEP_UNKNOWN) {
            ESP_LOGW(TAG, "backlight edge: set a step first");
        } else {
            ESP_LOGI(TAG, "backlight: one edge after step %u (expected step %u%s); next set restarts the driver",
                     was, was + 1u, was == AW9364_STEPS ? ": edge 17, wrap to full or no change?" : "");
        }
        return;
    }
    uint8_t percent;
    if (strncmp(args, "step", 4) == 0) {
        const int step = atoi(args + 4);
        if (step < 1 || step > AW9364_STEPS) {
            ESP_LOGW(TAG, "backlight step: 1..%d", AW9364_STEPS);
            return;
        }
        /* The smallest percent that maps to this step. */
        percent = (uint8_t)((100 * (AW9364_STEPS - step) + AW9364_STEPS) / AW9364_STEPS);
    } else {
        const int pct = atoi(args);
        percent = (uint8_t)(pct < 0 ? 0 : (pct > 100 ? 100 : pct));
    }
    const esp_err_t err = mao_board_backlight_set(percent);
    const uint8_t step = aw9364_step_from_percent(percent);
    ESP_LOGI(TAG, "backlight %u %%: AW9364 step %u, %" PRIu32 " uA per channel x 2 (%s)", percent, step,
             aw9364_step_current_ua(step), esp_err_to_name(err));
}

static void register_devcmd(void)
{
    mao_devcmd_register("backlight", "backlight <0..100> | step <1..16> | edge  (A0 AW9364 bring-up)",
                        devcmd_backlight);
}

#else

static void register_devcmd(void)
{
}

#endif

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
    /* Same routing spi_bus_initialize() / spi_bus_add_device() chose: SCLK
     * and MOSI on the native IO_MUX function of SPI2, CS through the GPIO
     * matrix (its pad is not SPI2's IO_MUX CS). DC stays a plain GPIO that
     * esp_lcd drives per transaction. */
    const int func = spi_periph_signal[LCD_SPI_HOST].func;
    gpio_iomux_output((gpio_num_t)MAO_PIN_LCD_SCLK, func);
    gpio_iomux_output((gpio_num_t)MAO_PIN_LCD_MOSI, func);
    gpio_func_sel((gpio_num_t)MAO_PIN_LCD_CS, PIN_FUNC_GPIO);
    esp_rom_gpio_connect_out_signal(MAO_PIN_LCD_CS, spi_periph_signal[LCD_SPI_HOST].spics_out[0], false, false);
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

    /* Backlight first, EN held low (AW9364 off), so the panel's power-on
     * garbage is never visible. */
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
