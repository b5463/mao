/*
 * MAO_MAIN A0 board implementation: identity, capabilities, input, touch,
 * IR and interrupt descriptors, rails and status lines.
 *
 * Bring-up order inside mao_board_init(): release whatever the last deep
 * sleep left held, put the dial sensors in fast mode, read the board ID
 * (once, before Wi-Fi starts), then the I2C bus and the expander with every
 * rail off. The display, amplifier, haptic driver,
 * proximity sensor and IR receiver stay unpowered until their owners ask.
 */
#include "mao_board.h"
#include "mao_board_priv.h"
#include "mao_board_a0_priv.h"

#include <string.h>
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_chip_info.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mao_system.h"

static const char *TAG = "MAO_BOARD";

static int s_id_mv = -1;
static const char *s_revision = "?";
static bool s_i2c_ok;
static bool s_mic_on;

/* ------------------------------------------------------------------------ */
/* Board ID                                                                 */
/* ------------------------------------------------------------------------ */

/* One-shot read of the 1M/1M divider (+ 100 nF) on the board-ID pin. The ADC unit
 * is released again so nothing else is blocked from it. */
static int read_board_id_mv(void)
{
    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel(MAO_PIN_BOARD_ID, &unit, &channel) != ESP_OK) {
        return -1;
    }
    adc_oneshot_unit_handle_t adc = NULL;
    const adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = unit };
    if (adc_oneshot_new_unit(&unit_cfg, &adc) != ESP_OK) {
        return -1;
    }
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    int mv = -1;
    if (adc_oneshot_config_channel(adc, channel, &chan_cfg) == ESP_OK) {
        adc_cali_handle_t cali = NULL;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        const adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id = unit,
            .chan = channel,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) != ESP_OK) {
            cali = NULL;
        }
#endif
        int sum = 0;
        int n = 0;
        for (int i = 0; i < A0_ID_SAMPLES; i++) {
            int raw = 0;
            if (adc_oneshot_read(adc, channel, &raw) == ESP_OK) {
                sum += raw;
                n++;
            }
        }
        if (n > 0) {
            const int raw = sum / n;
            if (!cali || adc_cali_raw_to_voltage(cali, raw, &mv) != ESP_OK) {
                mv = raw * 3100 / 4095;   /* uncalibrated: 12 dB range is ~0..3.1 V */
            }
        }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        if (cali) {
            adc_cali_delete_scheme_curve_fitting(cali);
        }
#endif
    }
    adc_oneshot_del_unit(adc);
    return mv;
}

const char *mao_board_revision(void)
{
    return s_revision;
}

int mao_board_revision_mv(void)
{
    return s_id_mv;
}

/* ------------------------------------------------------------------------ */
/* Init and capabilities                                                    */
/* ------------------------------------------------------------------------ */

static esp_err_t config_irq_inputs(void)
{
    /* Expander INT, ToF GPIO1 and charger PGOOD are open-drain with external
     * pull-ups. The IMU drives INT1/INT2 push-pull and gets NO pull: an
     * LSM6DSOX that sees INT1 high while it powers up (a brown-out, a 3V3
     * glitch) switches to I3C-only and never answers I2C again. Without the
     * IMU the lines float; the IMU's ISRs and its deep-sleep wake are only
     * armed when it answered at boot. */
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << MAO_PIN_EXP_INT_N) | (1ULL << MAO_PIN_TOF_INT_N) |
                        (1ULL << MAO_PIN_USB_PRESENT_N) | (1ULL << MAO_PIN_IMU_INT1) |
                        (1ULL << MAO_PIN_IMU_INT2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

esp_err_t mao_board_init(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_RETURN_ON_FALSE(chip.model == CHIP_ESP32S3, ESP_ERR_NOT_SUPPORTED, TAG,
                        "unexpected chip model %d", (int)chip.model);

    a0_sleep_release_pads();

    /* Dial sensors sample at 2.5 kHz while awake (20 Hz in deep sleep). */
    const gpio_config_t hall = {
        .pin_bit_mask = 1ULL << MAO_PIN_HALL_FAST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&hall), TAG, "hall fast");
    gpio_set_level(MAO_PIN_HALL_FAST, 1);

    /* The mic is powered from a GPIO (100R / 1 uF): it draws 80 uA even with
     * its clock stopped, so it stays off until someone listens. */
    const gpio_config_t mic = {
        .pin_bit_mask = 1ULL << MAO_PIN_MIC_PWR,
        .mode = GPIO_MODE_INPUT_OUTPUT,   /* readable for the rail self-test */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&mic), TAG, "mic power");
    gpio_set_level(MAO_PIN_MIC_PWR, 0);

    /* Display TE (tearing effect): an input; the pull-down holds it low while
     * the panel rail is off. */
    const gpio_config_t te = {
        .pin_bit_mask = 1ULL << MAO_PIN_LCD_TE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&te), TAG, "lcd te");

    ESP_RETURN_ON_ERROR(config_irq_inputs(), TAG, "irq inputs");

    s_id_mv = read_board_id_mv();
    s_revision = (s_id_mv >= A0_ID_A0_MIN_MV && s_id_mv <= A0_ID_A0_MAX_MV) ? "A0" : "?";
    ESP_LOGI(TAG, "board: %s, revision %s (ID %d mV)", MAO_BOARD_NAME, s_revision, s_id_mv);

    /* Missing devices (even the expander) are reported by a0_i2c_init() and
     * do not fail the board; only a bus that cannot be created does. */
    const esp_err_t err = a0_i2c_init();
    s_i2c_ok = err == ESP_OK;
    mao_board_common_init();
    return err;
}

void mao_board_get_caps(mao_board_caps_t *out)
{
    if (!out) {
        return;
    }
    const bool exp = a0_expander_ok();
    *out = (mao_board_caps_t) {
        .rgb_led = false,             /* removed on purpose on the A0 */
        .display_power = exp,
        .i2c = s_i2c_ok,
        .expander = exp,
        .amp_switch = exp,
        .mic = true,
        .touch = true,
        .ir = true,
        .power_status = true,
        .charge_control = exp,
        .hall_fast = true,
        .sleep = true,
        .imu = true,
        .tof = true,
        .als = true,
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

    /* Hall latches drive push-pull; the press has an external 10 k pull-up.
     * GPIO0 is the BOOT strap: input only, never driven. */
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
        .transitions_per_detent = A0_DIAL_TRANSITIONS_PER_DETENT,
        .rest_mask = A0_DIAL_REST_MASK,
        .detents_per_rev = A0_DIAL_DETENTS_PER_REV,
        .reverse = A0_DIAL_REVERSE,
    };
    return ESP_OK;
}

esp_err_t mao_board_hall_fast_set(bool fast)
{
    return gpio_set_level(MAO_PIN_HALL_FAST, fast ? 1 : 0);
}

esp_err_t mao_board_touch_get(mao_board_touch_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");
    /* On the ESP32-S3, touch channel n is GPIOn. */
    *out = (mao_board_touch_t) {
        .channel = {
            [MAO_TOUCH_RIGHT] = MAO_PIN_TOUCH_RIGHT,
            [MAO_TOUCH_LEFT] = MAO_PIN_TOUCH_LEFT,
            [MAO_TOUCH_TOP] = MAO_PIN_TOUCH_TOP,
            [MAO_TOUCH_REAR] = MAO_PIN_TOUCH_REAR,
        },
        .wake_zone = MAO_TOUCH_TOP,
    };
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* LED, IR, interrupt lines                                                 */
/* ------------------------------------------------------------------------ */

esp_err_t mao_board_led_init(led_strip_handle_t *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;   /* no RGB LED on the A0, by design */
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
            .carrier_hz = A0_IR_CARRIER_HZ,
            .carrier_duty_pct = A0_IR_CARRIER_DUTY_PCT,
            .rx_settle_ms = A0_IR_RX_SETTLE_MS,
        };
    }
}

esp_err_t mao_board_irq_get(mao_board_irq_t irq, mao_board_irq_desc_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");
    static const int kGpio[MAO_IRQ_COUNT] = {
        [MAO_IRQ_IMU_INT1] = MAO_PIN_IMU_INT1,
        [MAO_IRQ_IMU_INT2] = MAO_PIN_IMU_INT2,
        [MAO_IRQ_TOF] = MAO_PIN_TOF_INT_N,
        [MAO_IRQ_EXPANDER] = MAO_PIN_EXP_INT_N,
        [MAO_IRQ_USB_PRESENT] = MAO_PIN_USB_PRESENT_N,
    };
    ESP_RETURN_ON_FALSE(irq < MAO_IRQ_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad irq");
    /* All active low; the IMU is configured active-low (CTRL3_C H_LACTIVE)
     * by its driver so its lines can join the ext1 ANY_LOW wake-up. */
    *out = (mao_board_irq_desc_t) { .gpio = kGpio[irq], .active_low = true };
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Rails and lines                                                          */
/* ------------------------------------------------------------------------ */

static int rail_bit(mao_board_rail_t rail)
{
    switch (rail) {
    case MAO_RAIL_DISPLAY: return MAO_EXP_LCD_PWR_EN;
    case MAO_RAIL_AMP:     return MAO_EXP_AMP_SD_N;
    case MAO_RAIL_HAPTIC:  return MAO_EXP_HAPTIC_EN;
    case MAO_RAIL_TOF:     return MAO_EXP_TOF_XSHUT;
    case MAO_RAIL_IR_RX:   return MAO_EXP_IR_RX_PWR;
    default:               return -1;
    }
}

esp_err_t mao_board_rail_set(mao_board_rail_t rail, bool on)
{
    if (rail == MAO_RAIL_MIC) {
        s_mic_on = on;
        return gpio_set_level(MAO_PIN_MIC_PWR, on ? 1 : 0);
    }
    const int bit = rail_bit(rail);
    ESP_RETURN_ON_FALSE(bit >= 0, ESP_ERR_INVALID_ARG, TAG, "bad rail");
    ESP_RETURN_ON_FALSE(a0_expander_ok(), ESP_ERR_INVALID_STATE, TAG, "expander missing");
    if (rail == MAO_RAIL_DISPLAY) {
        return a0_display_power(on);   /* needs the reset / bus sequencing */
    }
    return a0_expander_write_bit((uint8_t)bit, on);
}

bool mao_board_rail_is_on(mao_board_rail_t rail)
{
    if (rail == MAO_RAIL_MIC) {
        return s_mic_on;
    }
    const int bit = rail_bit(rail);
    return bit >= 0 && a0_expander_bit((uint8_t)bit);
}

esp_err_t mao_board_rail_readback(mao_board_rail_t rail, bool *on)
{
    ESP_RETURN_ON_FALSE(on, ESP_ERR_INVALID_ARG, TAG, "bad args");
    if (rail == MAO_RAIL_MIC) {
        *on = gpio_get_level(MAO_PIN_MIC_PWR) != 0;
        return ESP_OK;
    }
    const int bit = rail_bit(rail);
    ESP_RETURN_ON_FALSE(bit >= 0, ESP_ERR_INVALID_ARG, TAG, "bad rail");
    /* The input register reflects the pin level of output pins too. */
    uint8_t in = 0;
    ESP_RETURN_ON_ERROR(a0_expander_read_inputs(&in), TAG, "expander inputs");
    *on = (in & (1u << bit)) != 0;
    return ESP_OK;
}

esp_err_t mao_board_line_get(mao_board_line_t line, bool *active)
{
    ESP_RETURN_ON_FALSE(active, ESP_ERR_INVALID_ARG, TAG, "bad args");
    switch (line) {
    case MAO_LINE_USB_PRESENT:
        *active = gpio_get_level(MAO_PIN_USB_PRESENT_N) == 0;   /* PGOOD, active low */
        return ESP_OK;
    case MAO_LINE_CHARGING:
        /* The BQ24073 /CHG output is not connected on this board (P6 is
         * its /CE now): mao_power derives charging from PGOOD and the gauge. */
        return ESP_ERR_NOT_SUPPORTED;
    case MAO_LINE_SENSE_ALERT: {
        uint8_t in = 0;
        ESP_RETURN_ON_ERROR(a0_expander_read_inputs(&in), TAG, "expander inputs");
        *active = (in & (1u << MAO_EXP_SENSE_ALRT_N)) == 0;
        return ESP_OK;
    }
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t mao_board_charge_enable(bool enable)
{
    ESP_RETURN_ON_FALSE(a0_expander_ok(), ESP_ERR_INVALID_STATE, TAG, "expander missing");
    /* P6 = BQ24073 /CE: low enables charging, high pauses it. */
    return a0_expander_write_bit(MAO_EXP_CHG_CE_N, !enable);
}
