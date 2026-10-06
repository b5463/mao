/*
 * MAO's rest (M4.1 power): the hardware side of the ladder in mao_power.h.
 *
 *   IDLE           the approved sleepy state (mao_app.c: dim, eyes closed)
 *   SLEEP DISPLAY  the sleeping eyes and the ODD JOBS maker's mark in
 *                  lavender, the backlight at MAO_PWR_SLEEP_PCT; once the
 *                  frame has settled the chip goes into LIGHT SLEEP with the
 *                  radio and the audio stopped and the knob as the wake source
 *   NIGHT          (a timer wake inside light sleep) the backlight off and
 *                  the panel asleep; the knob still wakes it
 *   deep sleep     DEV only ("deepsleep <s>"): a timer wakes it, or reset.
 *                  The knob cannot wake the C3 from deep sleep on the LCDkit;
 *                  on the A1 the press, the dial and IMU motion can.
 *   critical       (boards with a battery) MAO_EVENT_BATTERY_CRITICAL: the
 *                  sleeping frame, then deep sleep that only the press or a
 *                  recheck timer ends; a recheck that still finds the cell
 *                  critical goes straight back to sleep, dark.
 *
 * Boards with more than the LCDkit's knob (A1): while the chip rests the
 * sensors drop to their rest level, haptics and the IR receiver are off,
 * and on USB the light sleep wakes at least every 10 s so the charge
 * temperature limit (mao_battery_poll) keeps running; a USB plug / unplug
 * also ends one sleep of the loop (housekeeping, not a wake of MAO).
 *
 * The ladder (s_pwr) belongs to the app task while MAO is up, and to the
 * power task while the chip is resting (s_resting): the hand-over is the
 * notify going one way and MAO_EVENT_POWER_WAKE coming back. Nothing here
 * writes NVS.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_rtc_time.h"
#include "driver/usb_serial_jtag.h"
#include "esp_system.h"
#include "hal/wdt_hal.h"
#include "hal/rwdt_ll.h"
#include "soc/rtc.h"
#include "sdkconfig.h"
#include "mao_app_priv.h"
#include "mao_audio.h"
#include "mao_battery.h"
#include "mao_board.h"
#include "mao_character.h"
#include "mao_devices.h"
#include "mao_display.h"
#include "mao_events.h"
#include "mao_haptics.h"
#include "mao_input.h"
#include "mao_ir.h"
#include "mao_link.h"
#include "mao_perception.h"
#include "mao_power.h"
#include "mao_radio.h"
#include "mao_selftest.h"
#include "mao_sense.h"
#include "mao_system.h"
#include "mao_ui.h"

static const char *TAG = "MAO_POWER";

#define REST_SETTLE_MS   4000     /* the sleeping eyes dim before the resting mark ... */
#define REST_MARK_MS     1800     /* ... and the mark settles before the chip sleeps */
#define REST_FADE_MS     3000
#define REST_RETRY_MS    5000     /* busy when it was time to sleep: look again */
#define DEEP_HOLD_MS     1500     /* DEV deep sleep: the last composition holds ... */
#define DEEP_FADE_MS     700      /* ... then the light goes out */
#define PANEL_WAKE_MS    120      /* GC9A01 sleep-out */

#define HEARTBEAT_MS     (10u * 60u * 1000u)   /* the longest sleep without a look (the watchdog's bound) */
#define WDT_SLACK_MS     15000u

#define NOTIFY_REST  (1u << 0)
#define NOTIFY_DEEP  (1u << 1)
#define NOTIFY_CRITICAL (1u << 2)

#define USB_LOOK_MS       10000u    /* on USB: the longest light sleep (the charge limit's period) */
#define CRITICAL_MAGIC    0x43524954u   /* "CRIT": the last deep sleep was the critical-battery one */

/* The marker in RTC memory: survives deep sleep (not a power cycle). */
static RTC_NOINIT_ATTR uint32_t s_rtc_marker;
/* With the marker (folded in from A0's continuity record): when the deep
 * sleep began (RTC clock) and whether it was the critical-battery one. */
static RTC_NOINIT_ATTR uint64_t s_rtc_sleep_at_us;
static RTC_NOINIT_ATTR uint32_t s_rtc_critical;
/* How far the last light sleep got (read at the next boot): a sleep that
 * never ends is reset by the RTC watchdog, and this says where it stopped. */
#define CRUMB_MAGIC   0xC0DE0000u
#define CRUMB_SLEEP   1u    /* inside esp_light_sleep_start */
#define CRUMB_BACK    2u    /* returned from it */
static RTC_NOINIT_ATTR uint32_t s_rtc_crumb;
static RTC_NOINIT_ATTR uint32_t s_rtc_crumb_n;   /* sleeps entered in that rest */

/* The RTC watchdog, counting through the sleep (IDF's own pauses in sleep):
 * if the chip does not come back by the planned wake + slack, it resets. */
static void sleep_wdt(bool on, uint64_t sleep_us)
{
    wdt_hal_context_t w = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&w);
    if (on) {
        wdt_hal_init(&w, WDT_RWDT, 0, false);
        const uint64_t ms = sleep_us / 1000u + WDT_SLACK_MS;
        wdt_hal_config_stage(&w, WDT_STAGE0, (uint32_t)(ms * rtc_clk_slow_freq_get_hz() / 1000u),
                             WDT_STAGE_ACTION_RESET_RTC);
        rwdt_ll_set_pause_in_sleep_en(w.rwdt_dev, false);
        wdt_hal_enable(&w);
        wdt_hal_feed(&w);
    } else {
        wdt_hal_disable(&w);
    }
    wdt_hal_write_protect_enable(&w);
}

static mao_power_t s_pwr;
static mao_wake_eat_t s_eat;
static volatile bool s_resting;       /* the power task owns the ladder */
static TaskHandle_t s_task;
static esp_timer_handle_t s_rest_timer;
static uint32_t s_deep_s;
static bool s_from_deep;
static bool s_night;                  /* the last rest reached the night */
static uint32_t s_rest_count;
static int64_t s_slept_us;            /* time in light sleep, this rest */
/* DEV ("power rest|night [s]"): the idle clock runs ahead by s_bias_ms, and
 * a timer ends the rest after s_dev_wake_s as a knob would (the console
 * cannot turn the knob). Both clear at the wake. */
static uint32_t s_bias_ms;
static uint32_t s_dev_wake_s;
static bool s_dev_force;              /* DEV: light sleep even with a USB host */
/* A USB host is attached (the bench): the C3's USB-Serial-JTAG does not
 * survive light sleep (IDF turns its pad off, and the host loses the port
 * until a reset), so the chip stays up - the sleeping screen, then the
 * night, drawn by an awake chip. On a charger it sleeps (IDF's own rule for
 * automatic light sleep is the same: CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION). */
static bool s_host_awake;
static bool s_marked;                 /* the resting mark is up (the eyes have gone) */

static uint32_t idle_ms(int64_t now)
{
    return mao_state_idle_ms(now) + s_bias_ms;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void busy_now(mao_pwr_busy_t *b)
{
    memset(b, 0, sizeof(*b));
    /* a factory self-test waits for its operator: like an action awaiting
     * its result, it keeps MAO awake (never true on the LCDkit) */
    b->action_pending = mao_selftest_running();
    mao_link_pair_status_t ps;
    mao_link_pair_status(&ps);
    b->pairing = ps.st != ODL_C_IDLE || mao_rel_sheet_open();
    b->forgetting = mao_app_rel_forgetting();
    b->transfer = mao_transfer_active();
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        mao_device_t dv;
        if (mao_devices_get(i, &dv)) {
            const mao_action_state_t a = mao_devices_action_state(dv.info.id);
            if (a == MAO_ACTION_SENDING || a == MAO_ACTION_ACCEPTED) {
                b->action_pending = true;
            }
        }
    }
}

static void log_busy(const char *what, const mao_pwr_busy_t *b)
{
    ESP_LOGI(TAG, "%s: stays awake (%s%s%s%s)", what, b->pairing ? "pairing " : "", b->forgetting ? "forget " : "",
             b->action_pending ? "action " : "", b->transfer ? "transfer" : "");
}

static void rest_timer_cb(void *arg)
{
    (void)arg;
    mao_event_post(MAO_EVENT_POWER_RESTED, 0);
}

static void rest_later(uint32_t ms)
{
    esp_timer_stop(s_rest_timer);
    esp_timer_start_once(s_rest_timer, (uint64_t)ms * 1000);
}

/* ---------------------------------------------------------------------- */
/* The power task: the chip's rest                                        */
/* ---------------------------------------------------------------------- */

static void critical_rest(bool dark);

/* While the chip rests the event queue does not run: a cell that turns
 * critical in light sleep is acted on from the rest loop itself. */
static bool critical_now(void)
{
#if CONFIG_MAO_BATTERY_CRITICAL_SLEEP
    mao_battery_status_t st;
    mao_battery_get_status(&st);
    return st.critical && !st.usb_present;
#else
    return false;
#endif
}

/* Everything besides the display and the audio that rests with the chip
 * (all no-ops on the LCDkit). */
static void peripherals_rest(bool rest)
{
    if (rest) {
        mao_haptics_suspend(true);
        mao_ir_suspend(true);
        mao_sense_set_level(MAO_SENSE_REST);
        mao_perception_rest(true);
    } else {
        mao_sense_set_level(MAO_SENSE_AWAKE);     /* the ToF boots again: the slow part, last */
        mao_ir_suspend(false);
        mao_perception_rest(false);
    }
}

/* Deep sleep next: the sensors' deep level (motion = IMU wake armed). */
static void peripherals_deep(mao_sense_level_t level)
{
    mao_haptics_suspend(true);
    mao_ir_suspend(true);
    mao_sense_set_level(level);
    mao_perception_deep_sleep();
    s_rtc_sleep_at_us = esp_rtc_get_time_us();
}

static void light_rest(void)
{
    const uint32_t idle0 = mao_state_idle_ms(esp_timer_get_time());
    if (idle0 < 1000 || !mao_display_lock(500)) {
        mao_event_post(MAO_EVENT_POWER_WAKE, 2);   /* touched meanwhile (or the screen is busy): not now */
        return;
    }
    peripherals_rest(true);
    vTaskDelay(pdMS_TO_TICKS(40));                 /* the last flush's DMA has finished */
    mao_audio_suspend();                           /* the PDM line goes quiet on a floor, then stops */
    mao_board_audio_hold(true);                    /* ... and is held low: the pad's sleep switch must not reach the amp */
    mao_radio_sleep(true);
    s_slept_us = 0;
    s_rtc_crumb_n = 0;
    s_night = s_pwr.st == MAO_PWR_NIGHT;
    int wakes = 0;
    for (;;) {
        mao_input_sleep(true);
        mao_board_knob_wake_arm(true);
        /* besides the knob: USB comes or goes (A1; the LCDkit has no line) */
        const mao_board_wake_t also = { .usb = true };
        mao_board_light_sleep_prepare(&also, NULL);
        esp_sleep_enable_gpio_wakeup();
        const uint32_t idle = idle_ms(esp_timer_get_time());
        uint64_t until_us = 0;
        if (!s_night && idle < MAO_PWR_NIGHT_MS) {
            until_us = (uint64_t)(MAO_PWR_NIGHT_MS - idle) * 1000u;
        }
        if (s_dev_wake_s) {
            const uint64_t left = (uint64_t)s_dev_wake_s * 1000000u - (uint64_t)s_slept_us;
            if (until_us == 0 || left < until_us) {
                until_us = left > 10000 ? left : 10000;
            }
        }
        if (until_us == 0 || until_us > (uint64_t)HEARTBEAT_MS * 1000u) {
            until_us = (uint64_t)HEARTBEAT_MS * 1000u;       /* the night too: a look every few minutes */
        }
        if (mao_battery_usb_present() && until_us > (uint64_t)USB_LOOK_MS * 1000u) {
            until_us = (uint64_t)USB_LOOK_MS * 1000u;        /* the charge limit keeps its 10 s */
        }
        esp_sleep_enable_timer_wakeup(until_us);
        ESP_LOGI(TAG, "chip sleeps (timer %" PRIu64 " ms)", until_us / 1000);
        const int64_t t0 = esp_timer_get_time();
        s_rtc_crumb = CRUMB_MAGIC | CRUMB_SLEEP;
        s_rtc_crumb_n++;
        sleep_wdt(true, until_us);
        const esp_err_t se = esp_light_sleep_start();
        sleep_wdt(false, 0);
        s_rtc_crumb = CRUMB_MAGIC | CRUMB_BACK;
        s_slept_us += esp_timer_get_time() - t0;
        const uint32_t causes = esp_sleep_get_wakeup_causes();
        ESP_LOGI(TAG, "chip woke: %s, causes 0x%" PRIx32 ", slept %" PRId64 " ms", esp_err_to_name(se), causes,
                 (esp_timer_get_time() - t0) / 1000);
        mao_board_wake_t why;
        mao_board_wake_decode(&why);                 /* before the pads are given back */
        mao_board_light_sleep_done();
        mao_board_knob_wake_arm(false);
        wakes++;
        if (causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
            if (why.usb && !why.dial && !why.press) {
                mao_battery_poll();                  /* USB came or went: housekeeping, not a wake */
                if (critical_now()) {
                    critical_rest(true);             /* does not return */
                }
                continue;
            }
            break;
        }
        if (causes & (1u << ESP_SLEEP_WAKEUP_TIMER)) {
            mao_battery_poll();                      /* the gauge and the charge limit (no-op on the LCDkit) */
            if (critical_now()) {
                ESP_LOGW(TAG, "critical battery while resting");
                critical_rest(true);                 /* does not return */
            }
            if (s_dev_wake_s && s_slept_us >= (int64_t)s_dev_wake_s * 1000000 - 20000) {
                break;                             /* DEV: the timer stands in for the knob */
            }
            const mao_pwr_busy_t none = { 0 };
            if (mao_power_idle(&s_pwr, idle_ms(esp_timer_get_time()), &none) == MAO_PWR_DO_NIGHT) {
                s_night = true;
                mao_display_set_brightness(0);
                mao_display_panel_sleep(true);
            }
            continue;
        }
        if (wakes > 1000) {
            break;                                 /* an unknown wake, again and again: stay up */
        }
    }
    const bool held = mao_board_switch_down();
    if (s_night) {
        mao_display_panel_sleep(false);
        vTaskDelay(pdMS_TO_TICKS(PANEL_WAKE_MS));
    }
    mao_display_unlock();
    mao_radio_sleep(false);
    mao_haptics_suspend(false);                    /* the waking press still clicks */
    mao_event_post(MAO_EVENT_POWER_WAKE, held ? 1 : 0);   /* before the input resumes: it is handled first */
    mao_input_sleep(false);
    mao_board_audio_hold(false);
    mao_audio_resume();                            /* the line rises behind the wake (the first touch is silent anyway) */
    peripherals_rest(false);
}

static void deep_rest(uint32_t seconds)
{
    vTaskDelay(pdMS_TO_TICKS(DEEP_HOLD_MS));       /* sleeping eyes + the maker's mark: the last frame */
    mao_display_fade_brightness(0, DEEP_FADE_MS);
    vTaskDelay(pdMS_TO_TICKS(DEEP_FADE_MS + 100));
    mao_audio_suspend();
    if (mao_display_lock(500)) {
        vTaskDelay(pdMS_TO_TICKS(40));
        mao_display_panel_sleep(true);             /* the panel keeps its picture, stops scanning */
    }
    mao_radio_sleep(true);
    peripherals_deep(MAO_SENSE_DEEP);
    s_rtc_marker = MAO_PWR_RTC_MAGIC;
    s_rtc_critical = 0;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    mao_board_deep_sleep_hold(true);               /* LCDkit: backlight and PDM held low; A1: rails off, knob / motion wake */
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000u);
    mao_board_caps_t caps;
    mao_board_get_caps(&caps);
    ESP_LOGI(TAG, "deep sleep now for %" PRIu32 " s (%s)", seconds,
             caps.deep_wake_knob ? "timer, reset, the press, the dial or motion wakes it"
                                 : "timer or reset wakes it; the knob cannot");
    vTaskDelay(pdMS_TO_TICKS(20));                 /* the log line leaves */
    esp_deep_sleep_start();
}

/* Critical battery: nothing but the press (and a recheck timer: USB cannot
 * wake the A1 from deep sleep) ends this sleep. No IMU wake, no dial wake:
 * the cell must not be drained by bumps. dark = straight away, without the
 * sleeping frame (a recheck that found the cell still critical). */
static void critical_rest(bool dark)
{
    if (!dark) {
        vTaskDelay(pdMS_TO_TICKS(DEEP_HOLD_MS));   /* the sleeping eyes + the mark: the last frame */
        mao_display_fade_brightness(0, DEEP_FADE_MS);
        vTaskDelay(pdMS_TO_TICKS(DEEP_FADE_MS + 100));
        mao_audio_suspend();
        if (mao_display_lock(500)) {
            vTaskDelay(pdMS_TO_TICKS(40));
            mao_display_panel_sleep(true);
        }
        mao_radio_sleep(true);
    }
    peripherals_deep(MAO_SENSE_OFF);
    s_rtc_marker = MAO_PWR_RTC_MAGIC;
    s_rtc_critical = CRITICAL_MAGIC;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const mao_board_wake_t want = { .press = true };
    mao_board_deep_sleep_prepare(&want, NULL);
#if CONFIG_MAO_BATTERY_CRITICAL_SLEEP
    esp_sleep_enable_timer_wakeup((uint64_t)CONFIG_MAO_BATTERY_CRITICAL_RECHECK_MIN * 60u * 1000000u);
#endif
    ESP_LOGW(TAG, "critical battery: deep sleep (the press wakes it; recheck timer)");
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_deep_sleep_start();
}

static void power_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        if (bits & NOTIFY_CRITICAL) {
            critical_rest(false);
        } else if (bits & NOTIFY_DEEP) {
            deep_rest(s_deep_s);
        } else if (bits & NOTIFY_REST) {
            light_rest();
        }
    }
}

/* ---------------------------------------------------------------------- */
/* The app side (app task)                                                */
/* ---------------------------------------------------------------------- */

bool mao_app_power_init(void)
{
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    const bool woke = (causes & ~(1u << ESP_SLEEP_WAKEUP_UNDEFINED)) != 0;
    s_from_deep = mao_power_boot_kind(woke, s_rtc_marker) == MAO_BOOT_FROM_DEEP;
    s_rtc_marker = 0;
    if ((s_rtc_crumb & 0xFFFF0000u) == CRUMB_MAGIC) {
        ESP_LOGW(TAG, "last light sleep: %s (sleep %" PRIu32 " of that rest), reset reason %d",
                 (s_rtc_crumb & 0xFFFFu) == CRUMB_SLEEP ? "NEVER CAME BACK" : "came back",
                 s_rtc_crumb_n, (int)esp_reset_reason());
    }
    s_rtc_crumb = 0;
    mao_power_init(&s_pwr);
    mao_wake_eat_reset(&s_eat);
    const bool from_critical = s_from_deep && s_rtc_critical == CRITICAL_MAGIC;
    s_rtc_critical = 0;
    if (s_from_deep) {
        /* the folded continuity: how long MAO slept (RTC clock; the RC
         * slow clock's accuracy is VERIFY AT BRING-UP) */
        const uint64_t now = esp_rtc_get_time_us();
        const uint64_t slept_ms = now > s_rtc_sleep_at_us ? (now - s_rtc_sleep_at_us) / 1000u : 0;
        mao_perception_woke(slept_ms);
        ESP_LOGI(TAG, "slept %" PRIu64 " s%s", slept_ms / 1000u, from_critical ? " (critical battery)" : "");
    }
    if (from_critical && (causes & (1u << ESP_SLEEP_WAKEUP_TIMER))) {
        /* The recheck timer: still critical on battery? Then back to sleep
         * before the screen lights (the display is up, backlight 0). */
        mao_battery_status_t st;
        mao_battery_get_status(&st);
        if (st.gauge && !st.usb_present && st.voltage_mv < CONFIG_MAO_BATTERY_CRITICAL_MV) {
            ESP_LOGW(TAG, "recheck: still critical (%u mV), back to sleep", st.voltage_mv);
            critical_rest(true);
        }
    }
    const esp_timer_create_args_t args = { .callback = rest_timer_cb, .name = "mao_rest" };
    esp_timer_create(&args, &s_rest_timer);
    xTaskCreate(power_task, "mao_power", 3072, NULL, 4, &s_task);
    if (s_from_deep) {
        ESP_LOGI(TAG, "woke from MAO's own deep sleep (causes 0x%" PRIx32 "): the short wake", causes);
    }
    return s_from_deep;
}

bool mao_app_power_from_deep(void)
{
    return s_from_deep;
}

/* The periodic look (every few seconds). */
void mao_app_power_tick(int64_t now)
{
    if (s_resting || mao_state()->view == MAO_VIEW_INTRO) {
        return;
    }
    mao_pwr_busy_t b;
    busy_now(&b);
    const mao_power_t before = s_pwr;
    const mao_pwr_do_t d = mao_power_idle(&s_pwr, idle_ms(now), &b);
    if (d == MAO_PWR_DO_SLEEP_DISPLAY) {
        ESP_LOGI(TAG, "sleep display after %" PRIu32 " min without input", idle_ms(now) / 60000u);
        mao_app_doze();
        mao_display_fade_brightness(MAO_PWR_SLEEP_PCT, REST_FADE_MS);
        s_marked = false;
        rest_later(REST_SETTLE_MS);
    } else if (d == MAO_PWR_DO_NIGHT) {
        if (s_host_awake) {
            ESP_LOGI(TAG, "night (the chip stays up: USB host): backlight off, panel asleep");
            mao_display_set_brightness(0);
            if (mao_display_lock(500)) {
                mao_display_panel_sleep(true);
                mao_display_unlock();
            }
        } else {
            /* not normally reached (the night comes inside light sleep): go back to rest */
            s_pwr = before;
            rest_later(10);
        }
    } else if (before.st == MAO_PWR_ACTIVE && mao_power_busy(&b) && idle_ms(now) >= MAO_PWR_SLEEP_MS &&
               (idle_ms(now) / 5000u) % 60u == 0) {
        log_busy("sleep display due", &b);
    }
}

/* The sleeping frame has settled. */
void mao_app_power_rested(void)
{
    if (s_resting || s_pwr.st == MAO_PWR_ACTIVE) {
        return;
    }
    mao_pwr_busy_t b;
    busy_now(&b);
    if (mao_power_busy(&b)) {
        log_busy("light sleep", &b);
        rest_later(REST_RETRY_MS);
        return;
    }
    if (!s_marked) {
        s_marked = true;
        mao_ui_sleep_mark(true);                   /* the eyes go; the mark rests in the centre */
        rest_later(REST_MARK_MS);
        return;
    }
    if (usb_serial_jtag_is_connected() && !s_dev_force) {
        if (!s_host_awake) {
            ESP_LOGI(TAG, "sleep display; no light sleep: a USB host is attached (the console must stay)");
        }
        s_host_awake = true;
        return;
    }
    s_dev_force = false;
    s_resting = true;
    s_rest_count++;
    ESP_LOGI(TAG, "light sleep (rest %" PRIu32 "): radio and audio off, the knob wakes MAO", s_rest_count);
    xTaskNotify(s_task, NOTIFY_REST, eSetBits);
}

/* MAO_EVENT_POWER_WAKE: 0 / 1 the knob woke it (1 = the button is held),
 * 2 = it did not sleep after all. */
void mao_app_power_woken(int32_t value, int64_t now)
{
    s_resting = false;
    const bool dev = s_dev_wake_s != 0;
    s_bias_ms = 0;
    s_dev_wake_s = 0;
    if (value == 2) {
        if (s_pwr.st != MAO_PWR_ACTIVE) {
            rest_later(REST_RETRY_MS);
        }
        return;
    }
    ESP_LOGI(TAG, "woke from light sleep by %s after %" PRId64 " s%s", dev ? "the DEV timer"
             : value ? "the knob (press)" : "the knob (turn)", s_slept_us / 1000000, s_night ? ", from the night" : "");
    mao_power_input(&s_pwr);
    mao_wake_eat_knob(&s_eat, value == 1, (uint32_t)(now / 1000));
    s_marked = false;
    mao_ui_sleep_mark(false);
    mao_app_wake_now(now);
}

/* Any input while MAO is up (before it acts). */
void mao_app_power_input(void)
{
    if (!s_resting && s_pwr.st != MAO_PWR_ACTIVE) {
        esp_timer_stop(s_rest_timer);              /* touched while the sleeping frame was settling */
        if (s_pwr.st == MAO_PWR_NIGHT && mao_display_lock(500)) {
            mao_display_panel_sleep(false);        /* the host-awake night: the panel comes back first */
            vTaskDelay(pdMS_TO_TICKS(PANEL_WAKE_MS));
            mao_display_unlock();
        }
        if (s_host_awake) {
            ESP_LOGI(TAG, "woke from the sleep display (the chip stayed up)");
        }
        s_host_awake = false;
        s_dev_force = false;
        s_marked = false;
        mao_power_input(&s_pwr);
        mao_ui_sleep_mark(false);
        s_bias_ms = 0;
        s_dev_wake_s = 0;
    }
}

static mao_in_t in_of(mao_event_type_t t)
{
    switch (t) {
    case MAO_EVENT_INPUT_PRESS:        return MAO_IN_PRESS;
    case MAO_EVENT_INPUT_RELEASE:      return MAO_IN_RELEASE;
    case MAO_EVENT_INPUT_CLICK:        return MAO_IN_CLICK;
    case MAO_EVENT_INPUT_LONG_PRESS:   return MAO_IN_LONG;
    case MAO_EVENT_INPUT_DOUBLE_CLICK: return MAO_IN_DOUBLE;
    default:                           return MAO_IN_TURN;
    }
}

/* The first touch after resting only wakes: true = eat this event. */
bool mao_app_power_eat(mao_event_type_t t, bool dimmed)
{
    const bool was = s_eat.eat != 0 || s_eat.quiet_until != 0;
    const bool eat = mao_wake_eat(&s_eat, in_of(t), dimmed, now_ms());
    if (eat && dimmed && !was) {
        ESP_LOGI(TAG, "%s woke MAO (not acted on)", t == MAO_EVENT_INPUT_PRESS ? "press" : "turn");
    }
    return eat;
}

/* MAO_EVENT_BATTERY_CRITICAL: the critical-battery sleep. Not on USB (the
 * cell is being charged), and a FORGET being committed or a pairing is
 * given up to 30 s to finish. */
void mao_app_power_critical(void)
{
    static int64_t s_first_us;
#if CONFIG_MAO_BATTERY_CRITICAL_SLEEP
    if (mao_battery_usb_present() || s_resting) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    s_first_us = s_first_us ? s_first_us : now;
    mao_pwr_busy_t b;
    busy_now(&b);
    if (mao_power_busy(&b) && now - s_first_us < 30LL * 1000000) {
        log_busy("critical battery sleep", &b);
        return;                                    /* the battery monitor reports it again */
    }
    esp_timer_stop(s_rest_timer);
    ESP_LOGW(TAG, "critical battery: MAO falls asleep");
    mao_app_doze();
    mao_ui_sleep_mark(true);
    mao_display_fade_brightness(MAO_PWR_SLEEP_PCT, 800);
    s_resting = true;
    xTaskNotify(s_task, NOTIFY_CRITICAL, eSetBits);
#else
    (void)s_first_us;
#endif
}

/* DEV: "deepsleep <s>" (MAO_EVENT_POWER_DEEP). Never while busy. */
void mao_app_power_deep(uint32_t seconds)
{
    mao_pwr_busy_t b;
    busy_now(&b);
    if (!mao_power_deep_allowed(&b)) {
        log_busy("deep sleep refused", &b);
        return;
    }
    if (seconds < 5 || seconds > 24u * 3600u) {
        ESP_LOGW(TAG, "deepsleep <5..86400 s>");
        return;
    }
    esp_timer_stop(s_rest_timer);
    ESP_LOGI(TAG, "dev: deep sleep for %" PRIu32 " s: the last frame, then dark", seconds);
    mao_app_doze();
    mao_ui_sleep_mark(true);
    mao_display_fade_brightness(MAO_PWR_SLEEP_PCT, 800);
    s_deep_s = seconds;
    s_resting = true;
    xTaskNotify(s_task, NOTIFY_DEEP, eSetBits);
}

/* DEV (mao_app_power_dev.c): rest now - the idle clock runs ahead to the
 * sleeping screen (or to a minute before the night); force = light sleep
 * even with a USB host; wake_s = a timer ends the rest as a knob would. */
void mao_app_power_dev_rest(bool night, bool force, uint32_t wake_s)
{
    s_dev_force = force;
    s_dev_wake_s = wake_s;
    s_bias_ms = (night ? MAO_PWR_NIGHT_MS - 60000u : MAO_PWR_SLEEP_MS) - mao_state_idle_ms(esp_timer_get_time());
    ESP_LOGI(TAG, "dev: rest now%s%s", night ? ", the night in a minute" : "",
             s_dev_wake_s ? " (a timer wakes it)" : " (the knob wakes it)");
    mao_event_post(MAO_EVENT_PAGE_IDLE, 0);
}

void mao_app_power_dev_status(void)
{
    mao_pwr_busy_t b;
    busy_now(&b);
    ESP_LOGI(TAG, "power: state %d, resting %d, rests %" PRIu32 ", idle %" PRIu32 " s, from deep %d, busy %d",
             (int)s_pwr.st, s_resting, s_rest_count, mao_state_idle_ms(esp_timer_get_time()) / 1000u,
             s_from_deep, mao_power_busy(&b));
    ESP_LOGI(TAG, "power: usb host %d, host-awake %d", usb_serial_jtag_is_connected(), s_host_awake);
}
