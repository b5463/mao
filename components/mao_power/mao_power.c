/*
 * MAO power manager: one task ("mao_power") owns the gauge, the charger and
 * USB lines, the battery policy and every power-state transition.
 *
 * Lines: USB PGOOD (GPIO, both edges) and the expander INT (the gauge/light
 * alert, falling edge) only notify the task; all I2C work happens in task
 * context. The gauge is also polled (it changes slowly).
 *
 * Charging: from the board's charger status line where it has one; on the
 * A0 (no /CHG line) estimated from USB present and the gauge
 * (mao_policy_charging). Charge temperature limit (boards with
 * caps.charge_control): while USB is present the registered board
 * temperature is read every CHARGE_LIMIT_PERIOD_MS and the charger paused /
 * resumed through mao_board_charge_enable() (mao_policy_charge_pause:
 * >= 43 C pause, <= 40 C resume). A missing or failing temperature leaves
 * charging enabled; the charger's pack NTC still stops it outside 0-50 C.
 *
 * Transitions run the registered hooks in this task, so a hook never races
 * another transition. DROWSY is a light-sleep loop inside this task: wake
 * lines, touch or proximity end it (back to ACTIVE); the housekeeping timer
 * only refreshes the battery state and sleeps again.
 */
#include "mao_power.h"
#include "mao_power_policy.h"
#include "mao_power_priv.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rtc_time.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "mao_board.h"
#include "mao_events.h"
#include "mao_system.h"

static const char *TAG = "MAO_POWER";

#define TASK_STACK              4096
#define TASK_PRIO               3
#define REQUEST_QUEUE_LEN       4
#define MAX_HOOKS               8

#define GAUGE_PERIOD_MS         30000      /* SOC moves slowly; alerts arrive by interrupt */
#define GAUGE_PERIOD_LOW_MS     5000       /* while the battery is low or critical */
#define LOW_PCT                 15.0f      /* BATTERY_LOW at or below */
#define LOW_CLEAR_PCT           20.0f      /* re-armed above (hysteresis) */
/* Deep sleep well before the 3V3 buck-boost UVLO (~2.96 V VSYS). */
#define CRITICAL_PCT            2.0f
#define CRITICAL_MV             3350
#define CRITICAL_READINGS       3          /* consecutive readings before acting */
#define CRITICAL_GRACE_MS       5000       /* time for the app to save state */
#define DROWSY_HOUSEKEEPING_S   60
#define CHARGE_LIMIT_PERIOD_MS  10000      /* board temperature check while on USB */
#define LOG_FLUSH_MS            50         /* let the console drain before sleeping */

#define NOTIFY_USB              (1u << 0)
#define NOTIFY_EXPANDER         (1u << 1)
#define NOTIFY_REQUEST          (1u << 2)

#define RTC_MAGIC               0x4D414F31u   /* "MAO1" */

#if CONFIG_MAO_POWER_CRITICAL_SHUTDOWN
#define CRITICAL_NOTE           ", critical-battery shutdown on"
#else
#define CRITICAL_NOTE           ", critical-battery shutdown OFF"
#endif

typedef enum { REQ_STATE = 0, REQ_DEEP_SLEEP, REQ_DROWSY_FOR, REQ_CHARGE_HOLD } request_kind_t;

typedef struct {
    request_kind_t kind;
    mao_power_state_t state;
    uint32_t seconds;
    mao_sleep_reason_t reason;
} request_t;

typedef struct {
    const char *name;
    mao_power_hook_t fn;
    void *ctx;
} hook_t;

/* Survives deep sleep (RTC slow memory); reinitialised on any other boot. */
typedef struct {
    uint32_t magic;
    uint32_t sleep_count;
    uint64_t sleep_start_us;      /* RTC clock at sleep entry */
    uint32_t reason;
} rtc_record_t;

static RTC_DATA_ATTR rtc_record_t s_rtc;

static hook_t s_hooks[MAX_HOOKS];
static size_t s_hook_count;
static TaskHandle_t s_task;
static QueueHandle_t s_requests;
static SemaphoreHandle_t s_lock;
static mao_power_status_t s_status;
static volatile mao_power_state_t s_state = MAO_POWER_ACTIVE;
static mao_power_continuity_t s_cont;
static volatile mao_wake_source_t s_last_wake = MAO_WAKE_NONE;
static bool s_gauge_ok;
static mao_board_caps_t s_caps;
static bool s_chg_line;                  /* the board has a charger status line */
static volatile mao_power_temp_source_t s_temp_source;

static void battery_policy(void);

/* Battery policy state (task only). */
static bool s_low_reported;
static int s_critical_count;
static bool s_critical_reported;
static uint32_t s_policy_reading_ms;     /* gauge reading the policy last counted */
static int64_t s_critical_deadline_us;

/* Charge limit state (task only). */
static bool s_charge_paused;
static bool s_charge_hold;               /* dev console: charging held off by hand (bring-up check of /CE) */
static int64_t s_next_charge_check_us;
static bool s_temp_fail_logged;

/* ------------------------------------------------------------------------ */
/* Names                                                                    */
/* ------------------------------------------------------------------------ */

const char *mao_power_state_name(mao_power_state_t state)
{
    switch (state) {
    case MAO_POWER_ACTIVE:     return "ACTIVE";
    case MAO_POWER_IDLE:       return "IDLE";
    case MAO_POWER_DROWSY:     return "DROWSY";
    case MAO_POWER_DEEP_SLEEP: return "DEEP_SLEEP";
    default:                   return "?";
    }
}

const char *mao_wake_source_name(mao_wake_source_t source)
{
    switch (source) {
    case MAO_WAKE_NONE:      return "none";
    case MAO_WAKE_PRESS:     return "press";
    case MAO_WAKE_MOTION:    return "motion";
    case MAO_WAKE_USB:       return "usb";
    case MAO_WAKE_EXPANDER:  return "charger/alert";
    case MAO_WAKE_TOUCH:     return "touch";
    case MAO_WAKE_PROXIMITY: return "proximity";
    case MAO_WAKE_TIMER:     return "timer";
    default:                 return "other";
    }
}

static const char *reason_name(mao_sleep_reason_t reason)
{
    switch (reason) {
    case MAO_SLEEP_REASON_REQUEST: return "request";
    case MAO_SLEEP_REASON_DEV:     return "dev";
    case MAO_SLEEP_REASON_BATTERY: return "battery";
    case MAO_SLEEP_REASON_IDLE:    return "idle";
    default:                       return "none";
    }
}

/* What woke the chip, from the sleep causes and the board's line decode. */
static mao_wake_source_t classify_wake(void)
{
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    mao_board_wake_t lines;
    mao_board_wake_decode(&lines);
    if (lines.press) {
        return MAO_WAKE_PRESS;
    }
    if (lines.usb) {
        return MAO_WAKE_USB;
    }
    if (lines.motion) {
        return MAO_WAKE_MOTION;
    }
    if (lines.proximity) {
        return MAO_WAKE_PROXIMITY;
    }
    if (lines.expander) {
        return MAO_WAKE_EXPANDER;
    }
    if (causes & BIT(ESP_SLEEP_WAKEUP_TOUCHPAD)) {
        return MAO_WAKE_TOUCH;
    }
    if (causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        return MAO_WAKE_TIMER;
    }
    return causes ? MAO_WAKE_OTHER : MAO_WAKE_NONE;
}

/* ------------------------------------------------------------------------ */
/* Status                                                                   */
/* ------------------------------------------------------------------------ */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void mao_power_get_status(mao_power_status_t *out)
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

mao_power_state_t mao_power_get_state(void)
{
    return s_state;
}

const mao_power_continuity_t *mao_power_continuity(void)
{
    return &s_cont;
}

void mao_power_set_temp_source(mao_power_temp_source_t source)
{
    s_temp_source = source;
}

static void update_gauge(void)
{
    if (!s_gauge_ok) {
        return;
    }
    mao_gauge_reading_t r;
    if (mao_gauge_read(&r) != ESP_OK) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.voltage_mv = r.voltage_mv;
    s_status.soc_pct = r.soc_pct;
    s_status.rate_pct_per_h = r.rate_pct_per_h;
    s_status.updated_ms = now_ms();
    xSemaphoreGive(s_lock);
}

/* The charging state from the board line or the estimate; false when a
 * board line could not be read (keep the previous state). */
static bool charging_now(bool *out)
{
    if (s_chg_line) {
        return mao_board_line_get(MAO_LINE_CHARGING, out) == ESP_OK;
    }
    const mao_charge_input_t in = {
        .usb_present = s_status.usb_present,
        .charge_paused = s_charge_paused,
        .gauge_valid = s_gauge_ok && s_status.updated_ms != 0,
        .soc_pct = s_status.soc_pct,
        .rate_pct_per_h = s_status.rate_pct_per_h,
    };
    *out = mao_policy_charging(&in);
    return true;
}

/* Charging state: post an event for every change. */
static void update_charging(void)
{
    bool chg = false;
    if (!charging_now(&chg) || chg == s_status.charging) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.charging = chg;
    xSemaphoreGive(s_lock);
    const int32_t soc = (int32_t)s_status.soc_pct;
    if (chg) {
        ESP_LOGI(TAG, "charging started");
        mao_event_post(MAO_EVENT_CHARGING_STARTED, soc);
    } else if (s_status.usb_present && !s_charge_paused) {
        /* Stopped with USB still present and nothing holding it off:
         * charge complete. */
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
        const mao_power_temp_source_t source = s_temp_source;
        const esp_err_t err = source ? source(&temp) : ESP_ERR_NOT_SUPPORTED;
        valid = err == ESP_OK;
        if (!valid && !s_temp_fail_logged) {
            ESP_LOGW(TAG, "charge limit: no board temperature (%s); charging stays enabled "
                     "(the pack NTC still stops it at 50 C)", esp_err_to_name(err));
        }
        s_temp_fail_logged = !valid;
    }

    const bool pause = s_charge_hold || mao_policy_charge_pause(usb, s_charge_paused, valid, temp);
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
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.charge_paused = s_charge_paused;
    s_status.temp_valid = valid;
    if (valid) {
        s_status.temp_c = temp;
    }
    xSemaphoreGive(s_lock);
}

/* USB line: post an event for every change; then the charging state. */
static void check_lines(void)
{
    bool usb = s_status.usb_present;
    mao_board_line_get(MAO_LINE_USB_PRESENT, &usb);

    const bool usb_was = s_status.usb_present;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.usb_present = usb;
    xSemaphoreGive(s_lock);

    if (usb != usb_was) {
        ESP_LOGI(TAG, "USB %s", usb ? "connected" : "disconnected");
        mao_event_post(usb ? MAO_EVENT_USB_CONNECTED : MAO_EVENT_USB_DISCONNECTED, 0);
        update_gauge();          /* fresh SOC / rate for the charging estimate */
        charge_limit(true);      /* starts (or stops) watching the temperature now */
    }
    update_charging();
}

/* The alert line is a wired-OR of the gauge ALRT and the light sensor INT.
 * Reading it also clears the expander INT, so it is read on every expander
 * interrupt, gauge or not. Only the gauge's part is handled here; the light
 * sensor clears its own latch when mao_sense reads it. */
static void check_alert(void)
{
    bool alert = false;
    if (mao_board_line_get(MAO_LINE_SENSE_ALERT, &alert) != ESP_OK || !alert || !s_gauge_ok) {
        return;
    }
    uint16_t flags = 0;
    if (mao_gauge_take_alert(&flags) == ESP_OK && flags) {
        ESP_LOGI(TAG, "gauge alert 0x%04x%s%s", flags,
                 (flags & MAO_GAUGE_ST_HD) ? " SOC-low" : "", (flags & MAO_GAUGE_ST_VL) ? " V-low" : "");
        update_gauge();
    }
}

/* ------------------------------------------------------------------------ */
/* Hooks and transitions                                                    */
/* ------------------------------------------------------------------------ */

esp_err_t mao_power_register_hook(const char *name, mao_power_hook_t hook, void *ctx)
{
    ESP_RETURN_ON_FALSE(hook, ESP_ERR_INVALID_ARG, TAG, "bad hook");
    ESP_RETURN_ON_FALSE(s_hook_count < MAX_HOOKS, ESP_ERR_NO_MEM, TAG, "hook table full");
    s_hooks[s_hook_count++] = (hook_t) { .name = name, .fn = hook, .ctx = ctx };
    return ESP_OK;
}

static void transition(mao_power_state_t to, mao_sleep_reason_t reason)
{
    const mao_power_transition_t t = { .from = s_state, .to = to, .reason = reason };
    if (t.from == t.to) {
        return;
    }
    ESP_LOGI(TAG, "state %s -> %s", mao_power_state_name(t.from), mao_power_state_name(t.to));
    for (size_t i = 0; i < s_hook_count; i++) {
        s_hooks[i].fn(&t, s_hooks[i].ctx);
    }
    /* Dial sensors sample fast only while MAO can react to them. */
    mao_board_hall_fast_set(to == MAO_POWER_ACTIVE || to == MAO_POWER_IDLE);
    s_state = to;
    if (to != MAO_POWER_DEEP_SLEEP) {
        mao_event_post(MAO_EVENT_POWER_STATE, (int32_t)to);
    }
}

static void enter_deep_sleep(uint32_t wake_after_s, mao_sleep_reason_t reason)
{
    const bool battery = reason == MAO_SLEEP_REASON_BATTERY;
    transition(MAO_POWER_DEEP_SLEEP, reason);

    s_rtc.magic = RTC_MAGIC;
    s_rtc.sleep_count++;
    s_rtc.reason = (uint32_t)reason;
    s_rtc.sleep_start_us = esp_rtc_get_time_us();

    /* On a critical battery only USB may wake MAO: anything else would boot
     * it into the same critical state again. */
    const mao_board_wake_t want = {
        .press = !battery,
        .motion = !battery,
        .usb = true,
        .expander = !battery,
    };
    mao_board_wake_t armed;
    mao_board_deep_sleep_prepare(&want, &armed);
    if (battery) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TOUCHPAD);
    } else {
        const uint64_t s = wake_after_s ? wake_after_s : (uint64_t)CONFIG_MAO_POWER_WAKE_INTERVAL_MIN * 60;
        esp_sleep_enable_timer_wakeup(s * 1000000ULL);
    }
    ESP_LOGW(TAG, "deep sleep (%s): wake on%s%s%s%s%s", reason_name(reason),
             armed.press ? " press" : "", armed.motion ? " motion" : "", armed.usb ? " usb" : "",
             armed.expander ? " charger/alert" : "", battery ? "" : " touch timer");
    vTaskDelay(pdMS_TO_TICKS(LOG_FLUSH_MS));
    esp_deep_sleep_start();
}

void mao_power_policy_config(struct mao_policy_config *out)
{
    if (!out) {
        return;
    }
#if CONFIG_MAO_POWER_AUTO
    *out = (mao_policy_config_t) {
        .enabled = s_task != NULL && s_caps.sleep,
        .idle_after_ms = (uint32_t)CONFIG_MAO_POWER_IDLE_AFTER_S * 1000u,
        .drowsy_after_ms = (uint32_t)CONFIG_MAO_POWER_DROWSY_AFTER_S * 1000u,
        .deep_after_ms = (uint32_t)CONFIG_MAO_POWER_DEEP_AFTER_MIN * 60u * 1000u,
        .redrowse_ms = (uint32_t)CONFIG_MAO_POWER_REDROWSE_S * 1000u,
#if CONFIG_MAO_POWER_SLEEP_WITH_CONSOLE
        .sleep_with_console = true,
#endif
#if CONFIG_MAO_POWER_DEEP_ON_USB
        .deep_on_usb = true,
#endif
    };
#else
    *out = (mao_policy_config_t) { .enabled = false };
#endif
}

mao_wake_source_t mao_power_last_wake(void)
{
    return s_last_wake;
}

/* Light-sleep loop for DROWSY. Returns when something other than the
 * housekeeping timer woke the chip, or after duration_s (0 = no limit).
 * Without a limit (the policy's doze) a long enough doze on battery ends
 * in deep sleep (mao_policy_drowsy_to_deep). */
static void drowsy_loop(uint32_t duration_s)
{
    const int64_t start = esp_timer_get_time();
    const int64_t until = duration_s ? start + (int64_t)duration_s * 1000000 : 0;
    mao_policy_config_t policy;
    mao_power_policy_config(&policy);
    const bool may_deepen = duration_s == 0 && policy.enabled && policy.deep_after_ms > 0;
    mao_wake_source_t source = MAO_WAKE_NONE;
    for (;;) {
        uint64_t sleep_us = (uint64_t)DROWSY_HOUSEKEEPING_S * 1000000ULL;
        if (may_deepen) {
            const int64_t dozed_ms = (esp_timer_get_time() - start) / 1000;
            if (mao_policy_drowsy_to_deep(&policy, (uint32_t)dozed_ms, s_status.usb_present,
                                          mao_system_console_attached())) {
                ESP_LOGI(TAG, "drowsy for %" PRId64 " s with nobody around: deep sleep", dozed_ms / 1000);
                enter_deep_sleep(0, MAO_SLEEP_REASON_IDLE);
            }
            const int64_t left_ms = (int64_t)policy.deep_after_ms - dozed_ms;
            if (left_ms > 0 && (uint64_t)left_ms * 1000ULL < sleep_us) {
                sleep_us = (uint64_t)left_ms * 1000ULL + 1000ULL;
            }
        }
        if (s_status.usb_present && s_caps.charge_control &&
            sleep_us > (uint64_t)CHARGE_LIMIT_PERIOD_MS * 1000ULL) {
            sleep_us = (uint64_t)CHARGE_LIMIT_PERIOD_MS * 1000ULL;   /* the charge limit keeps watching */
        }
        if (until) {
            const int64_t left = until - esp_timer_get_time();
            if (left <= 0) {
                source = MAO_WAKE_TIMER;
                break;
            }
            if ((uint64_t)left < sleep_us) {
                sleep_us = (uint64_t)left;
            }
        }
        const mao_board_wake_t want = {
            .press = true, .motion = true, .usb = true, .expander = true, .proximity = true,
        };
        mao_board_light_sleep_prepare(&want, NULL);
        esp_sleep_enable_timer_wakeup(sleep_us);
        vTaskDelay(pdMS_TO_TICKS(LOG_FLUSH_MS));
        const esp_err_t err = esp_light_sleep_start();
        source = classify_wake();
        mao_board_light_sleep_done();
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "light sleep rejected: %s", esp_err_to_name(err));
            source = MAO_WAKE_OTHER;
            break;
        }
        /* Housekeeping on every wake: the battery may have moved (a
         * critical battery deep-sleeps from here), and on a charger the
         * temperature is checked. */
        check_lines();
        update_gauge();
        charge_limit(false);
        update_charging();
        battery_policy();
        if (source != MAO_WAKE_TIMER) {
            break;
        }
    }
    ESP_LOGI(TAG, "drowsy ended by %s", mao_wake_source_name(source));
    s_last_wake = source;
    transition(MAO_POWER_ACTIVE, MAO_SLEEP_REASON_NONE);
}

static void handle_request(const request_t *req)
{
    switch (req->kind) {
    case REQ_STATE:
        transition(req->state, MAO_SLEEP_REASON_NONE);
        if (req->state == MAO_POWER_DROWSY) {
            drowsy_loop(0);
        }
        break;
    case REQ_DROWSY_FOR:
        transition(MAO_POWER_DROWSY, MAO_SLEEP_REASON_NONE);
        drowsy_loop(req->seconds);
        break;
    case REQ_DEEP_SLEEP:
        enter_deep_sleep(req->seconds, req->reason);
        break;
    case REQ_CHARGE_HOLD:
        s_charge_hold = req->seconds != 0;
        charge_limit(true);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Battery policy                                                           */
/* ------------------------------------------------------------------------ */

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

    /* Count consecutive gauge readings, not policy runs (line events also
     * run the policy). */
    if (s_status.updated_ms != s_policy_reading_ms) {
        s_policy_reading_ms = s_status.updated_ms;
        const bool critical = on_battery && (soc <= CRITICAL_PCT || s_status.voltage_mv < CRITICAL_MV);
        s_critical_count = critical ? s_critical_count + 1 : 0;
    }
    if (!on_battery) {
        s_critical_count = 0;
    }
    if (!on_battery) {
        s_critical_reported = false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.low = s_low_reported;
    s_status.critical = s_critical_count >= CRITICAL_READINGS;
    xSemaphoreGive(s_lock);

    if (s_critical_count >= CRITICAL_READINGS && !s_critical_reported) {
        s_critical_reported = true;
        s_critical_deadline_us = esp_timer_get_time() + (int64_t)CRITICAL_GRACE_MS * 1000;
        ESP_LOGE(TAG, "battery critical: %.1f %%, %u mV", (double)soc, s_status.voltage_mv);
        mao_event_post(MAO_EVENT_BATTERY_CRITICAL, (int32_t)soc);
    }
#if CONFIG_MAO_POWER_CRITICAL_SHUTDOWN
    if (s_critical_reported && on_battery && esp_timer_get_time() >= s_critical_deadline_us) {
        enter_deep_sleep(0, MAO_SLEEP_REASON_BATTERY);
    }
#endif
}

/* ------------------------------------------------------------------------ */
/* Task, ISRs                                                               */
/* ------------------------------------------------------------------------ */

static void line_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(s_task, (uint32_t)(uintptr_t)arg, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

static void power_task(void *arg)
{
    (void)arg;
    int64_t next_gauge_us = 0;
    for (;;) {
        const bool watch = s_low_reported || s_critical_count > 0 || s_critical_reported;
        const uint32_t period_ms = watch ? GAUGE_PERIOD_LOW_MS : GAUGE_PERIOD_MS;
        int64_t next_us = next_gauge_us;
        if (s_status.usb_present && s_caps.charge_control && s_next_charge_check_us < next_us) {
            next_us = s_next_charge_check_us;
        }
        int64_t wait_us = next_us - esp_timer_get_time();
        wait_us = wait_us < 0 ? 0 : wait_us;

        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(wait_us / 1000) + 1);

        if (bits & (NOTIFY_USB | NOTIFY_EXPANDER)) {
            check_lines();
        }
        if (bits & NOTIFY_EXPANDER) {
            check_alert();
        }
        if (esp_timer_get_time() >= next_gauge_us) {
            update_gauge();
            next_gauge_us = esp_timer_get_time() + (int64_t)period_ms * 1000;
        }
        charge_limit(false);
        update_charging();
        battery_policy();

        request_t req;
        while (xQueueReceive(s_requests, &req, 0) == pdTRUE) {
            handle_request(&req);
        }
    }
}

static esp_err_t install_isr(mao_board_irq_t irq, gpio_int_type_t type, uint32_t bit)
{
    mao_board_irq_desc_t d;
    ESP_RETURN_ON_ERROR(mao_board_irq_get(irq, &d), TAG, "irq %d", (int)irq);
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(d.gpio, type), TAG, "intr type");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(d.gpio, line_isr, (void *)(uintptr_t)bit), TAG, "isr");
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* Requests                                                                 */
/* ------------------------------------------------------------------------ */

static esp_err_t send_request(const request_t *req)
{
    ESP_RETURN_ON_FALSE(s_task, ESP_ERR_NOT_SUPPORTED, TAG, "power manager not running");
    ESP_RETURN_ON_FALSE(xQueueSend(s_requests, req, 0) == pdTRUE, ESP_ERR_TIMEOUT, TAG, "request queue full");
    xTaskNotify(s_task, NOTIFY_REQUEST, eSetBits);
    return ESP_OK;
}

esp_err_t mao_power_request_state(mao_power_state_t state)
{
    ESP_RETURN_ON_FALSE(state < MAO_POWER_DEEP_SLEEP, ESP_ERR_INVALID_ARG, TAG,
                        "use mao_power_deep_sleep() for deep sleep");
    const request_t req = { .kind = REQ_STATE, .state = state };
    return send_request(&req);
}

esp_err_t mao_power_deep_sleep(uint32_t wake_after_s, mao_sleep_reason_t reason)
{
    const request_t req = { .kind = REQ_DEEP_SLEEP, .seconds = wake_after_s, .reason = reason };
    return send_request(&req);
}

esp_err_t mao_power_drowsy_for(uint32_t duration_s)
{
    const request_t req = { .kind = REQ_DROWSY_FOR, .seconds = duration_s };
    return send_request(&req);
}

/* ------------------------------------------------------------------------ */
/* Development console                                                      */
/* ------------------------------------------------------------------------ */

esp_err_t mao_power_refresh(void)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NOT_SUPPORTED, TAG, "power manager not running");
    ESP_RETURN_ON_FALSE(s_gauge_ok, ESP_ERR_NOT_FOUND, TAG, "no fuel gauge");
    update_gauge();
    return ESP_OK;
}

esp_err_t mao_power_gauge_version(uint16_t *version)
{
    ESP_RETURN_ON_FALSE(s_gauge_ok, ESP_ERR_NOT_FOUND, TAG, "no fuel gauge");
    return mao_gauge_version(version);
}

#if CONFIG_MAO_DEV_CONSOLE

static void devcmd_power(const char *args)
{
    if (strncmp(args, "charge", 6) == 0) {    /* power charge off | on: hold /CE high by hand, or release it */
        const char *v = args + 6;
        while (*v == ' ') {
            v++;
        }
        if (!s_caps.charge_control || (strcmp(v, "off") != 0 && strcmp(v, "on") != 0)) {
            ESP_LOGW(TAG, "power charge off | on%s", s_caps.charge_control ? "" : ": no charge control on this board");
            return;
        }
        const request_t req = { .kind = REQ_CHARGE_HOLD, .seconds = strcmp(v, "off") == 0 };
        if (send_request(&req) == ESP_OK) {
            ESP_LOGI(TAG, "charging %s", req.seconds ? "held off (temperature limit still applies when released)"
                                                     : "released to the temperature limit");
        }
        return;
    }
    mao_power_status_t st;
    update_gauge();
    mao_power_get_status(&st);
    if (st.gauge) {
        ESP_LOGI(TAG, "battery: %u mV, %.1f %%, %+.1f %%/h%s%s", st.voltage_mv, (double)st.soc_pct,
                 (double)st.rate_pct_per_h, st.low ? " LOW" : "", st.critical ? " CRITICAL" : "");
    } else {
        ESP_LOGI(TAG, "battery: no fuel gauge");
    }
    ESP_LOGI(TAG, "charger: usb=%d charging=%d%s%s; state %s", st.usb_present, st.charging,
             s_chg_line ? "" : " (estimated from the gauge)",
             st.charge_paused ? (s_charge_hold ? ", PAUSED (console hold)" : ", PAUSED (temperature)") : "",
             mao_power_state_name(s_state));
    if (s_caps.charge_control) {
        if (st.temp_valid) {
            ESP_LOGI(TAG, "charge limit: board %.1f C (pause >= %.0f C, resume <= %.0f C)", (double)st.temp_c,
                     (double)MAO_CHARGE_PAUSE_C, (double)MAO_CHARGE_RESUME_C);
        } else {
            ESP_LOGI(TAG, "charge limit: %s", st.usb_present ? "no board temperature" : "idle (no USB)");
        }
    }
    const mao_power_continuity_t *c = &s_cont;
    ESP_LOGI(TAG, "continuity: woke_from_sleep=%d reason=%s source=%s slept=%" PRIu64 " ms sleeps=%" PRIu32,
             c->woke_from_sleep, reason_name(c->reason), mao_wake_source_name(c->source), c->slept_ms,
             c->sleep_count);
}

static void devcmd_sleep(const char *args)
{
    if (strncmp(args, "light", 5) == 0) {
        const int s = atoi(args + 5);
        ESP_LOGW(TAG, "dev: drowsy (light sleep) for up to %d s (0 = until woken)", s);
        mao_power_drowsy_for(s > 0 ? (uint32_t)s : 0);
        return;
    }
    const int s = atoi(args);
    ESP_LOGW(TAG, "dev: deep sleep, timer %d s (0 = %d min)", s, CONFIG_MAO_POWER_WAKE_INTERVAL_MIN);
    mao_power_deep_sleep(s > 0 ? (uint32_t)s : 0, MAO_SLEEP_REASON_DEV);
}

static void register_devcmds(void)
{
    mao_devcmd_register("power", "power [charge off|on]  (battery, charger, power state, sleep continuity; "
                        "hold charging off by hand)", devcmd_power);
    mao_devcmd_register("sleep", "sleep [seconds] | sleep light [seconds]  (deep / light-sleep test)",
                        devcmd_sleep);
}

#else

static void register_devcmds(void)
{
}

#endif

/* ------------------------------------------------------------------------ */
/* Init                                                                     */
/* ------------------------------------------------------------------------ */

static void continuity_init(void)
{
    memset(&s_cont, 0, sizeof(s_cont));
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP && s_rtc.magic == RTC_MAGIC) {
        const uint64_t now = esp_rtc_get_time_us();
        s_cont.woke_from_sleep = true;
        s_cont.reason = (mao_sleep_reason_t)s_rtc.reason;
        s_cont.source = classify_wake();
        s_cont.slept_ms = now > s_rtc.sleep_start_us ? (now - s_rtc.sleep_start_us) / 1000 : 0;
        s_cont.sleep_count = s_rtc.sleep_count;
        ESP_LOGI(TAG, "woke from deep sleep #%" PRIu32 " (%s) after %" PRIu64 " s, by %s",
                 s_cont.sleep_count, reason_name(s_cont.reason), s_cont.slept_ms / 1000,
                 mao_wake_source_name(s_cont.source));
    } else {
        s_rtc = (rtc_record_t) { .magic = RTC_MAGIC };
    }
}

esp_err_t mao_power_init(void)
{
    mao_board_get_caps(&s_caps);
    if (!s_caps.power_status && !s_caps.sleep) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    continuity_init();

    s_lock = xSemaphoreCreateMutex();
    s_requests = xQueueCreate(REQUEST_QUEUE_LEN, sizeof(request_t));
    ESP_RETURN_ON_FALSE(s_lock && s_requests, ESP_ERR_NO_MEM, TAG, "alloc");

    /* A missing gauge is reported but not fatal: USB, charger and sleep
     * management work without it. */
    s_gauge_ok = mao_system_report("fuel gauge", mao_gauge_init()) == ESP_OK;
    s_status.gauge = s_gauge_ok;
    mao_board_line_get(MAO_LINE_USB_PRESENT, &s_status.usb_present);
    bool chg = false;
    s_chg_line = mao_board_line_get(MAO_LINE_CHARGING, &chg) == ESP_OK;
    update_gauge();
    if (charging_now(&chg)) {
        s_status.charging = chg;
    }
    /* The expander came up with the charger enabled. The first temperature
     * check waits one period, so the source (mao_sense, started after this)
     * is registered by then. */
    s_next_charge_check_us = esp_timer_get_time() + (int64_t)CHARGE_LIMIT_PERIOD_MS * 1000;

    if (xTaskCreate(power_task, "mao_power", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "isr service");
    ESP_RETURN_ON_ERROR(install_isr(MAO_IRQ_USB_PRESENT, GPIO_INTR_ANYEDGE, NOTIFY_USB), TAG, "usb isr");
    if (s_caps.expander) {
        ESP_RETURN_ON_ERROR(install_isr(MAO_IRQ_EXPANDER, GPIO_INTR_NEGEDGE, NOTIFY_EXPANDER), TAG, "exp isr");
        xTaskNotify(s_task, NOTIFY_EXPANDER, eSetBits);   /* catch an alert pending since boot */
    }
    register_devcmds();

    ESP_LOGI(TAG, "power: battery %u mV %.1f %%, usb=%d charging=%d%s; sleep timer %d min" CRITICAL_NOTE,
             s_status.voltage_mv, (double)s_status.soc_pct, s_status.usb_present, s_status.charging,
             s_chg_line ? "" : " (estimated)", CONFIG_MAO_POWER_WAKE_INTERVAL_MIN);
    return ESP_OK;
}
