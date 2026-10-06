/*
 * MAO boot check and factory / board self-test. See mao_selftest.h and
 * docs/hardware/mao-factory-test.md (procedure, fixture, limits).
 *
 * The self-test only uses the public APIs of the drivers: board (I2C, rails,
 * lines, charger status, revision), sense (identity registers,
 * observations), battery (gauge, charger), haptics (identity, calibration),
 * audio (test chirp), IR (send, receive counters), input (counters), display
 * (test plate, rail cycle, backlight), radio (counters). It never touches a
 * pin it did not get from a board descriptor.
 *
 * MAO_MAIN A1 against the A0 test: no board-ID, expander, light-sensor,
 * touch or mic steps (that hardware is gone); new: vbus, charger_stat
 * (BQ25185 STAT1/STAT2), charge_pause (/CE), backlight_sink (the LEDC-fed
 * current sink at the rest level). The speaker is confirmed by ear (no mic
 * loopback).
 *
 * Machine-readable lines go to stdout with printf (no log prefix, so the
 * fixture script can parse them in release-level logging as well).
 */
#include "mao_selftest.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "mao_audio.h"
#include "mao_board.h"
#include "mao_display.h"
#include "mao_events.h"
#include "mao_haptics.h"
#include "mao_input.h"
#include "mao_ir.h"
#include "mao_battery.h"
#include "mao_radio.h"
#include "mao_sense.h"
#include "mao_settings.h"
#include "mao_system.h"

static const char *TAG = "MAO_SELFTEST";

#define TASK_STACK              6144
#define TASK_PRIO               2
#define POLL_MS                 20
#define PROMPT_TIMEOUT_MS       (CONFIG_MAO_SELFTEST_PROMPT_TIMEOUT_S * 1000)
#define DETAIL_LEN              72
#define MAX_STEPS               40
#define RESULT_HOLD_MS          15000

/* ---- Pass / fail limits (docs/hardware/mao-factory-test.md) -------------- */
#define LIM_ACCEL_MIN_G         0.85f
#define LIM_ACCEL_MAX_G         1.15f
#define LIM_TOF_RESULTS         3          /* new results within LIM_TOF_WINDOW_MS */
#define LIM_TOF_WINDOW_MS       1500
#define LIM_TOF_NEAR_MM         120        /* operator's hand over the window */
#define LIM_VCELL_MIN_MV        3000
#define LIM_VCELL_MAX_MV        4400
/* VERIFY AT BRING-UP: LRA LD0832AA nominal 235 Hz. */
#define LIM_LRA_MIN_HZ          190
#define LIM_LRA_MAX_HZ          285
#define CHIRP_LEVEL_PCT         70
#define IR_TEST_ADDRESS         0xA5
#define IR_TEST_COMMAND         0x3C
/* Switched rails at their probe pads (the display logic rail behind
 * LCD_PWR_EN, the IR receiver supply behind AUX_PWR_EN; both TPS22916C load
 * switches on +3V3), measured to GND by the fixture DMM or the operator.
 * A1 pads (hardware/mao/design/circuit.py TEST_PADS): TP13 = 3V3_LCD, TP15 =
 * IR_RX_VCC. VERIFY AT BRING-UP: the limits against the A1 +3V3. */
#define PAD_LCD                 "TP13"
#define PAD_IRV                 "TP15"
#define LIM_PAD_OFF_MAX_MV      300
#define LIM_PAD_ON_MIN_MV       2850
#define LIM_PAD_ON_MAX_MV       3400
#define MEASURE_AUTO_TIMEOUT_MS 5000       /* automatic mode: a fixture DMM answers at once */
#define CHARGE_PAUSE_SETTLE_MS  1500       /* /CE high -> STAT leaves "charging" (VERIFY AT BRING-UP) */
#define BACKLIGHT_REST_PCT      3          /* the M4.1 sleeping screen (MAO_PWR_SLEEP_PCT) */

typedef enum { R_PASS = 0, R_FAIL, R_SKIP } result_t;

typedef struct {
    const char *id;
    result_t (*run)(char *detail);
    bool interactive;           /* needs an operator */
    bool (*applies)(void);      /* hardware present on this board */
} step_t;

typedef struct {
    const char *id;
    result_t result;
    char detail[DETAIL_LEN];
} outcome_t;

static mao_boot_check_t s_boot;
static mao_board_caps_t s_caps;
static TaskHandle_t s_task;
static volatile bool s_running;
static volatile bool s_interactive;
static volatile int s_answer;           /* 0 none, 1 yes, -1 no, 2 skip, 3 measured value */
static volatile int s_measured_mv;
static volatile bool s_no_pads;         /* no DMM and no operator for the probe pads */
static volatile bool s_abort;
static outcome_t s_out[MAX_STEPS];
static char s_json[5120];     /* ~26 steps x ~110 B + header */

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
}

static void out_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out_line(const char *fmt, ...)
{
    /* One write per line, so other tasks' log output cannot split it. */
    char line[240];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n > (int)sizeof(line) - 2) {
        n = (int)sizeof(line) - 2;
    }
    line[n] = '\n';
    line[n + 1] = '\0';
    fputs(line, stdout);
    fflush(stdout);
}

static void screen(uint32_t bg, const char *big, const char *small)
{
    mao_display_test_show(bg, bg == 0xFFFFFF || bg == 0x00FF00 ? 0x000000 : 0xFFFFFF, big, small);
}

static bool i2c_fitted(mao_board_i2c_dev_t dev)
{
    mao_board_i2c_info_t info;
    return mao_board_i2c_device(dev, &info) == ESP_OK && info.fitted;
}

static uint32_t presses(void)
{
    mao_input_stats_t st;
    mao_input_get_stats(&st);
    return st.presses;
}

/* Operator interaction. Returns 1 when cond() became true (or "yes"), -1 on
 * "no", 2 on "skip" / abort, 0 on timeout. A face press counts as "yes" for
 * confirm prompts (press_is_yes). */
static int wait_for(bool (*cond)(void *), void *ctx, uint32_t timeout_ms, bool press_is_yes)
{
    const uint32_t start = now_ms();
    const uint32_t p0 = presses();
    for (;;) {
        if (s_abort) {
            return 2;
        }
        const int a = s_answer;
        if (a != 0) {
            s_answer = 0;
            return a;
        }
        if (cond && cond(ctx)) {
            return 1;
        }
        if (press_is_yes && presses() != p0) {
            return 1;
        }
        if (now_ms() - start >= timeout_ms) {
            return 0;
        }
        sleep_ms(POLL_MS);
    }
}

static void prompt(const char *id, const char *kind, const char *text, const char *big, const char *small)
{
    s_answer = 0;
    out_line("SELFTEST PROMPT %s %s %s", id, kind, text);
    screen(0x203040, big, small);
}

/* ------------------------------------------------------------------------ */
/* Applicability                                                            */
/* ------------------------------------------------------------------------ */

static bool has_i2c(void)       { return i2c_fitted(MAO_I2C_IMU) || i2c_fitted(MAO_I2C_FUEL_GAUGE); }
static bool has_imu(void)       { return s_caps.imu; }
static bool has_tof(void)       { return s_caps.tof; }
static bool has_gauge(void)     { return s_caps.fuel_gauge; }
static bool has_charger(void)   { return s_caps.power_status; }
static bool has_ce(void)        { return s_caps.charge_control; }
static bool has_haptic(void)    { return s_caps.haptic; }
static bool has_lcd_rail(void)  { return s_caps.display_power; }
static bool has_ir(void)        { mao_board_ir_t ir; mao_board_ir_get(&ir); return ir.tx_gpio >= 0 && ir.rx_gpio >= 0; }
static bool always(void)        { return true; }

/* ------------------------------------------------------------------------ */
/* Automatic steps                                                          */
/* ------------------------------------------------------------------------ */

static result_t t_i2c_bus(char *d)
{
    uint8_t found[16];
    size_t count = 0;
    const esp_err_t err = mao_board_i2c_scan(found, sizeof(found), &count);
    if (err != ESP_OK) {
        snprintf(d, DETAIL_LEN, "scan failed: %s", esp_err_to_name(err));
        return R_FAIL;
    }
    int n = 0, want = 0, have = 0;
    char missing[40] = "";
    int mn = 0;
    for (int dev = 0; dev < MAO_I2C_DEV_COUNT; dev++) {
        mao_board_i2c_info_t info;
        if (mao_board_i2c_device((mao_board_i2c_dev_t)dev, &info) != ESP_OK || !info.fitted) {
            continue;
        }
        want++;
        bool ok = false;
        for (size_t i = 0; i < count && i < sizeof(found); i++) {
            ok |= found[i] == info.address;
        }
        if (ok) {
            have++;
        } else if (mn < (int)sizeof(missing) - 6) {
            mn += snprintf(missing + mn, sizeof(missing) - mn, " %02X", info.address);
        }
    }
    n = snprintf(d, DETAIL_LEN, "%d/%d answered", have, want);
    if (mn) {
        snprintf(d + n, DETAIL_LEN - n, ", missing%s", missing);
    } else if ((int)count > want) {
        snprintf(d + n, DETAIL_LEN - n, ", %d unexpected", (int)count - want);
    }
    return have == want ? R_PASS : R_FAIL;
}

static result_t identify(char *d, mao_obs_kind_t kind, uint32_t want, uint32_t alt, const char *alt_name)
{
    uint32_t id = 0;
    const esp_err_t err = mao_sense_identify(kind, &id);
    if (err != ESP_OK) {
        snprintf(d, DETAIL_LEN, "no answer (%s)", esp_err_to_name(err));
        return R_FAIL;
    }
    if (id == want) {
        snprintf(d, DETAIL_LEN, "id 0x%" PRIX32, id);
        return R_PASS;
    }
    if (alt && id == alt) {
        snprintf(d, DETAIL_LEN, "id 0x%" PRIX32 " (%s)", id, alt_name);
        return R_PASS;
    }
    snprintf(d, DETAIL_LEN, "id 0x%" PRIX32 ", expected 0x%" PRIX32, id, want);
    return R_FAIL;
}

static result_t t_imu_id(char *d)  { return identify(d, MAO_OBS_IMU, 0x67, 0, NULL); }   /* ICM-42670-P */
static result_t t_tof_id(char *d)  { return identify(d, MAO_OBS_TOF, 0xEBAA, 0, NULL); }

static result_t t_imu_motion(char *d)
{
    sleep_ms(250);
    mao_obs_t o;
    if (!mao_sense_latest(MAO_OBS_IMU, &o) || now_ms() - o.time_ms > 1500) {
        snprintf(d, DETAIL_LEN, "no recent accelerometer data");
        return R_FAIL;
    }
    const mao_vec3_t a = o.imu.accel_g;
    const float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    snprintf(d, DETAIL_LEN, "|a| %.3f g (%.2f %.2f %.2f)", (double)g, (double)a.x, (double)a.y, (double)a.z);
    return g >= LIM_ACCEL_MIN_G && g <= LIM_ACCEL_MAX_G ? R_PASS : R_FAIL;
}

static result_t t_tof_ranging(char *d)
{
    mao_obs_t o;
    uint32_t last = mao_sense_latest(MAO_OBS_TOF, &o) ? o.time_ms : 0;
    int results = 0;
    const uint32_t start = now_ms();
    while (now_ms() - start < LIM_TOF_WINDOW_MS) {
        if (mao_sense_latest(MAO_OBS_TOF, &o) && o.time_ms != last) {
            last = o.time_ms;
            results++;
        }
        sleep_ms(POLL_MS);
    }
    if (results == 0) {
        snprintf(d, DETAIL_LEN, "no ranging results");
        return R_FAIL;
    }
    snprintf(d, DETAIL_LEN, "%d results in %d ms, last %u mm status %u", results, LIM_TOF_WINDOW_MS,
             o.tof.distance_mm, o.tof.range_status);
    return results >= LIM_TOF_RESULTS ? R_PASS : R_FAIL;
}

static result_t t_gauge_id(char *d)
{
    uint16_t v = 0;
    const esp_err_t err = mao_battery_gauge_version_get(&v);
    snprintf(d, DETAIL_LEN, "VERSION 0x%04X (%s)", v, esp_err_to_name(err));
    return err == ESP_OK && (v & 0xFFF0) == 0x0010 ? R_PASS : R_FAIL;
}

static result_t t_gauge_voltage(char *d)
{
    const esp_err_t err = mao_battery_refresh();
    mao_battery_status_t st;
    mao_battery_get_status(&st);
    snprintf(d, DETAIL_LEN, "%u mV, %.1f %% (%s)", st.voltage_mv, (double)st.soc_pct, esp_err_to_name(err));
    return err == ESP_OK && st.voltage_mv >= LIM_VCELL_MIN_MV && st.voltage_mv <= LIM_VCELL_MAX_MV &&
           st.soc_pct >= 0.0f && st.soc_pct <= 100.5f ? R_PASS : R_FAIL;
}

static result_t t_vbus(char *d)
{
    /* The test runs over USB: VBUS_SENSE (100 k / 150 k divider) must read
     * present. */
    bool usb = false;
    const esp_err_t err = mao_board_line_get(MAO_LINE_USB_PRESENT, &usb);
    snprintf(d, DETAIL_LEN, "VBUS_SENSE=%d (%s)", usb, esp_err_to_name(err));
    return err == ESP_OK && usb ? R_PASS : R_FAIL;
}

static result_t t_charger_stat(char *d)
{
    /* BQ25185 STAT1 / STAT2 on USB: charging (H/L) or done (H/H) are fine,
     * a fault (L/x) is not. Where firmware limits the charge temperature
     * (/CE), the board temperature it relies on must be readable. */
    mao_charger_status_t cs = MAO_CHARGER_IDLE;
    const esp_err_t e1 = mao_board_charger_status(&cs);
    float temp = 0.0f;
    const esp_err_t et = s_caps.charge_control ? mao_sense_imu_temperature(&temp) : ESP_ERR_NOT_SUPPORTED;
    const int n = snprintf(d, DETAIL_LEN, "STAT %s%s", e1 == ESP_OK ? mao_board_charger_status_name(cs) : "?",
                           e1 ? " (read error)" : "");
    if (s_caps.charge_control) {
        if (et == ESP_OK) {
            snprintf(d + n, DETAIL_LEN - n, ", board %.1f C", (double)temp);
        } else {
            snprintf(d + n, DETAIL_LEN - n, ", no board temp (%s)", esp_err_to_name(et));
        }
    }
    const bool stat_ok = cs == MAO_CHARGER_CHARGING || cs == MAO_CHARGER_IDLE;
    return e1 == ESP_OK && stat_ok && (!s_caps.charge_control || et == ESP_OK) ? R_PASS : R_FAIL;
}

static result_t t_charge_pause(char *d)
{
    /* /CE high must stop a charge in progress: STAT leaves H/L. Only
     * provable while the cell is charging (a full cell reads H/H anyway). */
    mao_charger_status_t before = MAO_CHARGER_IDLE, paused = MAO_CHARGER_IDLE;
    if (mao_board_charger_status(&before) != ESP_OK) {
        snprintf(d, DETAIL_LEN, "no STAT lines");
        return R_FAIL;
    }
    if (before != MAO_CHARGER_CHARGING) {
        snprintf(d, DETAIL_LEN, "not charging (%s): /CE not provable", mao_board_charger_status_name(before));
        return R_SKIP;
    }
    esp_err_t err = mao_board_charge_enable(false);
    sleep_ms(CHARGE_PAUSE_SETTLE_MS);
    mao_board_charger_status(&paused);
    mao_board_charge_enable(true);    /* the battery monitor's limit takes over again on its next pass */
    snprintf(d, DETAIL_LEN, "/CE high: STAT %s (%s)", mao_board_charger_status_name(paused), esp_err_to_name(err));
    return err == ESP_OK && paused != MAO_CHARGER_CHARGING ? R_PASS : R_FAIL;
}

static result_t t_haptic_id(char *d)
{
    uint8_t id = 0;
    const esp_err_t err = mao_haptics_identify(&id, 1000);
    snprintf(d, DETAIL_LEN, "DEVICE_ID %u (%s)", id, esp_err_to_name(err));
    return err == ESP_OK && (id == 7 || id == 3) ? R_PASS : R_FAIL;
}

static result_t t_haptic_cal(char *d)
{
    mao_haptics_cal_info_t before, after;
    mao_haptics_get_cal_info(&before);
    esp_err_t err = mao_haptics_calibrate();
    const uint32_t start = now_ms();
    do {
        sleep_ms(50);
        mao_haptics_get_cal_info(&after);
    } while (err == ESP_OK && after.runs == before.runs && now_ms() - start < 4000);
    if (err == ESP_OK && after.runs == before.runs) {
        err = ESP_ERR_TIMEOUT;
    } else if (err == ESP_OK) {
        err = after.result;
    }
    snprintf(d, DETAIL_LEN, "%s, comp 0x%02X bemf 0x%02X gain %u, %u Hz", esp_err_to_name(err), after.comp,
             after.bemf, after.bemf_gain, after.resonance_hz);
    const bool f_ok = after.resonance_hz == 0 ||
                      (after.resonance_hz >= LIM_LRA_MIN_HZ && after.resonance_hz <= LIM_LRA_MAX_HZ);
    return err == ESP_OK && f_ok ? R_PASS : R_FAIL;
}

static result_t t_rail_lcd(char *d)
{
    esp_err_t err = mao_display_rail(false);
    sleep_ms(60);
    bool off_level = true, on_level = false;
    const esp_err_t e1 = mao_board_rail_readback(MAO_RAIL_DISPLAY, &off_level);
    const esp_err_t e2 = mao_display_rail(true);     /* panel re-initialised and redrawn */
    const esp_err_t e3 = mao_board_rail_readback(MAO_RAIL_DISPLAY, &on_level);
    err = err != ESP_OK ? err : (e1 != ESP_OK ? e1 : (e2 != ESP_OK ? e2 : e3));
    snprintf(d, DETAIL_LEN, "enable off=%d on=%d (%s)", off_level, on_level, esp_err_to_name(err));
    return err == ESP_OK && !off_level && on_level ? R_PASS : R_FAIL;
}

static result_t t_rail_ir_rx(char *d)
{
    mao_board_ir_t ir;
    mao_board_ir_get(&ir);
    const bool was_on = mao_ir_rx_is_on();
    mao_ir_rx_enable(false);
    sleep_ms(20);
    bool off_rb = true, on_rb = false;
    const esp_err_t e1 = mao_board_rail_readback(MAO_RAIL_IR_RX, &off_rb);
    const int off_out = gpio_get_level(ir.rx_gpio);
    mao_ir_rx_enable(true);
    sleep_ms(ir.rx_settle_ms + 20);
    const esp_err_t e2 = mao_board_rail_readback(MAO_RAIL_IR_RX, &on_rb);
    /* The receiver output is isolated while its supply is off (reads low
     * with no pull) and idles high once it is powered. VERIFY AT BRING-UP:
     * the off level on the A1 (the pin map says "isolated while off"). */
    const int idle = gpio_get_level(ir.rx_gpio);
    const bool idle_ok = ir.rx_active_low ? idle == 1 : idle == 0;
    const bool off_ok = off_out == 0;
    if (!was_on) {
        mao_ir_rx_enable(false);
    }
    snprintf(d, DETAIL_LEN, "supply off=%d on=%d, output off=%d idle=%d", off_rb, on_rb, off_out, idle);
    return e1 == ESP_OK && e2 == ESP_OK && !off_rb && on_rb && off_ok && idle_ok ? R_PASS : R_FAIL;
}

/* One reading at a probe pad: the fixture DMM (factory_test.py --dmm) or
 * the operator answers "mao selftest mv <millivolts>". Returns 1 measured,
 * 2 skipped, 0 no answer. */
static int measure(const char *id, const char *pad, const char *net, bool on, int min_mv, int max_mv, int *mv)
{
    if (s_no_pads) {
        return 2;
    }
    s_answer = 0;
    out_line("SELFTEST MEASURE %s %s %s %s %d %d Measure %s (%s) to GND: rail %s, expect %d..%d mV", id, pad, net,
             on ? "on" : "off", min_mv, max_mv, pad, net, on ? "ON" : "OFF", min_mv, max_mv);
    char small[24];
    snprintf(small, sizeof(small), "%s %s %s", pad, net, on ? "on" : "off");
    screen(0x203040, "MEASURE", small);
    const uint32_t timeout = s_interactive ? 2u * PROMPT_TIMEOUT_MS : MEASURE_AUTO_TIMEOUT_MS;
    const int a = wait_for(NULL, NULL, timeout, false);
    if (a == 3) {
        *mv = s_measured_mv;
        return 1;
    }
    return a == 2 ? 2 : 0;
}

typedef esp_err_t (*rail_fn_t)(bool on);

static esp_err_t rail_lcd(bool on) { return mao_display_rail(on); }
static esp_err_t rail_irv(bool on) { return mao_ir_rx_enable(on); }

static result_t pad_step(char *d, const char *id, const char *pad, const char *net, rail_fn_t rail, int on_max_mv,
                         bool leave_on)
{
    int off_mv = -1, on_mv = -1;
    esp_err_t err = rail(false);
    sleep_ms(150);                   /* RC filters discharge (4.7 uF / 100 R + load) */
    const int r_off = err == ESP_OK ? measure(id, pad, net, false, 0, LIM_PAD_OFF_MAX_MV, &off_mv) : 0;
    const esp_err_t e2 = rail(true);
    sleep_ms(150);
    const int r_on = e2 == ESP_OK && r_off != 2 ? measure(id, pad, net, true, LIM_PAD_ON_MIN_MV, on_max_mv, &on_mv) : 2;
    if (!leave_on) {
        rail(false);
    }
    if (err != ESP_OK || e2 != ESP_OK) {
        snprintf(d, DETAIL_LEN, "%s: rail control failed (%s)", pad, esp_err_to_name(err != ESP_OK ? err : e2));
        return R_FAIL;
    }
    if (r_off == 2 || r_on == 2) {
        snprintf(d, DETAIL_LEN, "%s %s not measured (no DMM / skipped)", pad, net);
        return R_SKIP;
    }
    if (r_off == 0 || r_on == 0) {
        snprintf(d, DETAIL_LEN, "%s %s: no reading", pad, net);
        return R_FAIL;
    }
    const bool ok = off_mv <= LIM_PAD_OFF_MAX_MV && on_mv >= LIM_PAD_ON_MIN_MV && on_mv <= on_max_mv;
    snprintf(d, DETAIL_LEN, "%s %s off %d mV (<= %d), on %d mV (%d..%d)", pad, net, off_mv, LIM_PAD_OFF_MAX_MV, on_mv,
             LIM_PAD_ON_MIN_MV, on_max_mv);
    return ok ? R_PASS : R_FAIL;
}

static result_t t_pad_lcd(char *d)
{
    return pad_step(d, "pad_lcd", PAD_LCD, "LCD", rail_lcd, LIM_PAD_ON_MAX_MV, true);
}

static result_t t_pad_irv(char *d)
{
    const bool was_on = mao_ir_rx_is_on();
    return pad_step(d, "pad_irv", PAD_IRV, "IRV", rail_irv, LIM_PAD_ON_MAX_MV, was_on);
}

static result_t t_speaker(char *d)
{
    /* No microphone on the A1: the chirp is confirmed by ear. */
    if (!s_interactive) {
        snprintf(d, DETAIL_LEN, "no mic for loopback; needs an operator");
        return R_SKIP;
    }
    prompt("speaker", "confirm", "Did you hear a rising chirp? (yes/no, or press the face for yes)",
           "LISTEN", "chirp...");
    mao_audio_test_chirp(CHIRP_LEVEL_PCT);
    sleep_ms(700);
    mao_audio_test_chirp(CHIRP_LEVEL_PCT);
    const int a = wait_for(NULL, NULL, PROMPT_TIMEOUT_MS, true);
    const size_t n = strlen(d);
    snprintf(d + n, DETAIL_LEN - n, "%soperator: %s", n ? "; " : "", a == 1 ? "heard" : (a == 2 ? "skipped" : "not heard"));
    return a == 1 ? R_PASS : (a == 2 ? R_SKIP : R_FAIL);
}

static bool ir_echo_seen(void *ctx)
{
    const uint32_t *echoes0 = ctx;
    mao_ir_stats_t st;
    mao_ir_get_stats(&st);
    return st.echoes != *echoes0 && st.last.command == IR_TEST_COMMAND && st.last.address == IR_TEST_ADDRESS;
}

static result_t t_ir_loopback(char *d)
{
    const bool was_on = mao_ir_rx_is_on();
    mao_ir_rx_enable(true);
    mao_ir_stats_t st;
    mao_ir_get_stats(&st);
    uint32_t echoes0 = st.echoes;
    /* The LEDs face the back edge and the receiver looks up through the
     * window: the frame comes back by reflection (enclosure, fixture lid). */
    for (int i = 0; i < 3; i++) {
        mao_ir_send(IR_TEST_ADDRESS, IR_TEST_COMMAND);
        sleep_ms(150);
        if (ir_echo_seen(&echoes0)) {
            snprintf(d, DETAIL_LEN, "echo received (try %d)", i + 1);
            if (!was_on) {
                mao_ir_rx_enable(false);
            }
            return R_PASS;
        }
    }
    result_t r = R_SKIP;
    if (s_interactive) {
        prompt("ir_loopback", "action", "Hold a white card 2-5 cm over the puck (back edge to 3 o'clock window)",
               "IR", "hold a card over me");
        const uint32_t start = now_ms();
        int a = 0;
        while (a == 0 && now_ms() - start < (uint32_t)PROMPT_TIMEOUT_MS) {
            mao_ir_send(IR_TEST_ADDRESS, IR_TEST_COMMAND);
            a = wait_for(ir_echo_seen, &echoes0, 300, false);
        }
        r = a == 1 ? R_PASS : (a == 2 ? R_SKIP : R_FAIL);
        snprintf(d, DETAIL_LEN, "%s", a == 1 ? "echo received with reflector" : "no echo (TX or RX dead?)");
    } else {
        snprintf(d, DETAIL_LEN, "no echo without a reflector; needs an operator or the fixture lid");
    }
    if (!was_on) {
        mao_ir_rx_enable(false);
    }
    return r;
}

static result_t t_radio(char *d)
{
    uint8_t mac[6];
    mao_radio_get_mac(mac);
    mao_radio_stats_t st;
    const uint32_t start = now_ms();
    do {
        mao_radio_get_stats(&st);
        if (st.tx_frames > 0) {
            break;
        }
        sleep_ms(200);
    } while (now_ms() - start < 12000);   /* discovery broadcasts every 10 s when idle */
    const bool mac_ok = (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0;
    snprintf(d, DETAIL_LEN, "mac %02x:%02x:%02x:%02x:%02x:%02x tx %" PRIu32 " rejected %" PRIu32 " rx %" PRIu32,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], st.tx_frames, st.tx_rejected, st.rx_frames);
    return mac_ok && st.tx_frames > 0 && st.tx_rejected == 0 ? R_PASS : R_FAIL;
}

static result_t t_nvs(char *d)
{
    /* Its own namespace: MAO's settings are never touched. */
    nvs_handle_t h;
    esp_err_t err = nvs_open("mao_st", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        snprintf(d, DETAIL_LEN, "open: %s", esp_err_to_name(err));
        return R_FAIL;
    }
    const uint32_t pattern = 0xA55A0000u | (now_ms() & 0xFFFF);
    uint32_t back = 0;
    err = nvs_set_u32(h, "probe", pattern);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    if (err == ESP_OK) {
        err = nvs_get_u32(h, "probe", &back);
    }
    nvs_erase_key(h, "probe");
    nvs_commit(h);
    nvs_close(h);
    snprintf(d, DETAIL_LEN, "write/read/erase %s", err == ESP_OK && back == pattern ? "ok" : esp_err_to_name(err));
    return err == ESP_OK && back == pattern ? R_PASS : R_FAIL;
}

/* ------------------------------------------------------------------------ */
/* Operator steps                                                           */
/* ------------------------------------------------------------------------ */

static result_t confirm(char *d, const char *id, const char *question)
{
    out_line("SELFTEST PROMPT %s confirm %s (yes/no, or press the face for yes)", id, question);
    s_answer = 0;
    const int a = wait_for(NULL, NULL, PROMPT_TIMEOUT_MS, true);
    snprintf(d, DETAIL_LEN, "operator: %s", a == 1 ? "ok" : (a == 2 ? "skipped" : (a == -1 ? "rejected" : "no answer")));
    return a == 1 ? R_PASS : (a == 2 ? R_SKIP : R_FAIL);
}

static result_t t_lcd(char *d)
{
    static const struct { uint32_t rgb; const char *name; } kFills[] = {
        { 0xFF0000, "RED" }, { 0x00FF00, "GREEN" }, { 0x0000FF, "BLUE" }, { 0xFFFFFF, "WHITE" }, { 0x000000, "BLACK" },
    };
    for (int round = 0; round < 2; round++) {
        for (size_t i = 0; i < sizeof(kFills) / sizeof(kFills[0]); i++) {
            screen(kFills[i].rgb, NULL, NULL);
            sleep_ms(500);
        }
        /* Orientation: the text must read upright, "12" at the USB side. */
        screen(0x101010, "MAO", "12 o'clock = USB");
        if (round == 0) {
            out_line("SELFTEST PROMPT lcd confirm Red, green, blue, white, black shown evenly, no dead pixels or tint, "
                     "and 'MAO' upright? (yes/no, or press the face for yes; watch again: skip)");
            s_answer = 0;
            const int a = wait_for(NULL, NULL, PROMPT_TIMEOUT_MS, true);
            if (a != 2) {
                snprintf(d, DETAIL_LEN, "operator: %s", a == 1 ? "ok" : (a == -1 ? "rejected" : "no answer"));
                return a == 1 ? R_PASS : R_FAIL;
            }
        }
    }
    return confirm(d, "lcd", "Colours even and 'MAO' upright?");
}

static result_t t_backlight(char *d)
{
    screen(0xFFFFFF, NULL, NULL);
    for (int pct = 100; pct >= 0; pct -= 10) {
        mao_display_set_brightness((uint8_t)pct);
        sleep_ms(70);
    }
    sleep_ms(300);
    for (int pct = 0; pct <= 100; pct += 10) {
        mao_display_set_brightness((uint8_t)pct);
        sleep_ms(70);
    }
    mao_display_set_brightness(mao_settings_get()->brightness);
    screen(0x203040, "LIGHT", "dimmed and back?");
    return confirm(d, "backlight", "Did the backlight fade smoothly to dark and back to full?");
}

static result_t t_backlight_sink(char *d)
{
    /* The A1 backlight: LEDC PWM into an analogue current sink. The sleeping
     * screen's 3 % must still glow evenly (no flicker, no dead LED), and the
     * hardware fade must ramp it without steps. VERIFY AT BRING-UP: LED
     * current at 100 % and 3 % (ammeter in the LED feed). */
    screen(0xFFFFFF, NULL, NULL);
    mao_display_set_brightness(BACKLIGHT_REST_PCT);
    sleep_ms(400);
    mao_display_fade_brightness(100, 1500);
    sleep_ms(1700);
    mao_display_fade_brightness(BACKLIGHT_REST_PCT, 1500);
    sleep_ms(1700);
    out_line("SELFTEST PROMPT backlight_sink confirm At the dim rest level the screen glows evenly, and both fades "
             "were smooth (no flicker, no steps)? (yes/no, or press the face for yes)");
    s_answer = 0;
    const int a = wait_for(NULL, NULL, PROMPT_TIMEOUT_MS, true);
    mao_display_set_brightness(mao_settings_get()->brightness);
    snprintf(d, DETAIL_LEN, "operator: %s", a == 1 ? "ok" : (a == 2 ? "skipped" : (a == -1 ? "rejected" : "no answer")));
    return a == 1 ? R_PASS : (a == 2 ? R_SKIP : R_FAIL);
}

typedef struct {
    uint32_t cw0, ccw0, a0, b0;
} dial_ctx_t;

static bool dial_cw(void *ctx)
{
    const dial_ctx_t *c = ctx;
    mao_input_stats_t st;
    mao_input_get_stats(&st);
    return st.detents_cw - c->cw0 >= 3;
}

static bool dial_ccw(void *ctx)
{
    const dial_ctx_t *c = ctx;
    mao_input_stats_t st;
    mao_input_get_stats(&st);
    return st.detents_ccw - c->ccw0 >= 3;
}

static result_t t_dial(char *d)
{
    mao_input_stats_t st;
    mao_input_get_stats(&st);
    dial_ctx_t c = { st.detents_cw, st.detents_ccw, st.edges_a, st.edges_b };
    prompt("dial", "action", "Turn the ring CLOCKWISE (3 detents or more)", "TURN", "clockwise ->");
    int a = wait_for(dial_cw, &c, PROMPT_TIMEOUT_MS, false);
    mao_input_get_stats(&st);
    const uint32_t wrong_cw = st.detents_ccw - c.ccw0;
    if (a != 1) {
        snprintf(d, DETAIL_LEN, "no clockwise detents%s", wrong_cw >= 3 ? " (counted as CCW: direction reversed)" : "");
        return a == 2 ? R_SKIP : R_FAIL;
    }
    c.ccw0 = st.detents_ccw;
    c.cw0 = st.detents_cw;
    prompt("dial", "action", "Turn the ring COUNTER-CLOCKWISE (3 detents or more)", "TURN", "<- counter-clockwise");
    a = wait_for(dial_ccw, &c, PROMPT_TIMEOUT_MS, false);
    mao_input_get_stats(&st);
    const uint32_t edges_a = st.edges_a - c.a0, edges_b = st.edges_b - c.b0;
    snprintf(d, DETAIL_LEN, "cw+ccw ok, Hall A %" PRIu32 " / B %" PRIu32 " edges, %" PRIu32 " invalid", edges_a,
             edges_b, st.invalid_transitions);
    if (a != 1) {
        snprintf(d, DETAIL_LEN, "no counter-clockwise detents");
        return a == 2 ? R_SKIP : R_FAIL;
    }
    return edges_a >= 6 && edges_b >= 6 ? R_PASS : R_FAIL;
}

static bool pressed_since(void *ctx)
{
    return presses() != *(const uint32_t *)ctx;
}

static result_t t_press(char *d)
{
    const uint32_t p0 = presses();
    prompt("press", "action", "Press the face once", "PRESS", "the face");
    const int a = wait_for(pressed_since, (void *)&p0, PROMPT_TIMEOUT_MS, false);
    snprintf(d, DETAIL_LEN, "%s", a == 1 ? "press seen" : "no press");
    sleep_ms(400);   /* let the release pass before the next step */
    return a == 1 ? R_PASS : (a == 2 ? R_SKIP : R_FAIL);
}

static bool hand_near(void *ctx)
{
    (void)ctx;
    mao_obs_t o;
    return mao_sense_latest(MAO_OBS_TOF, &o) && now_ms() - o.time_ms < 600 && o.tof.range_status == 0 &&
           o.tof.distance_mm > 0 && o.tof.distance_mm < LIM_TOF_NEAR_MM;
}

static result_t t_tof_near(char *d)
{
    prompt("tof_near", "action", "Hold a hand about 5 cm above the window at 11 o'clock", "HAND", "5 cm above me");
    const int a = wait_for(hand_near, NULL, PROMPT_TIMEOUT_MS, false);
    mao_obs_t o;
    const bool have = mao_sense_latest(MAO_OBS_TOF, &o);
    snprintf(d, DETAIL_LEN, "%s, last %u mm status %u", a == 1 ? "hand seen" : "no hand within 120 mm",
             have ? o.tof.distance_mm : 0, have ? o.tof.range_status : 0);
    return a == 1 ? R_PASS : (a == 2 ? R_SKIP : R_FAIL);
}

/* Automatic first (no operator needed), then operator steps. */
static const step_t kSteps[] = {
    { "i2c_bus",        t_i2c_bus,        false, has_i2c },
    { "imu_id",         t_imu_id,         false, has_imu },
    { "imu_motion",     t_imu_motion,     false, has_imu },
    { "tof_id",         t_tof_id,         false, has_tof },
    { "tof_ranging",    t_tof_ranging,    false, has_tof },
    { "gauge_id",       t_gauge_id,       false, has_gauge },
    { "gauge_voltage",  t_gauge_voltage,  false, has_gauge },
    { "vbus",           t_vbus,           false, has_charger },
    { "charger_stat",   t_charger_stat,   false, has_charger },
    { "charge_pause",   t_charge_pause,   false, has_ce },
    { "haptic_id",      t_haptic_id,      false, has_haptic },
    { "haptic_cal",     t_haptic_cal,     false, has_haptic },
    { "rail_lcd",       t_rail_lcd,       false, has_lcd_rail },
    { "rail_ir_rx",     t_rail_ir_rx,     false, has_ir },
    { "pad_lcd",        t_pad_lcd,        false, has_lcd_rail },
    { "pad_irv",        t_pad_irv,        false, has_ir },
    { "speaker",        t_speaker,        false, always },
    { "ir_loopback",    t_ir_loopback,    false, has_ir },
    { "radio",          t_radio,          false, always },
    { "nvs",            t_nvs,            false, always },
    { "lcd",            t_lcd,            true,  always },
    { "backlight",      t_backlight,      true,  always },
    { "backlight_sink", t_backlight_sink, true,  has_lcd_rail },
    { "dial",           t_dial,           true,  always },
    { "press",          t_press,          true,  always },
    { "tof_near",       t_tof_near,       true,  has_tof },
};
#define STEP_COUNT ((int)(sizeof(kSteps) / sizeof(kSteps[0])))

/* ------------------------------------------------------------------------ */
/* Runner                                                                   */
/* ------------------------------------------------------------------------ */

static const char *result_name(result_t r)
{
    return r == R_PASS ? "PASS" : (r == R_FAIL ? "FAIL" : "SKIP");
}

/* JSON-safe copy (the details are ours, but never trust a quote). */
static void json_str(char *dst, size_t n, const char *src)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < n; i++) {
        const char c = src[i];
        dst[j++] = (c == '"' || c == '\\' || (unsigned char)c < 0x20) ? '\'' : c;
    }
    dst[j] = '\0';
}

static void report(int total, int passed, int failed, int skipped)
{
    char failed_ids[160] = "", skipped_ids[160] = "";
    int nf = 0, ns = 0;
    for (int i = 0; i < total; i++) {
        if (s_out[i].result == R_FAIL && nf < (int)sizeof(failed_ids) - 16) {
            nf += snprintf(failed_ids + nf, sizeof(failed_ids) - nf, "%s%s", nf ? "," : "", s_out[i].id);
        } else if (s_out[i].result == R_SKIP && ns < (int)sizeof(skipped_ids) - 16) {
            ns += snprintf(skipped_ids + ns, sizeof(skipped_ids) - ns, "%s%s", ns ? "," : "", s_out[i].id);
        }
    }
    const char *verdict = failed ? "FAIL" : (skipped ? "INCOMPLETE" : "PASS");
    if (failed || skipped) {
        out_line("SELFTEST %s %d/%d%s%s%s%s", verdict, passed, total, nf ? " failed=" : "", failed_ids,
                 ns ? " skipped=" : "", skipped_ids);
    } else {
        out_line("SELFTEST PASS %d/%d", passed, total);
    }

    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    const esp_app_desc_t *app = esp_app_get_description();
    int n = snprintf(s_json, sizeof(s_json),
                     "SELFTEST_JSON {\"result\":\"%s\",\"passed\":%d,\"failed\":%d,\"skipped\":%d,\"total\":%d,"
                     "\"board\":\"%s\",\"rev\":\"%s\",\"fw\":\"%s\","
                     "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"interactive\":%s,\"steps\":[",
                     verdict, passed, failed, skipped, total, MAO_BOARD_NAME, mao_board_revision(),
                     app->version, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                     s_interactive ? "true" : "false");
    for (int i = 0; i < total && n < (int)sizeof(s_json) - 160; i++) {
        char detail[DETAIL_LEN];
        json_str(detail, sizeof(detail), s_out[i].detail);
        n += snprintf(s_json + n, sizeof(s_json) - n, "%s{\"id\":\"%s\",\"r\":\"%s\",\"d\":\"%s\"}", i ? "," : "",
                      s_out[i].id, result_name(s_out[i].result), detail);
    }
    if (n < (int)sizeof(s_json) - 4) {
        snprintf(s_json + n, sizeof(s_json) - n, "]}\n");
    }
    fputs(s_json, stdout);     /* one write: the fixture parses it as one line */
    fflush(stdout);

    char small[24];
    snprintf(small, sizeof(small), "%d/%d", passed, total);
    screen(failed ? 0xB00020 : (skipped ? 0xA06000 : 0x00A040), verdict, small);
}

static void selftest_task(void *arg)
{
    (void)arg;
    mao_board_get_caps(&s_caps);
    int total = 0;
    for (int i = 0; i < STEP_COUNT; i++) {
        total += kSteps[i].applies() ? 1 : 0;
    }
    out_line("SELFTEST BEGIN board=\"%s\" rev=%s steps=%d mode=%s", MAO_BOARD_NAME, mao_board_revision(), total,
             s_interactive ? "interactive" : "auto");
    int idx = 0, passed = 0, failed = 0, skipped = 0;
    for (int i = 0; i < STEP_COUNT && idx < MAX_STEPS; i++) {
        const step_t *st = &kSteps[i];
        if (!st->applies()) {
            continue;
        }
        outcome_t *o = &s_out[idx++];
        o->id = st->id;
        o->detail[0] = '\0';
        if (s_abort) {
            o->result = R_SKIP;
            snprintf(o->detail, DETAIL_LEN, "aborted");
        } else if (st->interactive && !s_interactive) {
            o->result = R_SKIP;
            snprintf(o->detail, DETAIL_LEN, "needs an operator");
        } else {
            char small[24];
            snprintf(small, sizeof(small), "%d/%d", idx, total);
            if (!st->interactive) {
                screen(0x101418, "TEST", small);
            }
            o->result = st->run(o->detail);
        }
        passed += o->result == R_PASS;
        failed += o->result == R_FAIL;
        skipped += o->result == R_SKIP;
        out_line("SELFTEST STEP %d/%d %s %s %s", idx, total, st->id, result_name(o->result), o->detail);
    }
    report(idx, passed, failed, skipped);
    s_running = false;
#if !CONFIG_MAO_SELFTEST_AT_BOOT
    /* Development use: give the face back after a while. Factory builds
     * keep the verdict on screen. */
    sleep_ms(RESULT_HOLD_MS);
    if (!s_running) {
        mao_display_test_clear();
    }
#endif
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t mao_selftest_start(bool interactive)
{
#if CONFIG_MAO_BOARD_LCDKIT
    /* Nothing of this on the LCDkit, and none of its code or RAM in the
     * image (the rest of this function is dead there and dropped). */
    return ESP_ERR_NOT_SUPPORTED;
#endif
    ESP_RETURN_ON_FALSE(!s_running && !s_task, ESP_ERR_INVALID_STATE, TAG, "self-test already running");
    s_running = true;
    s_interactive = interactive;
    s_abort = false;
    s_answer = 0;
    if (xTaskCreate(selftest_task, "mao_selftest", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool mao_selftest_running(void)
{
    return s_running;
}

/* ------------------------------------------------------------------------ */
/* Boot check                                                               */
/* ------------------------------------------------------------------------ */

const char *mao_fault_name(uint32_t bit)
{
    static const char *const kNames[MAO_FAULT_COUNT] = {
        "display", "input", "imu", "proximity", "fuel_gauge", "haptic", "ir", "audio", "radio", "nvs",
        "battery", "charger",
    };
    for (int i = 0; i < MAO_FAULT_COUNT; i++) {
        if (bit == (1u << i)) {
            return kNames[i];
        }
    }
    return "?";
}

/* A fitted subsystem whose bring-up report was not OK. */
static uint32_t check(bool fitted, const char *report_name, uint32_t bit)
{
    if (!fitted) {
        return 0;
    }
    const esp_err_t err = mao_system_report_status(report_name);
    return err == ESP_OK ? 0 : bit;
}

static void boot_check(void)
{
    mao_board_get_caps(&s_caps);
    if (!s_caps.i2c && !s_caps.power_status) {
        /* No MAO hardware (the LCDkit): no boot check, nothing in NVS. */
        return;
    }
    uint32_t f = 0;
    f |= check(true, "display", MAO_FAULT_DISPLAY);
    f |= check(true, "input", MAO_FAULT_INPUT);
    f |= check(true, "audio", MAO_FAULT_AUDIO);
    f |= check(s_caps.imu, "IMU", MAO_FAULT_IMU);
    f |= check(s_caps.tof, "proximity", MAO_FAULT_TOF);
    f |= check(s_caps.haptic, "haptics", MAO_FAULT_HAPTIC);
    f |= check(s_caps.fuel_gauge, "fuel gauge", MAO_FAULT_GAUGE);
    f |= check(s_caps.power_status, "battery", MAO_FAULT_BATTERY);
    f |= check(has_ir(), "IR", MAO_FAULT_IR);
    f |= check(true, "devices", MAO_FAULT_RADIO);
    f |= check(true, "settings", MAO_FAULT_NVS);
    mao_charger_status_t cs;
    if (s_caps.power_status && mao_board_charger_status(&cs) == ESP_OK && cs == MAO_CHARGER_FAULT_LATCHED) {
        f |= MAO_FAULT_CHARGER;
    }
    s_boot.faults = f;
    s_boot.fatal = (f & MAO_FAULT_FATAL) != 0;

    /* Service code: the lowest fault bit number, 2 digits. */
    int first = -1;
    for (int i = 0; i < MAO_FAULT_COUNT && first < 0; i++) {
        if (f & (1u << i)) {
            first = i;
        }
    }
    snprintf(s_boot.code, sizeof(s_boot.code), "SERVICE %02d", first < 0 ? 0 : first + 1);

    const mao_settings_t *cfg = mao_settings_get();
    const char *rev = mao_board_revision();
    s_boot.first_boot = strcmp(cfg->check_revision, rev) != 0;

    char names[160] = "";
    int n = 0;
    for (int i = 0; i < MAO_FAULT_COUNT && n < (int)sizeof(names) - 16; i++) {
        if (f & (1u << i)) {
            n += snprintf(names + n, sizeof(names) - n, "%s%s", n ? "," : "", mao_fault_name(1u << i));
        }
    }
    if (s_boot.first_boot) {
        /* First boot of this board (or a different revision): say so in a
         * line a production log can grep. */
        out_line("BOOTCHECK first board=\"%s\" rev=%s faults=0x%04" PRIX32 " %s", MAO_BOARD_NAME, rev,
                 f, f ? names : "none");
    }
    if (s_boot.fatal) {
        ESP_LOGE(TAG, "[!!] hardware: FATAL fault(s) 0x%04" PRIx32 " (%s): MAO cannot run as a character; %s",
                 f, names, s_boot.code);
    } else if (f) {
        ESP_LOGW(TAG, "[!!] hardware: fault(s) 0x%04" PRIx32 " (%s); MAO runs without them", f, names);
    } else {
        ESP_LOGI(TAG, "[OK] hardware check: board %s rev %s, every fitted device answered", MAO_BOARD_NAME, rev);
    }
    if (s_boot.first_boot || cfg->hw_faults != f) {
        mao_settings_set_hw_record(rev, f);   /* kept for diagnosis (NVS) */
    }
}

const mao_boot_check_t *mao_selftest_boot_check(void)
{
    return &s_boot;
}

/* ------------------------------------------------------------------------ */
/* Console, boot start                                                      */
/* ------------------------------------------------------------------------ */

#if CONFIG_MAO_DEV_CONSOLE

static void devcmd_selftest(char *arg)
{
    const char *args = arg ? arg : "";
    if (strcmp(args, "yes") == 0 || strcmp(args, "y") == 0) {
        s_answer = 1;
    } else if (strcmp(args, "no") == 0 || strcmp(args, "n") == 0) {
        s_answer = -1;
    } else if (strcmp(args, "skip") == 0) {
        s_answer = 2;
    } else if (strcmp(args, "abort") == 0) {
        s_abort = true;
    } else if (strcmp(args, "clear") == 0) {
        mao_display_test_clear();
    } else if (strcmp(args, "boot") == 0) {
        const mao_settings_t *cfg = mao_settings_get();
        out_line("BOOTCHECK board=\"%s\" rev=%s faults=0x%04" PRIX32 " fatal=%d stored_rev=%s",
                 MAO_BOARD_NAME, mao_board_revision(), s_boot.faults, s_boot.fatal,
                 cfg->check_revision[0] ? cfg->check_revision : "-");
        for (int i = 0; i < MAO_FAULT_COUNT; i++) {
            if (s_boot.faults & (1u << i)) {
                out_line("BOOTCHECK fault %s", mao_fault_name(1u << i));
            }
        }
    } else if (strncmp(args, "mv ", 3) == 0) {
        s_measured_mv = atoi(args + 3);    /* a probe-pad reading, millivolts */
        s_answer = 3;
    } else if (args[0] == '\0' || strncmp(args, "run", 3) == 0 || strncmp(args, "auto", 4) == 0) {
        if (!s_running) {
            s_no_pads = strstr(args, "nopads") != NULL;   /* no DMM, no operator for the probe pads */
        }
        const esp_err_t err = mao_selftest_start(strncmp(args, "auto", 4) != 0);
        if (err != ESP_OK) {
            out_line("SELFTEST ERROR %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "dev: selftest [run|auto] [nopads] | yes|no|skip|mv <mV>|abort|clear|boot");
    }
}

static void register_devcmds(void)
{
    mao_devcmd_register("selftest", devcmd_selftest);   /* [run|auto] [nopads] | yes|no|skip|mv <mV>|abort|clear|boot */
}

#else

static void register_devcmds(void)
{
}

#endif

#if CONFIG_MAO_SELFTEST_AT_BOOT
#define AT_BOOT_DELAY_US    (2 * 1000 * 1000)
#if CONFIG_MAO_SELFTEST_AT_BOOT_INTERACTIVE
#define AT_BOOT_INTERACTIVE true
#else
#define AT_BOOT_INTERACTIVE false
#endif

static esp_timer_handle_t s_boot_timer;

static void boot_timer_cb(void *arg)
{
    (void)arg;
    mao_selftest_start(AT_BOOT_INTERACTIVE);
}

static void on_event(const mao_event_t *ev, void *ctx)
{
    (void)ctx;
    if (ev->type == MAO_EVENT_SYSTEM_READY && s_boot_timer) {
        /* Factory build: let the boot animation finish, then test. */
        out_line("SELFTEST AT BOOT in 2 s");
        esp_timer_start_once(s_boot_timer, AT_BOOT_DELAY_US);
    }
}
#endif

esp_err_t mao_selftest_init(void)
{
#if CONFIG_MAO_BOARD_LCDKIT
    /* Nothing of this on the LCDkit, and none of its code or RAM in the
     * image (the rest of this function is dead there and dropped). */
    return ESP_ERR_NOT_SUPPORTED;
#endif
    boot_check();
    register_devcmds();
#if CONFIG_MAO_SELFTEST_AT_BOOT
    const esp_timer_create_args_t targs = { .callback = boot_timer_cb, .name = "mao_selftest" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_boot_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(mao_event_subscribe(on_event, NULL), TAG, "subscribe");
    ESP_LOGW(TAG, "factory build: self-test runs at boot");
#endif
    return ESP_OK;
}
