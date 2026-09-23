/*
 * ESP32-C3-LCDkit physical assignments. PRIVATE to mao_board.
 *
 * Verified on hardware (boot log of the factory firmware) or taken from the
 * Espressif ESP32-C3-LCDkit BSP / user guide. See the hardware discovery
 * report for evidence per pin.
 */
#pragma once

#include "driver/spi_common.h"

/* Display: GC9A01 240x240 on SPI2 (write-only, no MISO, no RST, no TE). */
#define MAO_PIN_LCD_SPI_HOST        SPI2_HOST
#define MAO_PIN_LCD_MOSI            0
#define MAO_PIN_LCD_SCLK            1
#define MAO_PIN_LCD_DC              2    /* strapping pin: driven only after boot */
#define MAO_PIN_LCD_CS              7
#define MAO_PIN_LCD_RST             (-1) /* not connected: software reset only */
#define MAO_PIN_LCD_BACKLIGHT       5    /* LEDC PWM, active high */

#define MAO_LCD_H_RES               240
#define MAO_LCD_V_RES               240
#define MAO_LCD_PCLK_HZ             (80 * 1000 * 1000)

/* EC11 encoder with push switch: 30 detents / 15 quadrature cycles per
 * revolution. Verified in M1: zero missed edges while one revolution counted
 * exactly half the detents with 4 transitions/detent, and the lines rest at
 * both AB=00 and AB=11. So a detent is every 2 transitions, resting
 * alternately at 00 and 11. */
#define MAO_PIN_ENC_A               10
#define MAO_PIN_ENC_B               6
#define MAO_PIN_ENC_SW              9    /* BOOT strapping pin: input only, never drive */
#define MAO_ENC_TRANSITIONS_PER_DETENT 2
#define MAO_ENC_REST_MASK           ((1u << 0x0) | (1u << 0x3))   /* rest at AB=00 and AB=11 */
#define MAO_ENC_DETENTS_PER_REV     30
#define MAO_ENC_REVERSE             false

/* Speaker: I2S0 PDM TX -> NS4150 amplifier (no enable pin). */
#define MAO_PIN_AUDIO_PDM_DOUT      3

/* WS2812B-Mini. GPIO8 is a strapping pin; only configured from app_main. */
#define MAO_PIN_LED_RGB             8

/* IR TX/RX shared line; jumper selects the path. Not configured in M0. */
#define MAO_PIN_IR                  4

/* Reserved: GPIO18/19 USB D-/D+ (console, flashing), GPIO20/21 UART0,
 * GPIO11..17 in-package flash. */
