/*
 * MAO_MAIN A0: the shared I2C bus and the TCA6408A rail / status expander.
 *
 * The expander is board wiring, so its driver lives here and is exposed only
 * as rails and lines (mao_board_rail_set / mao_board_line_get).
 *
 * TCA6408A power-up state: every pin an input, output register 0xFF. If the
 * config register were cleared first, every rail would glitch on. So the
 * output register is ALWAYS written first (0x00: every rail off, panel held
 * in reset), and only then are the output pins enabled.
 *
 * Recovery: MAO_PIN_EXP_RST_N (GPIO38) drives the expander's RESET (open-drain, 10 k pull-up
 * R205 on the board). If a write or an input read fails, the bus is reset,
 * the expander is pulsed into reset and reprogrammed from the shadow
 * registers, and the access is retried once. A wedged expander no longer
 * needs a power cycle.
 *
 * What a reset costs: while RESET is low and until the output register is
 * rewritten (~0.2 ms) every expander pin is an input, so the rails fall to
 * their pull-down defaults: the panel sees LCD_RST_N low and its supply
 * dip, the ToF its XSHUT, the haptic driver its EN. Those devices lose their
 * configuration. Every reset therefore bumps a counter
 * (mao_board_expander_resets()); the display, sense and haptics owners
 * notice the change and re-initialise their device.
 *
 * i2c_master_bus_reset() does not take the driver's bus lock: a transaction
 * another task has in flight at that moment fails once with a timeout and is
 * retried by its owner on the next sample.
 */
#include "mao_board.h"
#include "mao_board_a0_priv.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "mao_system.h"

static const char *TAG = "MAO_BOARD";

/* Settle time after XSHUT / EN goes high before the device answers I2C
 * (VL53L4CD boot 1.2 ms max; DRV2605L a few hundred us). */
#define RAIL_PROBE_SETTLE_MS   2
#define PROBE_TIMEOUT_MS       20

static const uint8_t kAddress[MAO_I2C_DEV_COUNT] = {
    [MAO_I2C_EXPANDER] = MAO_I2C_ADDR_EXPANDER,
    [MAO_I2C_TOF] = MAO_I2C_ADDR_TOF,
    [MAO_I2C_FUEL_GAUGE] = MAO_I2C_ADDR_FUEL_GAUGE,
    [MAO_I2C_ALS] = MAO_I2C_ADDR_ALS,
    [MAO_I2C_HAPTIC] = MAO_I2C_ADDR_HAPTIC,
    [MAO_I2C_IMU] = MAO_I2C_ADDR_IMU,
};

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_exp;
static SemaphoreHandle_t s_exp_lock;
static uint8_t s_exp_out;           /* shadow of the output register */
static bool s_exp_ok;
static bool s_present[MAO_I2C_DEV_COUNT];
static volatile uint32_t s_exp_resets;    /* pulses on EXP_RST_N since boot */

/* ------------------------------------------------------------------------ */
/* Expander                                                                 */
/* ------------------------------------------------------------------------ */

static esp_err_t exp_write(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_exp, buf, sizeof(buf), A0_I2C_TIMEOUT_MS);
}

static esp_err_t exp_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_exp, &reg, 1, value, 1, A0_I2C_TIMEOUT_MS);
}

/* Program the expander from the shadow output register (output before
 * direction, as at power-up). Caller holds the lock or is the init path. */
static esp_err_t exp_program(void)
{
    ESP_RETURN_ON_ERROR(exp_write(A0_EXP_REG_OUTPUT, s_exp_out), TAG, "output");
    ESP_RETURN_ON_ERROR(exp_write(A0_EXP_REG_POLARITY, 0x00), TAG, "polarity");
    ESP_RETURN_ON_ERROR(exp_write(A0_EXP_REG_CONFIG, A0_EXP_INPUT_MASK), TAG, "config");
    return ESP_OK;
}

/* Pulse RESET low. Every register returns to its power-up default (all
 * inputs, output register 0xFF); the owners of the rails are told through
 * the reset counter. */
static void exp_pulse_reset(void)
{
    gpio_set_level(MAO_PIN_EXP_RST_N, 0);
    esp_rom_delay_us(10);                     /* TCA6408A: 4 ns minimum pulse */
    gpio_set_level(MAO_PIN_EXP_RST_N, 1);
    esp_rom_delay_us(10);
    s_exp_resets++;
}

static esp_err_t exp_recover(void)
{
    ESP_LOGW(TAG, "expander: no answer, resetting it (GPIO%d)", MAO_PIN_EXP_RST_N);
    i2c_master_bus_reset(s_bus);
    exp_pulse_reset();
    return exp_program();
}

/* A register write with one recovery attempt (caller holds the lock). */
static esp_err_t exp_write_safe(uint8_t reg, uint8_t value)
{
    esp_err_t err = exp_write(reg, value);
    if (err != ESP_OK && s_exp_ok && exp_recover() == ESP_OK) {
        err = exp_write(reg, value);
    }
    return err;
}

/* A register read with one recovery attempt (caller holds the lock). */
static esp_err_t exp_read_safe(uint8_t reg, uint8_t *value)
{
    esp_err_t err = exp_read(reg, value);
    if (err != ESP_OK && s_exp_ok && exp_recover() == ESP_OK) {
        err = exp_read(reg, value);
    }
    return err;
}

uint32_t mao_board_expander_resets(void)
{
    return s_exp_resets;
}

bool a0_expander_ok(void)
{
    return s_exp_ok;
}

bool a0_expander_bit(uint8_t bit)
{
    return s_exp_ok && (s_exp_out & (1u << bit)) != 0;
}

esp_err_t a0_expander_write_all(uint8_t value)
{
    ESP_RETURN_ON_FALSE(s_exp_ok, ESP_ERR_INVALID_STATE, TAG, "expander missing");
    xSemaphoreTake(s_exp_lock, portMAX_DELAY);
    const uint8_t previous = s_exp_out;
    s_exp_out = value;                        /* a recovery reprograms the new state */
    const esp_err_t err = exp_write_safe(A0_EXP_REG_OUTPUT, value);
    if (err != ESP_OK) {
        s_exp_out = previous;
    }
    xSemaphoreGive(s_exp_lock);
    return err;
}

esp_err_t a0_expander_write_bit(uint8_t bit, bool level)
{
    ESP_RETURN_ON_FALSE(s_exp_ok, ESP_ERR_INVALID_STATE, TAG, "expander missing");
    xSemaphoreTake(s_exp_lock, portMAX_DELAY);
    const uint8_t value = level ? (uint8_t)(s_exp_out | (1u << bit)) : (uint8_t)(s_exp_out & ~(1u << bit));
    esp_err_t err = ESP_OK;
    if (value != s_exp_out) {
        const uint8_t previous = s_exp_out;
        s_exp_out = value;
        err = exp_write_safe(A0_EXP_REG_OUTPUT, value);
        if (err != ESP_OK) {
            s_exp_out = previous;
        }
    }
    xSemaphoreGive(s_exp_lock);
    return err;
}

esp_err_t a0_expander_read_inputs(uint8_t *value)
{
    ESP_RETURN_ON_FALSE(s_exp_ok, ESP_ERR_INVALID_STATE, TAG, "expander missing");
    xSemaphoreTake(s_exp_lock, portMAX_DELAY);
    const esp_err_t err = exp_read_safe(A0_EXP_REG_INPUT, value);
    xSemaphoreGive(s_exp_lock);
    return err;
}

esp_err_t mao_board_expander_test(uint8_t *failed_bits)
{
    if (failed_bits) {
        *failed_bits = 0;
    }
    if (!s_exp) {
        return ESP_ERR_NOT_FOUND;     /* fitted, but it never answered */
    }
    ESP_RETURN_ON_FALSE(s_exp_ok, ESP_ERR_INVALID_STATE, TAG, "expander not configured");
    esp_err_t err = ESP_OK;
    uint8_t bad = 0;
    xSemaphoreTake(s_exp_lock, portMAX_DELAY);
    uint8_t config = 0;
    err = exp_read(A0_EXP_REG_CONFIG, &config);
    if (err == ESP_OK && config != A0_EXP_INPUT_MASK) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    /* Polarity inversion acts on inputs only: patterns on the output pins
     * round-trip through the register without touching any rail. */
    static const uint8_t kPatterns[] = { 0x2A, 0x15 };
    for (size_t i = 0; err == ESP_OK && i < sizeof(kPatterns); i++) {
        const uint8_t pattern = kPatterns[i] & A0_EXP_OUTPUT_MASK;
        uint8_t back = 0;
        err = exp_write(A0_EXP_REG_POLARITY, pattern);
        if (err == ESP_OK) {
            err = exp_read(A0_EXP_REG_POLARITY, &back);
        }
        if (err == ESP_OK && back != pattern) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    exp_write(A0_EXP_REG_POLARITY, 0x00);
    /* Output pins must read back what they drive (input register). */
    uint8_t in = 0;
    if (err == ESP_OK) {
        err = exp_read(A0_EXP_REG_INPUT, &in);
        bad = (uint8_t)((in ^ s_exp_out) & A0_EXP_OUTPUT_MASK);
        if (err == ESP_OK && bad) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    xSemaphoreGive(s_exp_lock);
    if (failed_bits) {
        *failed_bits = bad;
    }
    return err;
}

esp_err_t mao_board_expander_reset_test(uint8_t *config_after_pulse, uint8_t *failed_bits)
{
    if (config_after_pulse) {
        *config_after_pulse = 0;
    }
    if (failed_bits) {
        *failed_bits = 0;
    }
    if (!s_exp) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_FALSE(s_exp_ok, ESP_ERR_INVALID_STATE, TAG, "expander not configured");
    xSemaphoreTake(s_exp_lock, portMAX_DELAY);
    /* 1. The pulse must really reset it: CONFIG reads its default, all
     *    inputs (0xFF). A stuck-high EXP_RST_N would leave 0xC0. */
    exp_pulse_reset();
    uint8_t config = 0;
    esp_err_t err = exp_read(A0_EXP_REG_CONFIG, &config);
    if (config_after_pulse) {
        *config_after_pulse = config;
    }
    const bool was_reset = err == ESP_OK && config == 0xFF;
    /* 2. Reprogram from the shadow and check it took: CONFIG re-read, every
     *    output pin back at its commanded level. */
    esp_err_t prog = exp_program();
    uint8_t bad = 0;
    if (prog == ESP_OK) {
        prog = exp_read(A0_EXP_REG_CONFIG, &config);
        if (prog == ESP_OK && config != A0_EXP_INPUT_MASK) {
            prog = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (prog == ESP_OK) {
        uint8_t in = 0;
        prog = exp_read(A0_EXP_REG_INPUT, &in);
        bad = (uint8_t)((in ^ s_exp_out) & A0_EXP_OUTPUT_MASK);
        if (prog == ESP_OK && bad) {
            prog = ESP_ERR_INVALID_RESPONSE;
        }
    }
    xSemaphoreGive(s_exp_lock);
    if (failed_bits) {
        *failed_bits = bad;
    }
    if (prog != ESP_OK) {
        return prog;
    }
    return was_reset ? ESP_OK : (err != ESP_OK ? err : ESP_ERR_INVALID_STATE);
}

static esp_err_t expander_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_bus, MAO_I2C_ADDR_EXPANDER, PROBE_TIMEOUT_MS), TAG, "probe");
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAO_I2C_ADDR_EXPANDER,
        .scl_speed_hz = A0_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev, &s_exp), TAG, "add");

    /* Output register before direction: see the file comment. */
    s_exp_out = 0x00;
    ESP_RETURN_ON_ERROR(exp_program(), TAG, "program");

    uint8_t config = 0;
    ESP_RETURN_ON_ERROR(exp_read(A0_EXP_REG_CONFIG, &config), TAG, "config readback");
    ESP_RETURN_ON_FALSE(config == A0_EXP_INPUT_MASK, ESP_ERR_INVALID_RESPONSE, TAG,
                        "config readback 0x%02x", config);
    uint8_t in = 0;
    exp_read(A0_EXP_REG_INPUT, &in);   /* clears any power-up interrupt */
    s_exp_ok = true;
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Bus                                                                      */
/* ------------------------------------------------------------------------ */

/* Power the switchable devices (ToF XSHUT, haptic EN) for a probe or scan.
 * Returns the bits that were switched on here, so that only those are
 * switched off again (other rails may change meanwhile). */
static uint8_t probe_power_begin(void)
{
    if (!s_exp_ok) {
        return 0;
    }
    uint8_t added = 0;
    static const uint8_t kBits[] = { MAO_EXP_TOF_XSHUT, MAO_EXP_HAPTIC_EN };
    for (size_t i = 0; i < sizeof(kBits); i++) {
        if (!a0_expander_bit(kBits[i]) && a0_expander_write_bit(kBits[i], true) == ESP_OK) {
            added |= (uint8_t)(1u << kBits[i]);
        }
    }
    if (added) {
        vTaskDelay(pdMS_TO_TICKS(RAIL_PROBE_SETTLE_MS));
    }
    return added;
}

static void probe_power_end(uint8_t added)
{
    for (uint8_t bit = 0; bit < 8; bit++) {
        if (added & (1u << bit)) {
            a0_expander_write_bit(bit, false);
        }
    }
}

esp_err_t a0_i2c_init(void)
{
    s_exp_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_exp_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    /* Expander RESET: released (high) from the first instruction on; the
     * board pull-up already holds it there through boot. */
    gpio_set_level(MAO_PIN_EXP_RST_N, 1);
    const gpio_config_t rst = {
        .pin_bit_mask = 1ULL << MAO_PIN_EXP_RST_N,
        .mode = GPIO_MODE_OUTPUT_OD,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "expander reset pin");

    const i2c_master_bus_config_t cfg = {
        .i2c_port = A0_I2C_PORT,
        .sda_io_num = MAO_PIN_I2C_SDA,
        .scl_io_num = MAO_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   /* external 2.2 k pull-ups */
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "i2c bus");

    /* Expander first: until it is configured every rail is held off only by
     * its pull-down resistors. */
    s_present[MAO_I2C_EXPANDER] = mao_system_report("I2C expander", expander_init()) == ESP_OK;

    const uint8_t added = probe_power_begin();
    int found = s_present[MAO_I2C_EXPANDER] ? 1 : 0;
    for (int d = 0; d < MAO_I2C_DEV_COUNT; d++) {
        if (d == MAO_I2C_EXPANDER) {
            continue;
        }
        s_present[d] = i2c_master_probe(s_bus, kAddress[d], PROBE_TIMEOUT_MS) == ESP_OK;
        found += s_present[d] ? 1 : 0;
        if (!s_present[d]) {
            ESP_LOGW(TAG, "i2c: %s (0x%02X) did not answer", mao_board_i2c_name((mao_board_i2c_dev_t)d), kAddress[d]);
        }
    }
    probe_power_end(added);

    ESP_LOGI(TAG, "i2c: bus %d @ %d kHz, SDA %d SCL %d, %d/%d devices answered",
             A0_I2C_PORT, A0_I2C_HZ / 1000, MAO_PIN_I2C_SDA, MAO_PIN_I2C_SCL, found, MAO_I2C_DEV_COUNT);
    return ESP_OK;
}

esp_err_t mao_board_i2c_bus(i2c_master_bus_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(s_bus, ESP_ERR_INVALID_STATE, TAG, "no bus");
    *out = s_bus;
    return ESP_OK;
}

esp_err_t mao_board_i2c_device(mao_board_i2c_dev_t dev, mao_board_i2c_info_t *out)
{
    ESP_RETURN_ON_FALSE(out && dev < MAO_I2C_DEV_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad args");
    *out = (mao_board_i2c_info_t) {
        .address = kAddress[dev],
        .fitted = true,
        .present = s_present[dev],
    };
    return ESP_OK;
}

esp_err_t mao_board_i2c_add(mao_board_i2c_dev_t dev, i2c_master_dev_handle_t *out)
{
    ESP_RETURN_ON_FALSE(out && dev < MAO_I2C_DEV_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(s_bus, ESP_ERR_INVALID_STATE, TAG, "no bus");
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = kAddress[dev],
        .scl_speed_hz = A0_I2C_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, out);
}

esp_err_t mao_board_i2c_scan(uint8_t *found, size_t max, size_t *count)
{
    ESP_RETURN_ON_FALSE(count, ESP_ERR_INVALID_ARG, TAG, "bad args");
    *count = 0;
    ESP_RETURN_ON_FALSE(s_bus, ESP_ERR_INVALID_STATE, TAG, "no bus");
    const uint8_t added = probe_power_begin();
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_bus, addr, PROBE_TIMEOUT_MS) == ESP_OK) {
            if (found && *count < max) {
                found[*count] = addr;
            }
            (*count)++;
        }
    }
    probe_power_end(added);
    return ESP_OK;
}
