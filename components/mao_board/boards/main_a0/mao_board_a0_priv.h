/*
 * MAO_MAIN A0: board constants that are not pins, and the internal API
 * shared by the boards/main_a0 files. PRIVATE to mao_board.
 *
 * Pins come from the generated mao_board_pins.h (hardware/mao/design/
 * pinmap.py, never edited by hand). Anything below that could not be
 * checked without hardware is marked VERIFY AT BRING-UP.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "mao_board_pins.h"

/* --- I2C: one bus, external 2.2 k pull-ups ------------------------------- */
#define A0_I2C_PORT                 0
#define A0_I2C_HZ                   400000
#define A0_I2C_TIMEOUT_MS           50

/* --- TCA6408A expander registers ------------------------------------------ */
#define A0_EXP_REG_INPUT            0x00
#define A0_EXP_REG_OUTPUT           0x01
#define A0_EXP_REG_POLARITY         0x02
#define A0_EXP_REG_CONFIG           0x03   /* 1 = input */
#define A0_EXP_INPUT_MASK           ((1u << MAO_EXP_CHG_N) | (1u << MAO_EXP_SENSE_ALRT_N))

/* --- Display: 1.28" round GC9A01 panel in the J301 18-pin FPC connector ---- */
#define A0_LCD_H_RES                240
#define A0_LCD_V_RES                240
#define A0_LCD_PCLK_HZ              (80 * 1000 * 1000)
#define A0_LCD_PWR_SETTLE_MS        10     /* load switch on -> reset sequence */
#define A0_LCD_RESET_LOW_MS         10
#define A0_LCD_RESET_WAIT_MS        120    /* reset released -> first command */
/* Backlight ceiling: 100 % brightness = 80 % PWM. Two LEDs from 3V3_LCD through
 * 10R draw 17-50 mA by panel Vf bin (2.8-3.2 V, simulation S3 in
 * docs/hardware/mao-a0-verification.md); 80 % keeps the average at or under the
 * panel's 40 mA on every bin. */
#define A0_BACKLIGHT_MAX_PCT        80
/* Colour handling and the panel-native orientation are properties of the
 * glass + GC9A01; these are the LCDkit's values for the same 1.28" IPS round
 * glass. The plug-in panel's tail leaves at 9 o'clock (as the LCDkit's did at
 * its own carrier), and any panel of the 18-pin standard may be fitted.
 * VERIFY AT BRING-UP: colour order / inversion and the mirror flags against the
 * fitted panel's datasheet. */
#define A0_LCD_INVERT_COLOR         true
#define A0_LCD_BGR                  true
#define A0_LCD_PANEL_MIRROR_X       true
#define A0_LCD_PANEL_MIRROR_Y       false
#define A0_LCD_PANEL_SWAP_XY        false
#define A0_LCD_SWAP_BYTES           true
/* How the panel is turned in the puck (tail at 9 o'clock): Kconfig
 * MAO_A0_LCD_ROTATION, default 90. VERIFY AT BRING-UP (try 270 live with
 * "mao rotate"). Applied by mao_display as an LVGL / MADCTL rotation. */
#define A0_LCD_ROTATION_DEG         CONFIG_MAO_A0_LCD_ROTATION_DEG

/* --- Ring dial: two DRV5012 latches on a 30-pole ring --------------------- */
/* Electrically identical to the LCDkit EC11 as the decoder sees it. */
#define A0_DIAL_TRANSITIONS_PER_DETENT 2
#define A0_DIAL_REST_MASK           ((1u << 0x0) | (1u << 0x3))   /* rest at AB=00 and AB=11 */
#define A0_DIAL_DETENTS_PER_REV     30
/* VERIFY AT BRING-UP: CW/CCW sense depends on which latch leads. */
#define A0_DIAL_REVERSE             false

/* --- Audio ---------------------------------------------------------------- */
/* MAX98357A, gain pin open = 9 dB, from VSYS. VERIFY AT BRING-UP: started at
 * the LCDkit's NS4150 value; re-tune by ear on the A0 speaker. */
#define A0_AUDIO_GAIN               0.58f
#define A0_AMP_I2S_PORT             I2S_NUM_1   /* standard (Philips) TX */
#define A0_MIC_I2S_PORT             I2S_NUM_0   /* PDM RX exists on I2S0 only */
/* SPH0641LU4H-1 with SELECT tied low. IDF documents I2S_PDM_SLOT_LEFT as
 * "the PDM device whose select pin is pulled down", which matches. VERIFY AT
 * BRING-UP: if the PCM stream is silent or noise-only, invert the clock. */
#define A0_MIC_CLK_INVERT           false

/* --- IR -------------------------------------------------------------------- */
#define A0_IR_CARRIER_HZ            38000
#define A0_IR_CARRIER_DUTY_PCT      33
/* VERIFY AT BRING-UP: receiver supply goes through an RC filter; ~1 ms. */
#define A0_IR_RX_SETTLE_MS          1

/* --- Board ID divider (ADC1 on GPIO8) ------------------------------------- */
#define A0_ID_A0_MIN_MV             1400
#define A0_ID_A0_MAX_MV             1900
#define A0_ID_SAMPLES               8

/* ---- Internal API (boards/main_a0) ---------------------------------------- */

/* I2C bus + expander: outputs all off, then probe every fitted device. */
esp_err_t a0_i2c_init(void);
bool a0_expander_ok(void);
/* Set / clear one expander output bit (read-modify-write on a shadow). */
esp_err_t a0_expander_write_bit(uint8_t bit, bool level);
/* Write the whole output register (bits not configured as outputs ignored). */
esp_err_t a0_expander_write_all(uint8_t value);
bool a0_expander_bit(uint8_t bit);
/* Read the input register (clears the expander interrupt). */
esp_err_t a0_expander_read_inputs(uint8_t *value);
/* Output-pin mask of the expander (rails and resets). */
#define A0_EXP_OUTPUT_MASK          ((uint8_t)~A0_EXP_INPUT_MASK)

/* Display rail sequencing (boards/main_a0/mao_board_a0_display.c). */
esp_err_t a0_display_power(bool on);
/* Drive the display bus low so an unpowered panel is not back-powered.
 * deep = also isolate the pads for deep sleep. */
void a0_display_park(bool deep);

/* Release pads that a previous deep sleep left held / routed to RTC IO. */
void a0_sleep_release_pads(void);
