/*
 * mao_perception: ESP-IDF glue around the platform-independent percept
 * engine (core/percept_engine.c). See mao_perception.h.
 *
 * One task owns the engine. Everything that feeds it arrives through one
 * queue, from three producers that must never block:
 *   - mao_sense callbacks (sense task): every IMU / ToF observation;
 *   - the event bus (dispatcher task): dial, press, USB, battery;
 *   - the IR receive callback (IR task): frames that are not MAO's own echo.
 * The rest state (light / deep sleep) comes from mao_app's power ladder
 * through mao_perception_rest().
 *
 * One fiddling detector: the M4.1 mao_fiddle (HOME's light) is MAO's; the
 * engine's FIDDLING_ESCALATION (its own dial / motion score, host-tested)
 * is computed but never posted, so there is no second escalation path.
 * The task ticks the engine every TICK_MS, opens the self-stimulation gates
 * while MAO's own speaker or haptic actuator is active, and posts every
 * percept as MAO_EVENT_PERCEPT.
 */
#include "mao_perception.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "mao_audio.h"
#include "mao_haptics.h"
#include "mao_ir.h"
#include "mao_battery.h"
#include "mao_sense.h"
#include "mao_system.h"
#include "percept_engine.h"

static const char *TAG = "MAO_PERCEPT";

#define TASK_STACK          4096
#define TASK_PRIO           3
#define QUEUE_LEN           32
#define TICK_MS             50
#define RTC_MAGIC           0x50455243u    /* "PERC" */

typedef enum { ITEM_OBS = 0, ITEM_EVENT, ITEM_IR, ITEM_REST, ITEM_WOKE } item_kind_t;

typedef struct {
    uint8_t kind;
    union {
        mao_obs_t obs;
        struct {
            mao_event_type_t type;
            int32_t value;
            uint32_t t_ms;
        } ev;
        struct {
            uint8_t command;
            bool repeat;
            uint32_t t_ms;
        } ir;
    };
} item_t;

static pe_engine_t s_engine;        /* perception task only */
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile uint32_t s_queue_drops;
static uint32_t s_posted;
#if CONFIG_MAO_PERCEPT_LOG
static volatile bool s_log = true;
#else
static volatile bool s_log = false;
#endif
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static mao_perception_status_t s_status;

/* Survives deep sleep: how long nobody had been around when MAO fell asleep. */
static RTC_DATA_ATTR uint32_t s_rtc_magic;
static RTC_DATA_ATTR uint32_t s_rtc_absent_ms;
static bool s_absent_valid;         /* the RTC record was there at boot ... */
static uint32_t s_absent_at_sleep_ms;  /* ... with this */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------------ */
/* Producers (other tasks; never block)                                     */
/* ------------------------------------------------------------------------ */

static void enqueue(const item_t *it)
{
    if (!s_queue || xQueueSend(s_queue, it, 0) != pdTRUE) {
        s_queue_drops++;
    }
}

static void on_obs(const mao_obs_t *obs, void *ctx)
{
    (void)ctx;
    item_t it = { .kind = ITEM_OBS };
    it.obs = *obs;
    enqueue(&it);
}

static void on_event(const mao_event_t *ev, void *ctx)
{
    (void)ctx;
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW:
    case MAO_EVENT_INPUT_PRESS:
    case MAO_EVENT_USB_CONNECTED:
    case MAO_EVENT_USB_DISCONNECTED:
    case MAO_EVENT_BATTERY_LOW:
    case MAO_EVENT_BATTERY_CRITICAL: {
        item_t it = { .kind = ITEM_EVENT };
        it.ev.type = ev->type;
        it.ev.value = ev->value;
        it.ev.t_ms = ev->time_ms;
        enqueue(&it);
        break;
    }
    default:
        break;
    }
}

static void on_ir(const mao_ir_frame_t *frame, void *ctx)
{
    (void)ctx;
    if (frame->echo) {
        return;     /* MAO hearing itself */
    }
    item_t it = { .kind = ITEM_IR };
    it.ir.command = frame->command;
    it.ir.repeat = frame->repeat;
    it.ir.t_ms = now_ms();
    enqueue(&it);
}

/* ------------------------------------------------------------------------ */
/* Task                                                                     */
/* ------------------------------------------------------------------------ */

static void feed_obs(const mao_obs_t *o)
{
    switch (o->kind) {
    case MAO_OBS_IMU: {
        const pe_imu_t imu = {
            .t_ms = o->time_ms,
            .ax = o->imu.accel_g.x,
            .ay = o->imu.accel_g.y,
            .az = o->imu.accel_g.z,
            /* An observation with an all-zero vector came from a powered-
             * down accelerometer: events only. */
            .accel_valid = o->imu.accel_g.x != 0.0f || o->imu.accel_g.y != 0.0f || o->imu.accel_g.z != 0.0f,
            .events = o->imu.events,   /* MAO_IMU_EV_* == PE_IMU_* */
        };
        pe_feed_imu(&s_engine, &imu);
        break;
    }
    case MAO_OBS_TOF: {
        const pe_tof_t tof = {
            .t_ms = o->time_ms,
            .distance_mm = o->tof.distance_mm,
            .status = o->tof.range_status,
            .threshold = o->tof.threshold,
        };
        pe_feed_tof(&s_engine, &tof);
        break;
    }
    default:
        break;
    }
}

static void feed_event(const item_t *it)
{
    const uint32_t t = it->ev.t_ms;
    switch (it->ev.type) {
    case MAO_EVENT_INPUT_CW:
        pe_feed_dial(&s_engine, t, it->ev.value);
        break;
    case MAO_EVENT_INPUT_CCW:
        pe_feed_dial(&s_engine, t, -it->ev.value);
        break;
    case MAO_EVENT_INPUT_PRESS:
        pe_feed_press(&s_engine, t);
        break;
    case MAO_EVENT_USB_CONNECTED:
    case MAO_EVENT_USB_DISCONNECTED:
        pe_feed_usb(&s_engine, t, it->ev.type == MAO_EVENT_USB_CONNECTED);
        break;
    case MAO_EVENT_BATTERY_LOW:
        pe_feed_battery(&s_engine, t, 1);
        break;
    case MAO_EVENT_BATTERY_CRITICAL:
        pe_feed_battery(&s_engine, t, 2);
        break;
    default:
        break;
    }
}

static void self_gates(uint32_t now)
{
    /* MAO's own sounds and vibrations are not news about the world. */
    const uint32_t sound = mao_audio_busy_until_ms();
    if (sound && (int32_t)(sound - now) > 0) {
        pe_note_self_sound(&s_engine, sound);
    }
    const uint32_t buzz = mao_haptics_busy_until_ms();
    if (buzz && (int32_t)(buzz - now) > 0) {
        pe_note_self_motion(&s_engine, buzz);
    }
}

static void publish(void)
{
    pe_out_t o;
    while (pe_pop(&s_engine, &o)) {
        if (o.percept == MAO_PERCEPT_FIDDLING_ESCALATION) {
            continue;    /* mao_fiddle is MAO's one fiddling detector (see the top of this file) */
        }
        const int32_t v = mao_percept_pack(o.percept, o.confidence, o.detail);
        if (mao_event_post(MAO_EVENT_PERCEPT, v) == ESP_OK) {
            s_posted++;
        } else {
            s_queue_drops++;
        }
        if (s_log) {
            const bool zone = o.percept == MAO_PERCEPT_TOUCH || o.percept == MAO_PERCEPT_TOUCH_HOLD ||
                              o.percept == MAO_PERCEPT_TOUCH_REPEAT;
            ESP_LOGI(TAG, "%s%s%s (confidence %u, detail %u)", mao_percept_name(o.percept), zone ? " " : "",
                     zone ? mao_percept_zone_name(o.detail) : "", o.confidence, o.detail);
        }
    }
}

static void snapshot(uint32_t now)
{
    const mao_perception_status_t st = {
        .senses = s_engine.senses,
        .fiddle_level = pe_fiddle_level(&s_engine),
        .fiddle_score = pe_fiddle_score(&s_engine),
        .held = pe_is_held(&s_engine),
        .near = pe_is_near(&s_engine),
        .ambient_dbfs = pe_ambient_dbfs(&s_engine),
        .absent_s = pe_absence_ms(&s_engine, now) / 1000u,
        .posted = s_posted,
        .dropped = s_engine.dropped + s_queue_drops,
    };
    portENTER_CRITICAL(&s_status_lock);
    s_status = st;
    portEXIT_CRITICAL(&s_status_lock);
}

static void perception_task(void *arg)
{
    (void)arg;
    uint32_t last_tick = now_ms();
    for (;;) {
        item_t it;
        const bool got = xQueueReceive(s_queue, &it, pdMS_TO_TICKS(TICK_MS)) == pdTRUE;
        const uint32_t now = now_ms();
        self_gates(now);
        if (got) {
            switch (it.kind) {
            case ITEM_OBS:   feed_obs(&it.obs); break;
            case ITEM_EVENT: feed_event(&it); break;
            case ITEM_IR:    pe_feed_ir(&s_engine, it.ir.t_ms, it.ir.command, it.ir.repeat); break;
            case ITEM_REST:  pe_set_power(&s_engine, it.ev.value ? PE_POWER_DROWSY : PE_POWER_ACTIVE); break;
            case ITEM_WOKE:
                /* nobody was here while MAO slept either */
                pe_set_absence(&s_engine, (uint32_t)it.ev.value);
                break;
            default: break;
            }
        }
        if (now - last_tick >= TICK_MS) {
            last_tick = now;
            pe_tick(&s_engine, now);
            snapshot(now);
        }
        publish();
    }
}

/* ------------------------------------------------------------------------ */
/* Rest: keep "how long nobody was here" across deep sleep                  */
/* ------------------------------------------------------------------------ */

void mao_perception_rest(bool resting)
{
    if (!s_queue) {
        return;
    }
    item_t it = { .kind = ITEM_REST };
    it.ev.value = resting;
    it.ev.t_ms = now_ms();
    enqueue(&it);
}

void mao_perception_deep_sleep(void)
{
    if (!s_queue) {
        return;
    }
    mao_perception_status_t st;
    mao_perception_get_status(&st);
    s_rtc_absent_ms = st.absent_s * 1000u;
    s_rtc_magic = RTC_MAGIC;
}

void mao_perception_woke(uint64_t slept_ms)
{
    if (!s_queue || !s_absent_valid) {
        return;
    }
    const uint64_t absent = (uint64_t)s_absent_at_sleep_ms + slept_ms;
    item_t it = { .kind = ITEM_WOKE };
    it.ev.value = absent > INT32_MAX ? INT32_MAX : (int32_t)absent;
    it.ev.t_ms = now_ms();
    enqueue(&it);
}

/* ------------------------------------------------------------------------ */
/* Public                                                                   */
/* ------------------------------------------------------------------------ */

void mao_perception_get_status(mao_perception_status_t *out)
{
    if (out) {
        portENTER_CRITICAL(&s_status_lock);
        *out = s_status;
        portEXIT_CRITICAL(&s_status_lock);
    }
}

#if CONFIG_MAO_DEV_CONSOLE

static void devcmd_percept(char *arg)
{
    const char *args = arg ? arg : "";
    if (strcmp(args, "log on") == 0 || strcmp(args, "log off") == 0) {
        s_log = args[5] == 'n';
        ESP_LOGI(TAG, "dev: percept log %s", s_log ? "on" : "off");
        return;
    }
    if (strncmp(args, "inject ", 7) == 0) {
        /* inject <NAME> [detail] [confidence]: exercise the application's
         * reactions without the hardware (also on the LCDkit). */
        char name[32];
        int detail = 0, conf = 80;
        const int n = sscanf(args + 7, "%31s %d %d", name, &detail, &conf);
        for (int p = 1; n >= 1 && p < MAO_PERCEPT_COUNT; p++) {
            if (strcasecmp(name, mao_percept_name((mao_percept_t)p)) == 0) {
                mao_event_post(MAO_EVENT_PERCEPT, mao_percept_pack((mao_percept_t)p, (uint8_t)conf, (uint16_t)detail));
                ESP_LOGI(TAG, "dev: injected %s detail %d confidence %d", mao_percept_name((mao_percept_t)p),
                         detail, conf);
                return;
            }
        }
        ESP_LOGW(TAG, "dev: unknown percept '%s'", n >= 1 ? name : "");
        return;
    }
    mao_perception_status_t st;
    mao_perception_get_status(&st);
    ESP_LOGI(TAG, "senses 0x%02" PRIx32 " (imu %d tof %d); posted %" PRIu32 ", dropped %" PRIu32,
             st.senses, !!(st.senses & PE_SENSE_IMU), !!(st.senses & PE_SENSE_TOF), st.posted, st.dropped);
    ESP_LOGI(TAG, "held %d, near %d, nobody for %" PRIu32 " s (engine fiddling score %.2f, not posted)",
             st.held, st.near, st.absent_s, (double)st.fiddle_score);
}

static void register_devcmds(void)
{
    mao_devcmd_register("percept", devcmd_percept);   /* percept | log on|off | inject <NAME> [detail] [confidence] */
}

#else

static void register_devcmds(void)
{
}

#endif

static uint32_t senses_from_sense(uint32_t available)
{
    uint32_t s = 0;
    s |= (available & MAO_OBS_MASK(MAO_OBS_IMU)) ? PE_SENSE_IMU : 0;
    s |= (available & MAO_OBS_MASK(MAO_OBS_TOF)) ? PE_SENSE_TOF : 0;
    return s;
}

esp_err_t mao_perception_init(void)
{
#if CONFIG_MAO_BOARD_LCDKIT
    /* Nothing of this on the LCDkit, and none of its code or RAM in the
     * image (the rest of this function is dead there and dropped). */
    return ESP_ERR_NOT_SUPPORTED;
#endif
    pe_config_t cfg;
    pe_config_default(&cfg);
    cfg.near_mm = CONFIG_MAO_PERCEPT_NEAR_MM;
    cfg.near_exit_mm = (uint16_t)(CONFIG_MAO_PERCEPT_NEAR_MM + CONFIG_MAO_PERCEPT_NEAR_MM / 2);
    cfg.quiet_ms = (uint32_t)CONFIG_MAO_PERCEPT_QUIET_ROOM_S * 1000u;
    cfg.dark_ms = (uint32_t)CONFIG_MAO_PERCEPT_DARK_ROOM_S * 1000u;
    cfg.long_absence_ms = (uint32_t)CONFIG_MAO_PERCEPT_LONG_ABSENCE_MIN * 60u * 1000u;
#if CONFIG_MAO_PERCEPT_IMU_Z_DOWN
    cfg.upright_sign = -1.0f;
#endif

    /* Boards without sensors (LCDkit) do not run perception: on the dial and
     * the press alone it would only invent LONG_ABSENCE, and the LCDkit's
     * M4.1 behaviour stays as it was. */
    const uint32_t senses = senses_from_sense(mao_sense_available());
    if (!senses) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const uint32_t now = now_ms();
    pe_init(&s_engine, &cfg, senses, now);
    pe_set_usb_initial(&s_engine, mao_battery_usb_present());

    /* Back from deep sleep: mao_app tells how long (mao_perception_woke). */
    s_absent_valid = s_rtc_magic == RTC_MAGIC;   /* taken by mao_perception_woke() */
    s_absent_at_sleep_ms = s_rtc_absent_ms;
    s_rtc_magic = 0;

    s_queue = xQueueCreate(QUEUE_LEN, sizeof(item_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue");
    if (xTaskCreate(perception_task, "mao_percept", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(mao_event_subscribe(on_event, NULL), TAG, "subscribe");
    if (senses) {
        ESP_RETURN_ON_ERROR(mao_sense_subscribe(MAO_OBS_ALL, 0, on_obs, NULL), TAG, "sense");
    }
    if (mao_ir_is_enabled()) {
        mao_ir_set_rx_callback(on_ir, NULL);
    }
    register_devcmds();
    snapshot(now);
    ESP_LOGI(TAG, "perception: %s%s+ dial, press, battery%s; near %d mm, long absence %d min",
             (senses & PE_SENSE_IMU) ? "imu " : "", (senses & PE_SENSE_TOF) ? "tof " : "",
             mao_ir_is_enabled() ? ", ir" : "", CONFIG_MAO_PERCEPT_NEAR_MM, CONFIG_MAO_PERCEPT_LONG_ABSENCE_MIN);
    return ESP_OK;
}
