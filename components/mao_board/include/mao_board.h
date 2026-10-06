/*
 * MAO board support: one board-agnostic API, one implementation per board.
 *
 * mao_board is the ONLY component that knows physical GPIO numbers, I2C
 * addresses and board wiring. The board is picked by the "MAO board" Kconfig
 * choice (defaulted from the target): boards/lcdkit = ESP32-C3-LCDkit,
 * boards/main_a1 = MAO_MAIN A1. Each board's pin header is private to this
 * component (the A1's is generated from hardware/mao/design/pinmap.py).
 * Other components receive initialised handles or descriptors from here and
 * never see a pin number they did not get from a descriptor.
 *
 * Absent hardware: mao_board_get_caps() says what the board has. Every call
 * for hardware the board lacks returns ESP_ERR_NOT_SUPPORTED (or does
 * nothing, for the void calls) and touches no pin, so callers can degrade
 * gracefully. On the LCDkit everything added for the A0 / A1 is "not fitted"
 * and the kit behaves exactly as before.
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
    bool amp_switch;       /* speaker amplifier can be shut down */
    bool audio_pdm_line;   /* speaker fed by a PDM line into an always-on amplifier (LCDkit) */
    bool ir;               /* separate IR transmitter (and receiver, see mao_board_ir_t) */
    bool power_status;     /* USB-present line and charger status lines */
    bool charge_control;   /* firmware can pause the charger (mao_board_charge_enable) */
    bool hall_fast;        /* dial sensors have a fast / low-power sampling control */
    bool sleep;            /* board defines light / deep sleep wake lines (mao_board_*_sleep_*) */
    bool deep_wake_knob;   /* the knob (dial / press) can wake the chip from DEEP sleep */
    /* Fitted by design; whether each one answered is in mao_board_i2c_device(). */
    bool imu;
    bool tof;
    bool fuel_gauge;
    bool haptic;
} mao_board_caps_t;

/* Early board init. Validates the chip for this board, releases whatever a
 * previous deep sleep left held and (where present) brings up the I2C bus
 * with every rail off. Missing I2C devices are logged, not fatal. */
esp_err_t mao_board_init(void);

void mao_board_get_caps(mao_board_caps_t *out);

/* Board revision ("A1"), "-" if the board has no revision record. A1: the
 * NVS record "hw_rev" (namespace "mao") written at manufacturing, "A1" if
 * absent. Valid after mao_board_init(). */
const char *mao_board_revision(void);

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

/* Backlight 0..100 % (0 = off). Valid after mao_board_display_init().
 * LCDkit and A1: LEDC PWM at 30 kHz (A1: the PWM sets an analogue current
 * sink), kept alive in light sleep. */
esp_err_t mao_board_backlight_set(uint8_t percent);

/* Smooth backlight change using the LEDC hardware fader. */
esp_err_t mao_board_backlight_fade(uint8_t percent, uint32_t fade_ms);

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

/* Dial sensor sampling: fast (awake) or low-power (asleep).
 * ESP_ERR_NOT_SUPPORTED without the control (LCDkit: a mechanical EC11). */
esp_err_t mao_board_hall_fast_set(bool fast);

/* Light sleep (M4.1): arm (or disarm) the knob as the wake source - any
 * movement or a press (GPIO wake, each line at the level it is not at now). */
esp_err_t mao_board_knob_wake_arm(bool arm);
/* The knob's button / the face press is held down now. */
bool mao_board_switch_down(void);

/* ------------------------------------------------------------------------ */
/* Audio                                                                    */
/* ------------------------------------------------------------------------ */

/* Create and initialise the speaker I2S TX channel (not yet enabled).
 * LCDkit: I2S0 PDM TX into the always-on NS4150. A1: I2S0 standard
 * (Philips) TX into the MAX98357A; its SD_MODE is MAO_RAIL_AMP. */
esp_err_t mao_board_audio_init(uint32_t sample_rate_hz, i2s_chan_handle_t *out);

/* Synthesiser gain at 100 % volume for this board's amplifier. */
float mao_board_audio_gain(void);

/* Light sleep: hold the speaker line(s) as they are. LCDkit: the PDM line
 * (to the always-on amplifier); A1: the amplifier's SD_MODE (low). */
void mao_board_audio_hold(bool hold);
/* The PDM line's rest and return (M4.1, LCDkit only; no-ops elsewhere):
 * glide it to still (after the stream is parked on its floor), up again
 * (before it restarts), and hand it back. */
void mao_board_audio_line_rest(void);
void mao_board_audio_line_rise(void);
void mao_board_audio_line_attach(void);
void mao_board_audio_line_idle(int permille);   /* DEV: the floor's density */
/* DEV: the PDM line's measured pulse density, per mille (-1 without one). */
int mao_board_audio_duty_permille(void);

/* ------------------------------------------------------------------------ */
/* RGB LED                                                                  */
/* ------------------------------------------------------------------------ */

/* Create the RGB LED driver (one WS2812 on RMT). ESP_ERR_NOT_SUPPORTED
 * without one (A1). */
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
    MAO_I2C_TOF = 0,
    MAO_I2C_FUEL_GAUGE,
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
    MAO_RAIL_DISPLAY = 0,     /* panel logic supply + reset; on = powered and initialised */
    MAO_RAIL_AMP,             /* speaker amplifier enable (SD_MODE) */
    MAO_RAIL_HAPTIC,          /* haptic driver enable */
    MAO_RAIL_TOF,             /* proximity sensor (XSHUT) */
    MAO_RAIL_IR_RX,           /* IR receiver supply */
    MAO_RAIL_COUNT,
} mao_board_rail_t;

/* A1: every enable is a plain GPIO with a hardware pull-down (off from reset
 * and while the chip is held in reset). */
esp_err_t mao_board_rail_set(mao_board_rail_t rail, bool on);
bool mao_board_rail_is_on(mao_board_rail_t rail);
const char *mao_board_rail_name(mao_board_rail_t rail);

/* Diagnostics: the rail's control pin as it actually reads back (GPIO
 * input), not as commanded. A mismatch with mao_board_rail_is_on() means a
 * short or a dead pin. */
esp_err_t mao_board_rail_readback(mao_board_rail_t rail, bool *on);

typedef enum {
    MAO_LINE_USB_PRESENT = 0, /* USB power valid (A1: VBUS_SENSE divider, high = present) */
    MAO_LINE_CHARGING,        /* the charger says it is charging (A1: STAT1/STAT2 = H/L) */
    MAO_LINE_COUNT,
} mao_board_line_t;

/* Logical state (true = asserted); the board handles polarity. */
esp_err_t mao_board_line_get(mao_board_line_t line, bool *active);

/* Charger status lines (A1: BQ25185 STAT1 / STAT2, open drain with pull-ups). */
typedef enum {
    MAO_CHARGER_IDLE = 0,         /* H/H: charge done, or no input (on battery) */
    MAO_CHARGER_CHARGING,         /* H/L: charging (pre, fast or taper) */
    MAO_CHARGER_FAULT,            /* L/H: recoverable fault (input, thermal, safety timer pause) */
    MAO_CHARGER_FAULT_LATCHED,    /* L/L: latched fault (needs input re-plug / power cycle) */
} mao_charger_status_t;

esp_err_t mao_board_charger_status(mao_charger_status_t *out);
const char *mao_board_charger_status_name(mao_charger_status_t s);

/* Charger enable (caps.charge_control): false pauses charging while USB is
 * present (the system keeps running from USB through the charger's power
 * path), true lets the charger charge. The charging policy (mao_battery:
 * the cell's temperature window) owns it; a reset and deep sleep fall back
 * to enabled, the hardware default. A1: GPIO CHG_CE_N = !enable. */
esp_err_t mao_board_charge_enable(bool enable);

typedef enum {
    MAO_IRQ_IMU_INT1 = 0,     /* wake-on-motion / data (A1: INT2 is not wired) */
    MAO_IRQ_TOF,              /* proximity threshold */
    MAO_IRQ_USB_PRESENT,      /* USB power came or went (A1: active HIGH) */
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
    bool press;               /* face press / knob button */
    bool dial;                /* the dial moved (deep sleep: channel A only) */
    bool motion;              /* IMU INT1 (wake-on-motion) */
    bool usb;                 /* USB plugged or unplugged (light sleep only on the A1) */
    bool proximity;           /* proximity threshold (light sleep only) */
} mao_board_wake_t;

/* Deep sleep: backlight and every rail off, the display lines parked, the
 * dial sensors in low-power mode, and the requested wake lines armed. A
 * line that is already asserted is not armed, since it would wake the chip
 * at once; armed (optional) reports what was armed. The timer belongs to
 * the caller. ESP_ERR_NOT_SUPPORTED where the board has no wake wiring. */
esp_err_t mao_board_deep_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed);

/* DEV / ladder deep sleep, the short form: true = the deep-sleep state with
 * the board's default wake lines (LCDkit: backlight and PDM held low, no
 * wake line; A1: deep_sleep_prepare(press | dial | motion)); false = release
 * the holds after the wake (mao_board_init does this too). Call after the
 * caller has cleared the wake sources and before it sets its timer. */
void mao_board_deep_sleep_hold(bool hold);

/* Light sleep: arm the requested wake lines besides the knob (rails stay as
 * they are) and note the levels the lines are at, for mao_board_wake_decode. */
esp_err_t mao_board_light_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed);

/* After a light sleep: disarm what light_sleep_prepare armed. */
void mao_board_light_sleep_done(void);

/* Which board lines caused the last wake-up (deep: from the wake status;
 * light: lines no longer at the level noted before the sleep). All false
 * where the board cannot tell. */
void mao_board_wake_decode(mao_board_wake_t *out);

#ifdef __cplusplus
}
#endif
