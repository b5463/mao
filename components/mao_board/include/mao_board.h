/*
 * MAO board support: ESP32-C3-LCDkit.
 *
 * mao_board is the ONLY component that knows physical GPIO numbers and board
 * wiring (see mao_board_pins.h, which is private to this component). Other
 * components receive initialised handles or board descriptors from here.
 * A future MAO PCB gets its own implementation of this header.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "driver/i2s_types.h"
#include "led_strip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_BOARD_NAME "ESP32-C3-LCDkit"

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    uint16_t h_res;
    uint16_t v_res;
    bool mirror_x;
    bool mirror_y;
    bool swap_xy;
    bool swap_bytes;          /* RGB565 must be byte-swapped before sending */
} mao_board_display_t;

typedef struct {
    int gpio_a;
    int gpio_b;
    int gpio_switch;
    bool switch_active_low;
    uint8_t transitions_per_detent;  /* quadrature transitions per mechanical detent (2 or 4) */
    uint8_t rest_mask;               /* bit n set: AB == n is a detent rest position */
    uint8_t detents_per_rev;
    bool reverse;                    /* swap CW/CCW sense */
} mao_board_encoder_t;

typedef struct {
    int gpio;
    bool mode_known;          /* false until the TX/RX jumper is physically verified */
} mao_board_ir_t;

/* Early board init: validates the chip and logs the board. Touches no pins. */
esp_err_t mao_board_init(void);

/* Panel resolution, available before display init (for buffer sizing). */
void mao_board_display_get_resolution(uint16_t *h_res, uint16_t *v_res);

/* Bring up SPI bus + GC9A01 panel (display off, backlight at 0 %).
 * max_transfer_bytes: largest single SPI transfer (the LVGL draw buffer). */
esp_err_t mao_board_display_init(size_t max_transfer_bytes, mao_board_display_t *out);

/* Backlight 0..100 %. Valid after mao_board_display_init(). */
esp_err_t mao_board_backlight_set(uint8_t percent);

/* Smooth backlight change using the LEDC hardware fader. */
esp_err_t mao_board_backlight_fade(uint8_t percent, uint32_t fade_ms);

/* Configure encoder/switch GPIOs as inputs with pull-ups (no ISR installed). */
esp_err_t mao_board_input_init(mao_board_encoder_t *out);

/* Light sleep (M4.1): arm (or disarm) the knob as the wake source - any
 * movement or a press. Not a deep-sleep source on this board (not RTC IOs). */
esp_err_t mao_board_knob_wake_arm(bool arm);
/* The encoder's button is held down now. */
bool mao_board_switch_down(void);
/* Light sleep: hold the PDM line (to the always-on amplifier) as it is. */
void mao_board_audio_hold(bool hold);
/* The PDM line's rest and return (M4.1): glide it to still (after the stream
 * is parked on its floor), up again (before it restarts), and hand it back. */
void mao_board_audio_line_rest(void);
void mao_board_audio_line_rise(void);
void mao_board_audio_line_attach(void);
void mao_board_audio_line_idle(int permille);   /* DEV: the floor's density */
/* DEV: the PDM line's measured pulse density, per mille. */
int mao_board_audio_duty_permille(void);
/* DEV deep sleep: hold the backlight and the PDM line low (true), or release
 * them after the wake (false). */
void mao_board_deep_sleep_hold(bool hold);

/* Create and initialise the speaker I2S PDM TX channel (not yet enabled). */
esp_err_t mao_board_audio_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out);

/* Create the RGB LED driver (one WS2812 on RMT). */
esp_err_t mao_board_led_init(led_strip_handle_t *out);

/* Describe the IR hardware. Does not touch the pin. */
void mao_board_ir_get(mao_board_ir_t *out);

#ifdef __cplusplus
}
#endif
