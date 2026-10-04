/*
 * MAO board support: one board-agnostic API, one implementation per board.
 *
 * mao_board is the ONLY component that knows physical GPIO numbers, I2C
 * addresses and board wiring. The board is picked by the "MAO board" Kconfig
 * choice (defaulted from the target): boards/lcdkit = ESP32-C3-LCDkit,
 * boards/main_a0 = MAO_MAIN A0. Each board's pin header is private to this
 * component. Other components receive initialised handles or descriptors
 * from here and never see a pin number they did not get from a descriptor.
 *
 * Absent hardware: mao_board_get_caps() says what the board has. Every call
 * for hardware the board lacks returns ESP_ERR_NOT_SUPPORTED and touches
 * nothing, so callers can degrade gracefully.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "driver/i2c_types.h"
#include "driver/i2s_types.h"
#include "led_strip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* From the board profile (Kconfig "MAO board"). */
#define MAO_BOARD_NAME CONFIG_MAO_BOARD_NAME

/* ------------------------------------------------------------------------ */
/* Identity and capabilities                                                */
/* ------------------------------------------------------------------------ */

typedef struct {
    bool rgb_led;          /* WS2812 status LED */
    bool display_power;    /* display rail and panel reset under firmware control */
    bool i2c;              /* shared sensor / power bus */
    bool expander;         /* rail and status-line expander answered at boot */
    bool amp_switch;       /* speaker amplifier can be shut down */
    bool mic;              /* PDM microphone */
    bool touch;            /* capacitive touch electrodes */
    bool ir;               /* separate IR transmitter and receiver */
    bool power_status;     /* USB-present and charging lines */
    bool hall_fast;        /* dial sensors have a fast / low-power sampling control */
    bool sleep;            /* board defines light / deep sleep wake lines */
    /* Fitted by design; whether each one answered is in mao_board_i2c_device(). */
    bool imu;
    bool tof;
    bool als;
    bool fuel_gauge;
    bool haptic;
} mao_board_caps_t;

/* Early board init. Validates the chip for this board, reads the board ID
 * and (where present) brings up the I2C bus and expander with every rail off.
 * Missing I2C devices are logged, not fatal. */
esp_err_t mao_board_init(void);

void mao_board_get_caps(mao_board_caps_t *out);

/* Board revision from the ID divider ("A0"), "?" if unrecognised, "-" if the
 * board has no ID divider. Valid after mao_board_init(). */
const char *mao_board_revision(void);

/* Measured ID divider voltage in mV, -1 if none. */
int mao_board_revision_mv(void);

/* ------------------------------------------------------------------------ */
/* Display                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    uint16_t h_res;
    uint16_t v_res;
    /* Panel-native orientation (the GC9A01 glass as mounted on the LCDkit:
     * FPC towards 6 o'clock). */
    bool mirror_x;
    bool mirror_y;
    bool swap_xy;
    bool swap_bytes;          /* RGB565 must be byte-swapped before sending */
    /* How the panel is turned in this product relative to that, clockwise
     * degrees (0/90/180/270). The display layer applies it as an LVGL
     * rotation, which the panel performs in hardware (MADCTL). */
    uint16_t rotation;
} mao_board_display_t;

/* Panel resolution, available before display init (for buffer sizing). */
void mao_board_display_get_resolution(uint16_t *h_res, uint16_t *v_res);

/* Power the panel (where switchable), bring up the SPI bus and the GC9A01
 * (display off, backlight at 0 %).
 * max_transfer_bytes: largest single SPI transfer (the LVGL draw buffer). */
esp_err_t mao_board_display_init(size_t max_transfer_bytes, mao_board_display_t *out);

/* Backlight 0..100 %. Valid after mao_board_display_init(). */
esp_err_t mao_board_backlight_set(uint8_t percent);

/* Panel sleep-in / sleep-out (controller keeps its RAM, draws microamps). */
esp_err_t mao_board_display_sleep(bool sleep);

/* ------------------------------------------------------------------------ */
/* Input                                                                    */
/* ------------------------------------------------------------------------ */

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

/* Configure dial / press GPIOs as inputs (no ISR installed). */
esp_err_t mao_board_input_init(mao_board_encoder_t *out);

/* Dial sensor sampling: fast (awake) or low-power. */
esp_err_t mao_board_hall_fast_set(bool fast);

typedef enum {
    MAO_TOUCH_RIGHT = 0,
    MAO_TOUCH_LEFT,
    MAO_TOUCH_TOP,
    MAO_TOUCH_REAR,
    MAO_TOUCH_ZONE_COUNT,
} mao_touch_zone_t;

typedef struct {
    int channel[MAO_TOUCH_ZONE_COUNT];   /* touch channel id per zone, -1 if absent */
    mao_touch_zone_t wake_zone;          /* the zone allowed to wake from deep sleep */
} mao_board_touch_t;

esp_err_t mao_board_touch_get(mao_board_touch_t *out);
const char *mao_board_touch_zone_name(mao_touch_zone_t zone);

/* ------------------------------------------------------------------------ */
/* Audio                                                                    */
/* ------------------------------------------------------------------------ */

/* Create and initialise the speaker I2S TX channel (not yet enabled). */
esp_err_t mao_board_audio_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out);

/* Synthesiser gain at 100 % volume for this board's amplifier. */
float mao_board_audio_gain(void);

/* Create the microphone PDM RX channel (hardware PDM->PCM, mono 16-bit,
 * not yet enabled). The mic's clock only runs while the channel is enabled. */
esp_err_t mao_board_mic_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out);

/* ------------------------------------------------------------------------ */
/* RGB LED                                                                  */
/* ------------------------------------------------------------------------ */

/* Create the RGB LED driver (one WS2812 on RMT). */
esp_err_t mao_board_led_init(led_strip_handle_t *out);

/* ------------------------------------------------------------------------ */
/* IR                                                                       */
/* ------------------------------------------------------------------------ */

typedef struct {
    int gpio;                 /* shared TX/RX line behind a jumper, -1 if none */
    bool mode_known;          /* shared line: false until the jumper is physically verified */
    int tx_gpio;              /* dedicated LED driver input, -1 if none */
    int rx_gpio;              /* dedicated demodulating receiver output, -1 if none */
    bool rx_active_low;
    uint32_t carrier_hz;
    uint8_t carrier_duty_pct;
    uint16_t rx_settle_ms;    /* after MAO_RAIL_IR_RX is switched on */
} mao_board_ir_t;

/* Describe the IR hardware. Does not touch any pin. */
void mao_board_ir_get(mao_board_ir_t *out);

/* ------------------------------------------------------------------------ */
/* Shared I2C bus                                                           */
/* ------------------------------------------------------------------------ */

typedef enum {
    MAO_I2C_EXPANDER = 0,
    MAO_I2C_TOF,
    MAO_I2C_FUEL_GAUGE,
    MAO_I2C_ALS,
    MAO_I2C_HAPTIC,
    MAO_I2C_IMU,
    MAO_I2C_DEV_COUNT,
} mao_board_i2c_dev_t;

typedef struct {
    uint8_t address;          /* 7-bit */
    bool fitted;              /* the board design has this device */
    bool present;             /* it acknowledged the boot probe */
} mao_board_i2c_info_t;

esp_err_t mao_board_i2c_bus(i2c_master_bus_handle_t *out);
esp_err_t mao_board_i2c_device(mao_board_i2c_dev_t dev, mao_board_i2c_info_t *out);

/* Add a device handle on the shared bus with the board's address and speed.
 * The i2c_master driver serialises transactions from different tasks. */
esp_err_t mao_board_i2c_add(mao_board_i2c_dev_t dev, i2c_master_dev_handle_t *out);

/* Probe 0x08..0x77 (switchable devices are powered for the scan and then
 * returned to their previous state). */
esp_err_t mao_board_i2c_scan(uint8_t *found, size_t max, size_t *count);

const char *mao_board_i2c_name(mao_board_i2c_dev_t dev);

/* ------------------------------------------------------------------------ */
/* Rails, status lines, interrupt lines                                     */
/* ------------------------------------------------------------------------ */

typedef enum {
    MAO_RAIL_DISPLAY = 0,     /* panel + backlight supply; on = powered and initialised */
    MAO_RAIL_AMP,             /* speaker amplifier enable */
    MAO_RAIL_HAPTIC,          /* haptic driver enable */
    MAO_RAIL_TOF,             /* proximity sensor (XSHUT) */
    MAO_RAIL_IR_RX,           /* IR receiver supply */
    MAO_RAIL_MIC,             /* microphone supply */
    MAO_RAIL_COUNT,
} mao_board_rail_t;

esp_err_t mao_board_rail_set(mao_board_rail_t rail, bool on);
bool mao_board_rail_is_on(mao_board_rail_t rail);
const char *mao_board_rail_name(mao_board_rail_t rail);

/* Diagnostics: the rail's control pin as it actually reads back (expander
 * input register / GPIO input), not as commanded. A mismatch with
 * mao_board_rail_is_on() means a short or a dead expander pin. */
esp_err_t mao_board_rail_readback(mao_board_rail_t rail, bool *on);

/* Diagnostics: exercise the rail / status expander without side effects:
 * CONFIG readback, POLARITY register write-read round trip on the output
 * pins (polarity only affects inputs), every output pin's level vs the
 * commanded level. failed_bits (optional) gets the output pins that read
 * back wrong. ESP_ERR_NOT_SUPPORTED on boards without one, ESP_ERR_NOT_FOUND
 * if it is fitted but did not answer at boot. */
esp_err_t mao_board_expander_test(uint8_t *failed_bits);

/* Expander resets since boot (recoveries of a wedged expander and reset
 * tests). A reset drops every expander-switched rail for ~0.2 ms, so the
 * panel, the ToF and the haptic driver lose their configuration: their
 * owners compare this counter and re-initialise. Always 0 without one. */
uint32_t mao_board_expander_resets(void);

/* Diagnostics: pulse the expander's RESET line, check that it really reset
 * (CONFIG back at its 0xFF default, config_after_pulse), reprogram it from
 * the shadow registers and check CONFIG and every output pin again
 * (failed_bits). ESP_ERR_INVALID_STATE if the pulse had no effect. */
esp_err_t mao_board_expander_reset_test(uint8_t *config_after_pulse, uint8_t *failed_bits);

/* Re-run the panel initialisation after the panel was reset under power
 * (expander reset). Caller holds the LVGL lock. ESP_ERR_NOT_SUPPORTED where
 * the panel reset is not shared with anything else. */
esp_err_t mao_board_display_reinit(void);

typedef enum {
    MAO_LINE_USB_PRESENT = 0, /* USB power valid */
    MAO_LINE_CHARGING,        /* charger is charging */
    MAO_LINE_SENSE_ALERT,     /* fuel-gauge or light-sensor alert asserted */
    MAO_LINE_COUNT,
} mao_board_line_t;

/* Logical state (true = asserted); the board handles polarity. Reading an
 * expander line also clears the expander interrupt. */
esp_err_t mao_board_line_get(mao_board_line_t line, bool *active);

typedef enum {
    MAO_IRQ_IMU_INT1 = 0,     /* wake-up / activity, tap */
    MAO_IRQ_IMU_INT2,         /* orientation, free-fall */
    MAO_IRQ_TOF,              /* proximity data ready / threshold */
    MAO_IRQ_EXPANDER,         /* a status line changed */
    MAO_IRQ_USB_PRESENT,      /* USB power came or went */
    MAO_IRQ_COUNT,
} mao_board_irq_t;

typedef struct {
    int gpio;
    bool active_low;
} mao_board_irq_desc_t;

/* Interrupt line descriptor. The pin is already configured as an input with
 * the correct pulls; the caller installs its own ISR. */
esp_err_t mao_board_irq_get(mao_board_irq_t irq, mao_board_irq_desc_t *out);

/* ------------------------------------------------------------------------ */
/* Sleep                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct {
    bool press;               /* face press */
    bool motion;              /* IMU INT1 */
    bool usb;                 /* USB plugged (or unplugged while present) */
    bool expander;            /* charger status / gauge or light alert */
    bool proximity;           /* proximity threshold (light sleep only) */
} mao_board_wake_t;

/* Deep sleep: switch every rail off, park the display bus, hold the dial
 * sensors in low-power mode and arm the requested wake lines. A line that is
 * already asserted is not armed, since it would wake the chip at once;
 * armed (optional) reports what was armed. Touch and timer wake belong to
 * their owners. */
esp_err_t mao_board_deep_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed);

/* Light sleep: arm the requested wake lines (rails stay as they are). */
esp_err_t mao_board_light_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed);

/* After a light sleep: disarm and give the wake pins back to their drivers. */
void mao_board_light_sleep_done(void);

/* Which board lines caused the last wake-up (deep or light sleep). */
void mao_board_wake_decode(mao_board_wake_t *out);

#ifdef __cplusplus
}
#endif
