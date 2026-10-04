/*
 * MAO_MAIN A0 sleep wiring: which pads wake the chip, and what every rail
 * and pad does while it sleeps.
 *
 * Wake lines (all active low): face press GPIO0, IMU INT1 GPIO2, charger
 * PGOOD GPIO8 and expander INT GPIO21 are RTC pads, combined in one ext1
 * ANY_LOW group. The proximity interrupt (GPIO48, light sleep only: the ToF is
 * off in deep sleep) is not an RTC pad and wakes through the GPIO wake source
 * instead. Two consequences of the ext1 group:
 *   - a line that is already low would wake the chip at once, so it is left
 *     out (the caller sees that in `armed`);
 *   - with USB present PGOOD is low, so unplugging is watched by ext0 on
 *     the same pad waiting for high instead.
 *
 * ext0/ext1 route their pads to the RTC IO mux and the deep-sleep holds
 * outlive the reset, so every boot and every light-sleep exit gives the pads
 * back to the digital GPIO matrix (a0_sleep_release_pads / _light_sleep_done).
 */
#include "mao_board.h"
#include "mao_board_a0_priv.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "MAO_BOARD";

/* Pads a sleep may have routed to RTC IO or held. */
static const int kSleepPads[] = {
    MAO_PIN_PRESS_N, MAO_PIN_IMU_INT1, MAO_PIN_USB_PRESENT_N, MAO_PIN_EXP_INT_N,
    MAO_PIN_LCD_DC, MAO_PIN_LCD_CS, MAO_PIN_LCD_MOSI, MAO_PIN_LCD_SCLK, MAO_PIN_HALL_FAST,
};

static uint64_t s_light_ext1_mask;
static bool s_light_ext0;
static bool s_light_gpio;              /* proximity armed through the GPIO wake source */

static void release_pad(int gpio)
{
    rtc_gpio_hold_dis((gpio_num_t)gpio);
    rtc_gpio_deinit((gpio_num_t)gpio);
}

void a0_sleep_release_pads(void)
{
    for (size_t i = 0; i < sizeof(kSleepPads) / sizeof(kSleepPads[0]); i++) {
        release_pad(kSleepPads[i]);
    }
}

static bool line_idle(int gpio)
{
    return gpio_get_level((gpio_num_t)gpio) == 1;
}

/* Build the ANY_LOW mask from the requested lines that are idle now. */
static uint64_t wake_mask(const mao_board_wake_t *want, bool light, mao_board_wake_t *armed, bool *usb_ext0,
                          bool *prox_gpio)
{
    mao_board_wake_t a = { 0 };
    uint64_t mask = 0;
    *usb_ext0 = false;
    *prox_gpio = false;
    if (want->press && line_idle(MAO_PIN_PRESS_N)) {
        mask |= 1ULL << MAO_PIN_PRESS_N;
        a.press = true;
    }
    if (want->motion && line_idle(MAO_PIN_IMU_INT1)) {
        mask |= 1ULL << MAO_PIN_IMU_INT1;
        a.motion = true;
    }
    if (want->expander && line_idle(MAO_PIN_EXP_INT_N)) {
        mask |= 1ULL << MAO_PIN_EXP_INT_N;
        a.expander = true;
    }
    if (want->usb) {
        if (line_idle(MAO_PIN_USB_PRESENT_N)) {
            mask |= 1ULL << MAO_PIN_USB_PRESENT_N;   /* wake on plug-in */
        } else {
            *usb_ext0 = true;                        /* wake on unplug */
        }
        a.usb = true;
    }
    if (light && want->proximity && line_idle(MAO_PIN_TOF_INT_N)) {
        *prox_gpio = true;                           /* GPIO48: not an RTC pad */
        a.proximity = true;
    }
    if (armed) {
        *armed = a;
    }
    return mask;
}

static esp_err_t arm(uint64_t mask, bool usb_ext0, bool prox_gpio)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (prox_gpio) {
        ESP_RETURN_ON_ERROR(gpio_wakeup_enable((gpio_num_t)MAO_PIN_TOF_INT_N, GPIO_INTR_LOW_LEVEL), TAG, "gpio wake");
        ESP_RETURN_ON_ERROR(esp_sleep_enable_gpio_wakeup(), TAG, "gpio wake");
    }
    if (mask) {
        ESP_RETURN_ON_ERROR(esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW), TAG, "ext1");
    }
    if (usb_ext0) {
        ESP_RETURN_ON_ERROR(esp_sleep_enable_ext0_wakeup((gpio_num_t)MAO_PIN_USB_PRESENT_N, 1), TAG, "ext0");
    }
    return ESP_OK;
}

esp_err_t mao_board_deep_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed)
{
    ESP_RETURN_ON_FALSE(want, ESP_ERR_INVALID_ARG, TAG, "bad args");

    /* Every rail off: panel held in reset, amplifier, haptic driver, ToF and
     * IR receiver unpowered. The display bus is isolated first so the panel
     * is never back-powered while its rail collapses. */
    mao_board_backlight_set(0);
    a0_display_park(true);
    if (a0_expander_ok()) {
        a0_expander_write_all(0x00);
        uint8_t in;
        a0_expander_read_inputs(&in);   /* release a pending INT before arming it */
    }

    /* Mic unpowered (its 100 k pull-down keeps it so through deep sleep). */
    mao_board_rail_set(MAO_RAIL_MIC, false);

    /* Hall latches to 20 Hz sampling (~1.6 uA); the pad hold keeps the level
     * through deep sleep (the pull-down alone would too, but not on purpose). */
    gpio_set_level((gpio_num_t)MAO_PIN_HALL_FAST, 0);
    rtc_gpio_hold_en((gpio_num_t)MAO_PIN_HALL_FAST);

    bool usb_ext0 = false;
    bool prox_gpio = false;
    const uint64_t mask = wake_mask(want, false, armed, &usb_ext0, &prox_gpio);
    ESP_RETURN_ON_ERROR(arm(mask, usb_ext0, false), TAG, "arm");
    ESP_LOGI(TAG, "deep sleep: rails off, ext1 mask 0x%llx%s", (unsigned long long)mask,
             usb_ext0 ? ", ext0 on USB unplug" : "");
    return ESP_OK;
}

esp_err_t mao_board_light_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed)
{
    ESP_RETURN_ON_FALSE(want, ESP_ERR_INVALID_ARG, TAG, "bad args");
    if (a0_expander_ok()) {
        uint8_t in;
        a0_expander_read_inputs(&in);
    }
    bool usb_ext0 = false;
    s_light_ext1_mask = wake_mask(want, true, armed, &usb_ext0, &s_light_gpio);
    s_light_ext0 = usb_ext0;
    return arm(s_light_ext1_mask, usb_ext0, s_light_gpio);
}

void mao_board_light_sleep_done(void)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
    if (s_light_gpio) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
        gpio_wakeup_disable((gpio_num_t)MAO_PIN_TOF_INT_N);
        s_light_gpio = false;
    }
    for (int gpio = 0; gpio < 64; gpio++) {
        if (s_light_ext1_mask & (1ULL << gpio)) {
            release_pad(gpio);
        }
    }
    if (s_light_ext0) {
        release_pad(MAO_PIN_USB_PRESENT_N);
    }
    s_light_ext1_mask = 0;
    s_light_ext0 = false;
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
        out->usb = (pins & (1ULL << MAO_PIN_USB_PRESENT_N)) != 0;
        out->expander = (pins & (1ULL << MAO_PIN_EXP_INT_N)) != 0;
    }
    if (causes & BIT(ESP_SLEEP_WAKEUP_GPIO)) {
        out->proximity = gpio_get_level((gpio_num_t)MAO_PIN_TOF_INT_N) == 0;
    }
    if (causes & BIT(ESP_SLEEP_WAKEUP_EXT0)) {
        out->usb = true;
    }
}
