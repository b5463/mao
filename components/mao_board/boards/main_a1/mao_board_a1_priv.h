/*
 * MAO_MAIN A1: board constants that are not pins, and the internal API
 * shared by the boards/main_a1 files. PRIVATE to mao_board.
 *
 * Pins come from the generated mao_board_pins.h (hardware/mao/design/
 * pinmap.py, never edited by hand). Anything below that could not be
 * checked without hardware is marked VERIFY AT BRING-UP.
 *
 * A1 against A0: no I/O expander (every rail / reset / charger line is a
 * native GPIO held off by a hardware pull-down), no touch, no microphone,
 * no light sensor, no board-ID divider (revision from NVS), ICM-42670-P IMU
 * instead of the LSM6DSOX, BQ25185 charger with STAT1/STAT2, a LEDC-driven
 * analogue backlight sink instead of the AW9364, no RGB LED.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "mao_board_pins.h"

/* --- I2C: one bus, external 4.7 k pull-ups ------------------------------- */
#define A1_I2C_PORT                 0
#define A1_I2C_HZ                   400000

/* --- Display: 1.28" round GC9A01 panel (240 x 240) on SPI2 IO_MUX pads ----- */
#define A1_LCD_H_RES                240
#define A1_LCD_V_RES                240
#define A1_LCD_PCLK_HZ              (CONFIG_MAO_A1_LCD_PCLK_MHZ * 1000 * 1000)   /* 80 by default; 40 is the fallback */
#define A1_LCD_PWR_SETTLE_MS        10     /* load switch on -> reset sequence */
#define A1_LCD_RESET_LOW_MS         10
#define A1_LCD_RESET_WAIT_MS        120    /* reset released -> first command */
/* Colour handling and the panel-native orientation are properties of the
 * glass + GC9A01; these are the LCDkit's values for the same 1.28" IPS
 * round glass. VERIFY AT BRING-UP against the fitted panel. */
#define A1_LCD_INVERT_COLOR         true
#define A1_LCD_BGR                  true
#define A1_LCD_PANEL_MIRROR_X       true
#define A1_LCD_PANEL_MIRROR_Y       false
#define A1_LCD_PANEL_SWAP_XY        false
#define A1_LCD_SWAP_BYTES           true
/* Mounting (tail at 9 o'clock): Kconfig MAO_A1_LCD_ROTATION, default 90.
 * VERIFY AT BRING-UP (try 270 live with "mao rotate"). */
#define A1_LCD_ROTATION_DEG         CONFIG_MAO_A1_LCD_ROTATION_DEG
/* Backlight: LEDC PWM on MAO_PIN_LCD_BL into the analogue current sink's
 * reference (RC-filtered), 30 kHz like the LCDkit. Higher duty = more
 * current. VERIFY AT BRING-UP: polarity, the current at 100 % (panel rating)
 * and that 3 % still glows evenly. */
#define A1_BL_FREQ_HZ               30000

/* --- Ring dial: two Hall latches on a 30-pole ring ------------------------ */
/* Electrically identical to the LCDkit EC11 as the decoder sees it. */
#define A1_DIAL_TRANSITIONS_PER_DETENT 2
#define A1_DIAL_REST_MASK           ((1u << 0x0) | (1u << 0x3))   /* rest at AB=00 and AB=11 */
#define A1_DIAL_DETENTS_PER_REV     30
/* VERIFY AT BRING-UP: CW/CCW sense depends on which latch leads. */
#define A1_DIAL_REVERSE             false

/* --- Audio: MAX98357A on I2S0 (standard Philips TX), SD_MODE = AMP_SD ------ */
/* VERIFY AT BRING-UP: started at the LCDkit's NS4150 value; tune by ear. */
#define A1_AUDIO_GAIN               0.58f
#define A1_AMP_I2S_PORT             I2S_NUM_0

/* --- IR -------------------------------------------------------------------- */
#define A1_IR_CARRIER_HZ            38000
#define A1_IR_CARRIER_DUTY_PCT      33
/* VERIFY AT BRING-UP: receiver supply through a load switch; ~1 ms. */
#define A1_IR_RX_SETTLE_MS          1

/* --- Revision record (NVS, written at manufacturing) ----------------------- */
#define A1_REV_NVS_NAMESPACE        "mao"
#define A1_REV_NVS_KEY              "hw_rev"
#define A1_REV_DEFAULT              "A1"

/* ---- Internal API (boards/main_a1) ---------------------------------------- */

/* I2C bus: probe every fitted device (switchable ones powered for it). */
esp_err_t a1_i2c_init(void);

/* Display rail sequencing (mao_board_a1_display.c). */
esp_err_t a1_display_power(bool on);
/* Drive the display bus low so an unpowered panel is not back-powered.
 * deep = also hold the pads for deep sleep. */
void a1_display_park(bool deep);
/* Backlight off and its pin held low (deep sleep), or released. */
void a1_backlight_hold(bool hold);

/* Release pads that a previous deep sleep left held / routed to RTC IO. */
void a1_sleep_release_pads(void);

/* Plain GPIO output, latched at `level` before it becomes an output. */
esp_err_t a1_gpio_out(int gpio, int level);
