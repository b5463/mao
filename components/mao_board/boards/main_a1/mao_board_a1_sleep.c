/*
 * MAO_MAIN A1 sleep wiring: which pads wake the chip, and what every rail
 * and pad does while it sleeps.
 *
 * Deep sleep (RTC pads only):
 *   - ext1 ANY_LOW: face press PRESS_N (GPIO14, 100 k pull-up) and IMU INT1 (GPIO4)
 *     (open drain, active low, latched, 100 k pull-up);
 *   - ext0: dial channel A HALL_A (GPIO21), armed at the level it is NOT at now, so
 *     any turn of the ring wakes MAO (the Hall latch keeps sampling in its
 *     low-power mode, HALL_FAST low);
 *   - VBUS_SENSE (GPIO39) is not an RTC pad: USB cannot wake MAO from deep
 *     sleep (a caller that needs it uses a timer).
 *   A line that is already asserted would wake the chip at once, so it is
 *   left out (the caller sees that in `armed`).
 * Every enable goes low and is held there (each also has a pull-down), the
 * display bus is driven low and held, the backlight PWM stops and its pin is
 * held low, /CE is held low (charging enabled: nothing watches the
 * temperature while the chip sleeps; the charger's own TS input still does).
 *
 * Light sleep: the knob (mao_board_knob_wake_arm) plus, on request, USB
 * plug / unplug (VBUS_SENSE at the level it is not at) and the proximity
 * threshold, all through the GPIO wake source. The levels before the sleep
 * are noted so mao_board_wake_decode() can tell which line moved. Every
 * enable and the panel's lines are held as they are for the sleep (the
 * panel stays powered and showing its resting frame; the pads' sleep
 * configuration must not reach them); the backlight pin is not held, its
 * PWM keeps running (LEDC on RC_FAST).
 *
 * Holds and RTC routing outlive a deep sleep, so every boot releases them
 * (a1_sleep_release_pads, from mao_board_init).
 */
#include "mao_board.h"
#include "mao_board_a1_priv.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "MAO_BOARD";

/* Pads a deep sleep holds. */
static const int kHeldPads[] = {
    MAO_PIN_LCD_DC, MAO_PIN_LCD_CS, MAO_PIN_LCD_MOSI, MAO_PIN_LCD_SCLK, MAO_PIN_LCD_BL, MAO_PIN_LCD_PWR_EN,
    MAO_PIN_LCD_RST_N, MAO_PIN_HALL_FAST, MAO_PIN_AMP_SD, MAO_PIN_HAPTIC_EN, MAO_PIN_TOF_XSHUT,
    MAO_PIN_AUX_PWR_EN, MAO_PIN_IR_TX, MAO_PIN_CHG_CE_N,
};
/* Pads ext0 / ext1 route to the RTC IO mux. */
static const int kWakePads[] = { MAO_PIN_PRESS_N, MAO_PIN_HALL_A, MAO_PIN_IMU_INT1 };

/* Light sleep: what was armed besides the knob, and the levels before. */
static bool s_light_usb;
static bool s_light_prox;
static bool s_light_noted;
static int s_lv_a, s_lv_b, s_lv_press, s_lv_usb;

void a1_sleep_release_pads(void)
{
    for (size_t i = 0; i < sizeof(kHeldPads) / sizeof(kHeldPads[0]); i++) {
        gpio_hold_dis((gpio_num_t)kHeldPads[i]);
    }
    for (size_t i = 0; i < sizeof(kWakePads) / sizeof(kWakePads[0]); i++) {
        rtc_gpio_deinit((gpio_num_t)kWakePads[i]);
    }
}

static bool line_idle_high(int gpio)
{
    return gpio_get_level((gpio_num_t)gpio) == 1;
}

esp_err_t mao_board_deep_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed)
{
    ESP_RETURN_ON_FALSE(want, ESP_ERR_INVALID_ARG, TAG, "bad args");

    /* Light off, the panel bus parked low before its rail collapses, then
     * every enable low and held. */
    a1_backlight_hold(true);
    a1_display_park(true);
    for (size_t i = 0; i < sizeof(kHeldPads) / sizeof(kHeldPads[0]); i++) {
        const int g = kHeldPads[i];
        if (g == MAO_PIN_LCD_BL || g == MAO_PIN_LCD_DC || g == MAO_PIN_LCD_CS || g == MAO_PIN_LCD_MOSI ||
            g == MAO_PIN_LCD_SCLK) {
            continue;                                   /* already held above */
        }
        gpio_set_level((gpio_num_t)g, 0);               /* HALL_FAST low = the Hall latches' low-power mode */
        gpio_hold_en((gpio_num_t)g);
    }

    mao_board_wake_t a = { 0 };
    uint64_t mask = 0;
    if (want->press && line_idle_high(MAO_PIN_PRESS_N)) {
        mask |= 1ULL << MAO_PIN_PRESS_N;
        a.press = true;
    }
    if (want->motion && line_idle_high(MAO_PIN_IMU_INT1)) {
        mask |= 1ULL << MAO_PIN_IMU_INT1;
        a.motion = true;
    }
    if (mask) {
        ESP_RETURN_ON_ERROR(esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW), TAG, "ext1");
    }
    int dial_level = -1;
    if (want->dial) {
        dial_level = gpio_get_level((gpio_num_t)MAO_PIN_HALL_A) ? 0 : 1;   /* the level it is not at */
        ESP_RETURN_ON_ERROR(esp_sleep_enable_ext0_wakeup((gpio_num_t)MAO_PIN_HALL_A, dial_level), TAG, "ext0");
        a.dial = true;
    }
    if (armed) {
        *armed = a;
    }
    ESP_LOGI(TAG, "deep sleep: rails off and held, ext1 mask 0x%llx, dial wake %s", (unsigned long long)mask,
             dial_level < 0 ? "off" : (dial_level ? "on high" : "on low"));
    return ESP_OK;
}

void mao_board_deep_sleep_hold(bool hold)
{
    if (!hold) {
        a1_sleep_release_pads();
        return;
    }
    const mao_board_wake_t want = { .press = true, .dial = true, .motion = true };
    mao_board_deep_sleep_prepare(&want, NULL);
}

/* Light sleep: hold the outputs exactly as they are (not the backlight). */
static void light_hold(bool hold)
{
    for (size_t i = 0; i < sizeof(kHeldPads) / sizeof(kHeldPads[0]); i++) {
        if (kHeldPads[i] == MAO_PIN_LCD_BL) {
            continue;
        }
        if (hold) {
            gpio_hold_en((gpio_num_t)kHeldPads[i]);
        } else {
            gpio_hold_dis((gpio_num_t)kHeldPads[i]);
        }
    }
}

esp_err_t mao_board_light_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed)
{
    ESP_RETURN_ON_FALSE(want, ESP_ERR_INVALID_ARG, TAG, "bad args");
    light_hold(true);
    mao_board_wake_t a = { 0 };
    s_lv_a = gpio_get_level((gpio_num_t)MAO_PIN_HALL_A);
    s_lv_b = gpio_get_level((gpio_num_t)MAO_PIN_HALL_B);
    s_lv_press = gpio_get_level((gpio_num_t)MAO_PIN_PRESS_N);
    s_lv_usb = gpio_get_level((gpio_num_t)MAO_PIN_VBUS_SENSE);
    s_light_noted = true;
    s_light_usb = false;
    s_light_prox = false;
    /* A level-type GPIO interrupt still enabled at the wake would fire
     * without end: the owners' interrupts are paused while the pads are wake
     * sources (the wake itself does not need them) and restored by
     * mao_board_light_sleep_done() with the owners' edge types. */
    if (want->usb) {
        gpio_intr_disable((gpio_num_t)MAO_PIN_VBUS_SENSE);
        ESP_RETURN_ON_ERROR(gpio_wakeup_enable((gpio_num_t)MAO_PIN_VBUS_SENSE,
                                               s_lv_usb ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL), TAG, "usb wake");
        s_light_usb = true;
        a.usb = true;
    }
    if (want->proximity && line_idle_high(MAO_PIN_TOF_INT_N)) {
        gpio_intr_disable((gpio_num_t)MAO_PIN_TOF_INT_N);
        ESP_RETURN_ON_ERROR(gpio_wakeup_enable((gpio_num_t)MAO_PIN_TOF_INT_N, GPIO_INTR_LOW_LEVEL), TAG, "tof wake");
        s_light_prox = true;
        a.proximity = true;
    }
    if (armed) {
        *armed = a;
    }
    return ESP_OK;
}

void mao_board_light_sleep_done(void)
{
    light_hold(false);
    if (s_light_usb) {
        gpio_wakeup_disable((gpio_num_t)MAO_PIN_VBUS_SENSE);
        gpio_set_intr_type((gpio_num_t)MAO_PIN_VBUS_SENSE, GPIO_INTR_ANYEDGE);   /* mao_battery: both edges */
        gpio_intr_enable((gpio_num_t)MAO_PIN_VBUS_SENSE);
        s_light_usb = false;
    }
    if (s_light_prox) {
        gpio_wakeup_disable((gpio_num_t)MAO_PIN_TOF_INT_N);
        gpio_set_intr_type((gpio_num_t)MAO_PIN_TOF_INT_N, GPIO_INTR_NEGEDGE);    /* mao_sense: active low */
        gpio_intr_enable((gpio_num_t)MAO_PIN_TOF_INT_N);
        s_light_prox = false;
    }
}

void mao_board_wake_decode(mao_board_wake_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) {
        const uint64_t pins = esp_sleep_get_ext1_wakeup_status();
        out->press = (pins & (1ULL << MAO_PIN_PRESS_N)) != 0;
        out->motion = (pins & (1ULL << MAO_PIN_IMU_INT1)) != 0;
    }
    if (causes & BIT(ESP_SLEEP_WAKEUP_EXT0)) {
        out->dial = true;
    }
    if ((causes & BIT(ESP_SLEEP_WAKEUP_GPIO)) && s_light_noted) {
        out->dial = gpio_get_level((gpio_num_t)MAO_PIN_HALL_A) != s_lv_a ||
                    gpio_get_level((gpio_num_t)MAO_PIN_HALL_B) != s_lv_b;
        out->press = gpio_get_level((gpio_num_t)MAO_PIN_PRESS_N) == 0 || s_lv_press == 0;
        out->usb = gpio_get_level((gpio_num_t)MAO_PIN_VBUS_SENSE) != s_lv_usb;
        out->proximity = gpio_get_level((gpio_num_t)MAO_PIN_TOF_INT_N) == 0;
    }
}
