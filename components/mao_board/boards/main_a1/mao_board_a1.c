/*
 * MAO_MAIN A1 board implementation: identity, capabilities, input, IR and
 * interrupt descriptors, rails, status lines and the charger.
 *
 * Bring-up order inside mao_board_init(): release whatever the last deep
 * sleep left held, put the dial sensors in fast mode, configure the status
 * and interrupt inputs, latch every enable low as an output (each already
 * held low by its hardware pull-down, so nothing glitches), then the I2C
 * bus. The display, amplifier, haptic driver, proximity sensor and IR
 * receiver stay unpowered until their owners ask.
 */
#include "mao_board.h"
#include "mao_board_priv.h"
#include "mao_board_a1_priv.h"

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "MAO_BOARD";

static char s_revision[8] = A1_REV_DEFAULT;
static bool s_i2c_ok;

/* Rails that are a single enable GPIO (the display rail is sequenced). */
static int rail_gpio(mao_board_rail_t rail)
{
    switch (rail) {
    case MAO_RAIL_DISPLAY: return MAO_PIN_LCD_PWR_EN;
    case MAO_RAIL_AMP:     return MAO_PIN_AMP_SD;
    case MAO_RAIL_HAPTIC:  return MAO_PIN_HAPTIC_EN;
    case MAO_RAIL_TOF:     return MAO_PIN_TOF_XSHUT;
    case MAO_RAIL_IR_RX:   return MAO_PIN_AUX_PWR_EN;
    default:               return -1;
    }
}

esp_err_t a1_gpio_out(int gpio, int level)
{
    gpio_set_level((gpio_num_t)gpio, level);
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT_OUTPUT,     /* readable for the rail self-test */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio %d", gpio);
    return gpio_set_level((gpio_num_t)gpio, level);
}

/* ------------------------------------------------------------------------ */
/* Revision record                                                          */
/* ------------------------------------------------------------------------ */

static void load_revision(void)
{
    nvs_handle_t h;
    if (nvs_open(A1_REV_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    char rev[sizeof(s_revision)];
    size_t len = sizeof(rev);
    if (nvs_get_str(h, A1_REV_NVS_KEY, rev, &len) == ESP_OK && rev[0]) {
        snprintf(s_revision, sizeof(s_revision), "%s", rev);
    }
    nvs_close(h);
}

const char *mao_board_revision(void)
{
    return s_revision;
}

esp_err_t mao_board_store_revision(const char *rev)
{
    ESP_RETURN_ON_FALSE(rev && rev[0] && strlen(rev) < sizeof(s_revision), ESP_ERR_INVALID_ARG, TAG, "revision");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(A1_REV_NVS_NAMESPACE, NVS_READWRITE, &h), TAG, "nvs");
    esp_err_t err = nvs_set_str(h, A1_REV_NVS_KEY, rev);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        snprintf(s_revision, sizeof(s_revision), "%s", rev);
    }
    return err;
}

/* ------------------------------------------------------------------------ */
/* Init and capabilities                                                    */
/* ------------------------------------------------------------------------ */

static esp_err_t config_inputs(void)
{
    /* No internal pulls anywhere: IMU INT1 has a 100 k pull-up (open drain,
     * active low, latched), the ToF interrupt 10 k, the charger STAT lines
     * 10 k each; USB_PRESENT_N has its 100 k pull-up at the 2N7002's
     * drain (active low: the VBUS divider drives the FET's gate). The display
     * TE line is driven by the panel while its rail is on; the rail is off from
     * reset, so TE starts with its internal pull-down (review N3; released by
     * the display's rail-up). */
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << MAO_PIN_IMU_INT1) | (1ULL << MAO_PIN_TOF_INT_N) |
                        (1ULL << MAO_PIN_USB_PRESENT_N) | (1ULL << MAO_PIN_CHG_STAT1) |
                        (1ULL << MAO_PIN_CHG_STAT2) | (1ULL << MAO_PIN_LCD_TE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "inputs");
    a1_display_te_pull(true, false);                   /* LCD_PWR_EN was just latched low */
    return ESP_OK;
}

esp_err_t mao_board_init(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_RETURN_ON_FALSE(chip.model == CHIP_ESP32S3, ESP_ERR_NOT_SUPPORTED, TAG,
                        "unexpected chip model %d", (int)chip.model);

    a1_sleep_release_pads();

    /* Dial sensors sample fast while awake (low-power in sleep). */
    ESP_RETURN_ON_ERROR(a1_gpio_out(MAO_PIN_HALL_FAST, 1), TAG, "hall fast");

    /* Every enable: latched low as an output (the pull-downs already hold
     * them there; this only makes the firmware own the level). /CE low =
     * charging enabled, the hardware default. */
    static const int kOff[] = {
        MAO_PIN_LCD_PWR_EN, MAO_PIN_LCD_RST_N, MAO_PIN_AMP_SD, MAO_PIN_HAPTIC_EN, MAO_PIN_TOF_XSHUT,
        MAO_PIN_AUX_PWR_EN, MAO_PIN_IR_TX, MAO_PIN_CHG_CE_N,
    };
    for (size_t i = 0; i < sizeof(kOff) / sizeof(kOff[0]); i++) {
        ESP_RETURN_ON_ERROR(a1_gpio_out(kOff[i], 0), TAG, "enable %d", kOff[i]);
    }
    ESP_RETURN_ON_ERROR(config_inputs(), TAG, "inputs");

    load_revision();
    ESP_LOGI(TAG, "board: %s, revision %s", MAO_BOARD_NAME, s_revision);

    /* Missing devices are reported by a1_i2c_init() and do not fail the
     * board; only a bus that cannot be created does. */
    const esp_err_t err = a1_i2c_init();
    s_i2c_ok = err == ESP_OK;
    mao_board_common_init();
    return err;
}

void mao_board_get_caps(mao_board_caps_t *out)
{
    if (!out) {
        return;
    }
    *out = (mao_board_caps_t) {
        .rgb_led = false,
        .display_power = true,
        .i2c = s_i2c_ok,
        .amp_switch = true,
        .audio_pdm_line = false,
        .ir = true,
        .power_status = true,
        .charge_control = true,
        .hall_fast = true,
        .sleep = true,
        .deep_wake_knob = true,
        .imu = true,
        .tof = true,
        .fuel_gauge = true,
        .haptic = true,
    };
}

/* ------------------------------------------------------------------------ */
/* Input                                                                    */
/* ------------------------------------------------------------------------ */

esp_err_t mao_board_input_init(mao_board_encoder_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    /* Hall latches drive push-pull; the press has an external 100 k pull-up
     * (and is off every strap). */
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << MAO_PIN_HALL_A) | (1ULL << MAO_PIN_HALL_B) | (1ULL << MAO_PIN_PRESS_N),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "dial gpio");

    *out = (mao_board_encoder_t) {
        .gpio_a = MAO_PIN_HALL_A,
        .gpio_b = MAO_PIN_HALL_B,
        .gpio_switch = MAO_PIN_PRESS_N,
        .switch_active_low = true,
        .transitions_per_detent = A1_DIAL_TRANSITIONS_PER_DETENT,
        .rest_mask = A1_DIAL_REST_MASK,
        .detents_per_rev = A1_DIAL_DETENTS_PER_REV,
        .reverse = A1_DIAL_REVERSE,
        .press_turn_guard_ms = A1_PRESS_TURN_GUARD_MS,
    };
    return ESP_OK;
}

esp_err_t mao_board_hall_fast_set(bool fast)
{
    return gpio_set_level((gpio_num_t)MAO_PIN_HALL_FAST, fast ? 1 : 0);
}

/* Light-sleep wake from the knob: any GPIO wakes the S3 from LIGHT sleep
 * (level-triggered). Each dial line is armed at the level it is not at now,
 * so any movement wakes; the press wakes when low. */
esp_err_t mao_board_knob_wake_arm(bool arm)
{
    const int pins[3] = { MAO_PIN_HALL_A, MAO_PIN_HALL_B, MAO_PIN_PRESS_N };
    for (int i = 0; i < 3; i++) {
        if (arm) {
            const gpio_int_type_t lvl = i == 2 ? GPIO_INTR_LOW_LEVEL
                                        : (gpio_get_level(pins[i]) ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
            ESP_RETURN_ON_ERROR(gpio_wakeup_enable(pins[i], lvl), TAG, "wake arm");
        } else {
            gpio_wakeup_disable(pins[i]);
        }
    }
    return ESP_OK;
}

bool mao_board_switch_down(void)
{
    return gpio_get_level(MAO_PIN_PRESS_N) == 0;
}

/* ------------------------------------------------------------------------ */
/* LED, IR, interrupt lines                                                 */
/* ------------------------------------------------------------------------ */

esp_err_t mao_board_led_init(led_strip_handle_t *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;   /* no RGB LED on the A1, by design */
}

void mao_board_ir_get(mao_board_ir_t *out)
{
    if (out) {
        *out = (mao_board_ir_t) {
            .gpio = -1,
            .mode_known = true,
            .tx_gpio = MAO_PIN_IR_TX,
            .rx_gpio = MAO_PIN_IR_RX,
            .rx_active_low = true,
            .carrier_hz = A1_IR_CARRIER_HZ,
            .carrier_duty_pct = A1_IR_CARRIER_DUTY_PCT,
            .rx_settle_ms = A1_IR_RX_SETTLE_MS,
        };
    }
}

esp_err_t mao_board_irq_get(mao_board_irq_t irq, mao_board_irq_desc_t *out)
{
    ESP_RETURN_ON_FALSE(out && irq < MAO_IRQ_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad args");
    switch (irq) {
    case MAO_IRQ_IMU_INT1:
        /* ICM-42670-P INT1: open drain, active low, latched (its driver
         * configures it so), 100 k pull-up. */
        *out = (mao_board_irq_desc_t) { .gpio = MAO_PIN_IMU_INT1, .active_low = true };
        break;
    case MAO_IRQ_TOF:
        *out = (mao_board_irq_desc_t) { .gpio = MAO_PIN_TOF_INT_N, .active_low = true };
        break;
    default:
        *out = (mao_board_irq_desc_t) { .gpio = MAO_PIN_USB_PRESENT_N, .active_low = true };
        break;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Rails and lines                                                          */
/* ------------------------------------------------------------------------ */

esp_err_t mao_board_rail_set(mao_board_rail_t rail, bool on)
{
    const int gpio = rail_gpio(rail);
    ESP_RETURN_ON_FALSE(gpio >= 0, ESP_ERR_INVALID_ARG, TAG, "bad rail");
    if (rail == MAO_RAIL_DISPLAY) {
        return a1_display_power(on);   /* needs the reset / bus sequencing */
    }
    return gpio_set_level((gpio_num_t)gpio, on ? 1 : 0);
}

/* The commanded level (the output register), read back through the pad. */
bool mao_board_rail_is_on(mao_board_rail_t rail)
{
    const int gpio = rail_gpio(rail);
    return gpio >= 0 && gpio_get_level((gpio_num_t)gpio) != 0;
}

esp_err_t mao_board_rail_readback(mao_board_rail_t rail, bool *on)
{
    ESP_RETURN_ON_FALSE(on, ESP_ERR_INVALID_ARG, TAG, "bad args");
    const int gpio = rail_gpio(rail);
    ESP_RETURN_ON_FALSE(gpio >= 0, ESP_ERR_INVALID_ARG, TAG, "bad rail");
    *on = gpio_get_level((gpio_num_t)gpio) != 0;   /* input path of an INPUT_OUTPUT pad: the pin level */
    return ESP_OK;
}

esp_err_t mao_board_charger_status(mao_charger_status_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");
    const bool s1 = gpio_get_level((gpio_num_t)MAO_PIN_CHG_STAT1) != 0;
    const bool s2 = gpio_get_level((gpio_num_t)MAO_PIN_CHG_STAT2) != 0;
    /* BQ25185 STAT1 / STAT2 (open drain, pulled up): H/H done or no input,
     * H/L charging, L/H recoverable fault, L/L latched fault. VERIFY AT
     * BRING-UP against the datasheet table for the fitted part. */
    if (s1 && s2) {
        *out = MAO_CHARGER_IDLE;
    } else if (s1) {
        *out = MAO_CHARGER_CHARGING;
    } else if (s2) {
        *out = MAO_CHARGER_FAULT;
    } else {
        *out = MAO_CHARGER_FAULT_LATCHED;
    }
    return ESP_OK;
}

esp_err_t mao_board_line_get(mao_board_line_t line, bool *active)
{
    ESP_RETURN_ON_FALSE(active, ESP_ERR_INVALID_ARG, TAG, "bad args");
    switch (line) {
    case MAO_LINE_USB_PRESENT:
        *active = gpio_get_level((gpio_num_t)MAO_PIN_USB_PRESENT_N) == 0;   /* inverter: low = VBUS */
        return ESP_OK;
    case MAO_LINE_CHARGING: {
        mao_charger_status_t s;
        ESP_RETURN_ON_ERROR(mao_board_charger_status(&s), TAG, "stat");
        *active = s == MAO_CHARGER_CHARGING;
        return ESP_OK;
    }
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t mao_board_charge_enable(bool enable)
{
    /* CHG_CE_N = the BQ25185's /CE: low enables charging (also the 100 k
     * pull-down default), high pauses it. */
    return gpio_set_level((gpio_num_t)MAO_PIN_CHG_CE_N, enable ? 0 : 1);
}
