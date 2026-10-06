/*
 * MAO battery: see mao_battery.h.
 *
 * One mutex (s_lock) guards the status and the housekeeping, so the battery
 * task and mao_battery_poll() (the light-sleep loop in mao_app's power task)
 * can both run it. Events are posted, never waited for.
 */
#include "mao_battery.h"
#include "mao_battery_policy.h"
#include "mao_battery_priv.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "mao_board.h"
#include "mao_events.h"
#include "mao_system.h"

static const char *TAG = "MAO_BATTERY";

#define TASK_STACK              4096
#define TASK_PRIO               3

#define GAUGE_PERIOD_MS         30000      /* SOC moves slowly */
#define GAUGE_PERIOD_LOW_MS     5000       /* while the battery is low or critical */
#define LOW_PCT                 15.0f      /* BATTERY_LOW at or below */
#define LOW_CLEAR_PCT           20.0f      /* re-armed above (hysteresis) */
#define CRITICAL_PCT            2.0f
#define CRITICAL_READINGS       3          /* consecutive readings before acting */
#define CHARGE_LIMIT_PERIOD_MS  10000      /* board temperature check while on USB */

#define NOTIFY_USB              (1u << 0)
#define NOTIFY_POLL             (1u << 1)

static TaskHandle_t s_task;
static SemaphoreHandle_t s_lock;
static mao_battery_status_t s_status;
static bool s_gauge_ok;
static bool s_chg_lines;                 /* the board has charger status lines */
static mao_board_caps_t s_caps;
static volatile mao_battery_temp_source_t s_temp_source;

static bool s_low_reported;
static int s_critical_count;
static bool s_critical_reported;
static uint32_t s_counted_reading_ms;    /* the gauge reading the critical count last counted */
static bool s_charge_paused;
static bool s_charge_hold;               /* console: charging held off by hand (bring-up check of /CE) */
static int64_t s_next_charge_check_us;
static int64_t s_next_gauge_us;
static bool s_temp_fail_logged;
static mao_charger_status_t s_chg_state = MAO_CHARGER_IDLE;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool mao_battery_available(void)
{
    return s_lock != NULL;
}

void mao_battery_get_status(mao_battery_status_t *out)
{
    if (!out) {
        return;
    }
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

bool mao_battery_usb_present(void)
{
    return s_lock && s_status.usb_present;
}

void mao_battery_set_temp_source(mao_battery_temp_source_t source)
{
    s_temp_source = source;
}

/* ------------------------------------------------------------------------ */
/* Housekeeping (s_lock held)                                               */
/* ------------------------------------------------------------------------ */

static void update_gauge(void)
{
    if (!s_gauge_ok) {
        return;
    }
    mao_gauge_reading_t r;
    if (mao_battery_gauge_read(&r) != ESP_OK) {
        return;
    }
    s_status.voltage_mv = r.voltage_mv;
    s_status.soc_pct = r.soc_pct;
    s_status.rate_pct_per_h = r.rate_pct_per_h;
    s_status.updated_ms = now_ms();
    /* ALRT is not wired: take (and clear) the alert flags with the reading. */
    uint16_t flags = 0;
    if (mao_battery_gauge_take_alert(&flags) == ESP_OK && (flags & (MAO_GAUGE_ST_HD | MAO_GAUGE_ST_VL))) {
        ESP_LOGI(TAG, "gauge alert 0x%04x%s%s", flags, (flags & MAO_GAUGE_ST_HD) ? " SOC-low" : "",
                 (flags & MAO_GAUGE_ST_VL) ? " V-low" : "");
    }
}

static void update_charging(void)
{
    bool chg = s_status.charging;
    bool fault = false;
    if (s_chg_lines) {
        mao_charger_status_t st;
        if (mao_board_charger_status(&st) != ESP_OK) {
            return;
        }
        if (st != s_chg_state && (st == MAO_CHARGER_FAULT || st == MAO_CHARGER_FAULT_LATCHED)) {
            ESP_LOGW(TAG, "charger: %s", mao_board_charger_status_name(st));
        }
        s_chg_state = st;
        chg = st == MAO_CHARGER_CHARGING;
        fault = st == MAO_CHARGER_FAULT || st == MAO_CHARGER_FAULT_LATCHED;
    } else {
        const mao_charge_input_t in = {
            .usb_present = s_status.usb_present,
            .charge_paused = s_charge_paused,
            .gauge_valid = s_gauge_ok && s_status.updated_ms != 0,
            .soc_pct = s_status.soc_pct,
            .rate_pct_per_h = s_status.rate_pct_per_h,
        };
        chg = mao_battery_charging_estimate(&in);
    }
    s_status.charger_fault = fault;
    if (chg == s_status.charging) {
        return;
    }
    s_status.charging = chg;
    const int32_t soc = (int32_t)s_status.soc_pct;
    if (chg) {
        ESP_LOGI(TAG, "charging started");
        mao_event_post(MAO_EVENT_CHARGING_STARTED, soc);
    } else if (s_status.usb_present && !s_charge_paused && !fault) {
        /* Stopped with USB still present, nothing holding it off, no
         * fault: charge complete. */
        ESP_LOGI(TAG, "charging done");
        mao_event_post(MAO_EVENT_CHARGING_DONE, soc);
    }
}

/* Charge temperature limit. Runs every CHARGE_LIMIT_PERIOD_MS while USB is
 * present, and at once (force) when USB comes or goes. */
static void charge_limit(bool force)
{
    if (!s_caps.charge_control) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (!force && now < s_next_charge_check_us) {
        return;
    }
    s_next_charge_check_us = now + (int64_t)CHARGE_LIMIT_PERIOD_MS * 1000;

    const bool usb = s_status.usb_present;
    float temp = 0.0f;
    bool valid = false;
    if (usb) {
        const mao_battery_temp_source_t source = s_temp_source;
        const esp_err_t err = source ? source(&temp) : ESP_ERR_NOT_SUPPORTED;
        valid = err == ESP_OK && temp == temp;
        if (!valid && !s_temp_fail_logged) {
            ESP_LOGW(TAG, "charge limit: no board temperature (%s); charging stays enabled "
                     "(the charger's TS input still protects the cell)", esp_err_to_name(err));
        }
        s_temp_fail_logged = !valid;
    }

    const bool pause = s_charge_hold || mao_battery_charge_pause(usb, s_charge_paused, valid, temp);
    if (pause != s_charge_paused) {
        const esp_err_t err = mao_board_charge_enable(!pause);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "charge limit: could not %s the charger (%s), retrying", pause ? "pause" : "enable",
                     esp_err_to_name(err));
        } else if (pause && s_charge_hold) {
            s_charge_paused = true;
            ESP_LOGW(TAG, "charging held off from the console (/CE high)");
        } else if (pause) {
            s_charge_paused = true;
            ESP_LOGW(TAG, "charging paused: board %.1f C >= %.0f C (cell limit 45 C)", (double)temp,
                     (double)MAO_CHARGE_PAUSE_C);
        } else {
            s_charge_paused = false;
            if (valid) {
                ESP_LOGI(TAG, "charging resumed: board %.1f C <= %.0f C", (double)temp, (double)MAO_CHARGE_RESUME_C);
            } else {
                ESP_LOGI(TAG, "charging enabled again (%s)", usb ? "no board temperature" : "USB removed");
            }
        }
    }
    s_status.charge_paused = s_charge_paused;
    s_status.temp_valid = valid;
    if (valid) {
        s_status.temp_c = temp;
    }
}

/* USB line: post an event for every change. */
static void check_usb(void)
{
    bool usb = s_status.usb_present;
    mao_board_line_get(MAO_LINE_USB_PRESENT, &usb);
    if (usb == s_status.usb_present) {
        return;
    }
    s_status.usb_present = usb;
    ESP_LOGI(TAG, "USB %s", usb ? "connected" : "disconnected");
    mao_event_post(usb ? MAO_EVENT_USB_CONNECTED : MAO_EVENT_USB_DISCONNECTED, 0);
    update_gauge();          /* fresh SOC / rate */
    charge_limit(true);      /* starts (or stops) watching the temperature now */
}

static void battery_policy(void)
{
    if (!s_gauge_ok || s_status.updated_ms == 0) {
        return;
    }
    const float soc = s_status.soc_pct;
    const bool on_battery = !s_status.usb_present;

    if (!s_low_reported && soc <= LOW_PCT) {
        s_low_reported = true;
        ESP_LOGW(TAG, "battery low: %.1f %%, %u mV", (double)soc, s_status.voltage_mv);
        mao_event_post(MAO_EVENT_BATTERY_LOW, (int32_t)soc);
    } else if (s_low_reported && soc > LOW_CLEAR_PCT) {
        s_low_reported = false;
    }

    /* Count consecutive gauge readings, not policy runs. */
    if (s_status.updated_ms != s_counted_reading_ms) {
        s_counted_reading_ms = s_status.updated_ms;
        const bool critical = mao_battery_critical_reading(!on_battery, s_status.voltage_mv, soc,
                                                           CONFIG_MAO_BATTERY_CRITICAL_MV, CRITICAL_PCT);
        s_critical_count = critical ? s_critical_count + 1 : 0;
    }
    if (!on_battery) {
        s_critical_count = 0;
        s_critical_reported = false;
    }
    s_status.low = s_low_reported;
    s_status.critical = s_critical_count >= CRITICAL_READINGS;

    if (s_status.critical && !s_critical_reported) {
        s_critical_reported = true;
        ESP_LOGE(TAG, "battery critical: %.1f %%, %u mV (floor %d mV)", (double)soc, s_status.voltage_mv,
                 CONFIG_MAO_BATTERY_CRITICAL_MV);
        mao_event_post(MAO_EVENT_BATTERY_CRITICAL, (int32_t)soc);
    }
}

static void housekeeping(bool usb_changed)
{
    if (usb_changed) {
        check_usb();
    }
    const int64_t now = esp_timer_get_time();
    if (now >= s_next_gauge_us) {
        update_gauge();
        const bool watch = s_low_reported || s_critical_count > 0 || s_critical_reported;
        s_next_gauge_us = now + (int64_t)(watch ? GAUGE_PERIOD_LOW_MS : GAUGE_PERIOD_MS) * 1000;
    }
    charge_limit(false);
    update_charging();
    battery_policy();
}

/* ------------------------------------------------------------------------ */
/* Task                                                                     */
/* ------------------------------------------------------------------------ */

static void usb_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(s_task, NOTIFY_USB, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

static void battery_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int64_t next_us = s_next_gauge_us;
        if (s_status.usb_present && s_caps.charge_control && s_next_charge_check_us < next_us) {
            next_us = s_next_charge_check_us;
        }
        xSemaphoreGive(s_lock);
        int64_t wait_us = next_us - esp_timer_get_time();
        wait_us = wait_us < 0 ? 0 : wait_us;

        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(wait_us / 1000) + 1);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        /* Every pass reads the USB line: an edge missed in light sleep (the
         * GPIO wake source owns the pad then) is caught here. */
        housekeeping(true);
        xSemaphoreGive(s_lock);
    }
}

void mao_battery_poll(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return;
    }
    housekeeping(true);
    xSemaphoreGive(s_lock);
}

esp_err_t mao_battery_refresh(void)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NOT_SUPPORTED, TAG, "battery monitor not running");
    ESP_RETURN_ON_FALSE(s_gauge_ok, ESP_ERR_NOT_FOUND, TAG, "no fuel gauge");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    update_gauge();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t mao_battery_gauge_version_get(uint16_t *version)
{
    ESP_RETURN_ON_FALSE(s_gauge_ok, ESP_ERR_NOT_FOUND, TAG, "no fuel gauge");
    return mao_battery_gauge_version(version);
}

/* ------------------------------------------------------------------------ */
/* Development console                                                      */
/* ------------------------------------------------------------------------ */

#if CONFIG_MAO_DEV_CONSOLE

/* "battery"                 status
 * "battery charge off|on"   hold /CE high by hand, or release it to the limit */
static void devcmd_battery(char *arg)
{
    if (arg && strncmp(arg, "charge", 6) == 0) {
        const char *v = arg + 6;
        while (*v == ' ') {
            v++;
        }
        if (!s_caps.charge_control || (strcmp(v, "off") != 0 && strcmp(v, "on") != 0)) {
            ESP_LOGW(TAG, "battery charge off | on%s", s_caps.charge_control ? "" : ": no charge control on this board");
            return;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_charge_hold = strcmp(v, "off") == 0;
        charge_limit(true);
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "charging %s", s_charge_hold ? "held off (the temperature limit still applies when released)"
                                                   : "released to the temperature limit");
        return;
    }
    mao_battery_refresh();
    mao_battery_status_t st;
    mao_battery_get_status(&st);
    if (st.gauge) {
        ESP_LOGI(TAG, "battery: %u mV, %.1f %%, %+.1f %%/h%s%s (critical floor %d mV)", st.voltage_mv,
                 (double)st.soc_pct, (double)st.rate_pct_per_h, st.low ? " LOW" : "", st.critical ? " CRITICAL" : "",
                 CONFIG_MAO_BATTERY_CRITICAL_MV);
    } else {
        ESP_LOGI(TAG, "battery: no fuel gauge");
    }
    ESP_LOGI(TAG, "charger: usb=%d charging=%d (%s)%s", st.usb_present, st.charging,
             s_chg_lines ? mao_board_charger_status_name(s_chg_state) : "estimated from the gauge",
             st.charge_paused ? (s_charge_hold ? ", PAUSED (console hold)" : ", PAUSED (temperature)") : "");
    if (s_caps.charge_control) {
        if (st.temp_valid) {
            ESP_LOGI(TAG, "charge limit: board %.1f C (pause >= %.0f C, resume <= %.0f C)", (double)st.temp_c,
                     (double)MAO_CHARGE_PAUSE_C, (double)MAO_CHARGE_RESUME_C);
        } else {
            ESP_LOGI(TAG, "charge limit: %s", st.usb_present ? "no board temperature" : "idle (no USB)");
        }
    }
}

static void register_devcmds(void)
{
    mao_devcmd_register("battery", devcmd_battery);
}

#else

static void register_devcmds(void)
{
}

#endif

/* ------------------------------------------------------------------------ */

esp_err_t mao_battery_init(void)
{
    mao_board_get_caps(&s_caps);
    if (!s_caps.power_status) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "alloc");

    /* A missing gauge is reported but not fatal: USB, charger and the
     * charge limit work without it. */
    s_gauge_ok = mao_system_report("fuel gauge", mao_battery_gauge_init()) == ESP_OK;
    s_status.gauge = s_gauge_ok;
    mao_board_line_get(MAO_LINE_USB_PRESENT, &s_status.usb_present);
    mao_charger_status_t st;
    s_chg_lines = mao_board_charger_status(&st) == ESP_OK;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    update_gauge();
    s_next_gauge_us = esp_timer_get_time() + (int64_t)GAUGE_PERIOD_MS * 1000;
    update_charging();
    xSemaphoreGive(s_lock);
    /* The board came up with the charger enabled. The first temperature
     * check waits one period, so the source (mao_sense, started after this)
     * is registered by then. */
    s_next_charge_check_us = esp_timer_get_time() + (int64_t)CHARGE_LIMIT_PERIOD_MS * 1000;

    if (xTaskCreate(battery_task, "mao_battery", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "isr service");
    mao_board_irq_desc_t d;
    ESP_RETURN_ON_ERROR(mao_board_irq_get(MAO_IRQ_USB_PRESENT, &d), TAG, "usb irq");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(d.gpio, GPIO_INTR_ANYEDGE), TAG, "usb intr");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(d.gpio, usb_isr, NULL), TAG, "usb isr");
    register_devcmds();

    ESP_LOGI(TAG, "battery: %u mV %.1f %%, usb=%d charging=%d%s; critical floor %d mV (VERIFY AT BRING-UP)",
             s_status.voltage_mv, (double)s_status.soc_pct, s_status.usb_present, s_status.charging,
             s_chg_lines ? "" : " (estimated)", CONFIG_MAO_BATTERY_CRITICAL_MV);
    return ESP_OK;
}
