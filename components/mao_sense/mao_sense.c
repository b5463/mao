/*
 * mao_sense core: sensor bring-up, the sense task, subscriptions and the
 * sense level (awake / rest / deep / off, set by mao_app's power ladder).
 *
 * One task ("mao_sense") owns the I2C sensors. It wakes on the IMU / ToF
 * interrupt lines or when a periodic sample is due, and publishes
 * observations:
 *              AWAKE    REST / DEEP / OFF
 *   IMU        100 ms   - (the chip sleeps; WoM latches INT1)
 *   ToF        200 ms   off
 * Events (IMU flags, ToF threshold) are published immediately.
 */
#include "mao_sense.h"
#include "mao_sense_priv.h"

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "mao_battery.h"
#include "mao_board.h"
#include "mao_system.h"

static const char *TAG = "MAO_SENSE";

#define TASK_STACK          4096
#define TASK_PRIO           4
#define MAX_SUBSCRIBERS     6
#define IMU_PERIOD_MS       100
#define TOF_WATCHDOG_EXTRA_MS 100       /* poll if a ToF interrupt edge was missed */

typedef struct {
    uint32_t kinds;
    uint32_t min_interval_ms;
    mao_sense_cb_t cb;
    void *ctx;
    QueueHandle_t queue;
    uint32_t last_ms[MAO_OBS_KIND_COUNT];
    bool sent[MAO_OBS_KIND_COUNT];
} subscriber_t;

static subscriber_t s_subs[MAX_SUBSCRIBERS];
static size_t s_sub_count;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static mao_obs_t s_latest[MAO_OBS_KIND_COUNT];
static bool s_have[MAO_OBS_KIND_COUNT];
static uint32_t s_available;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_dev_lock;     /* sense task vs the power ladder / console / charge limit */
static volatile mao_sense_level_t s_level = MAO_SENSE_AWAKE;

const char *mao_obs_kind_name(mao_obs_kind_t kind)
{
    switch (kind) {
    case MAO_OBS_IMU: return "imu";
    case MAO_OBS_TOF: return "tof";
    default:          return "?";
    }
}

uint32_t sense_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool available(mao_obs_kind_t kind)
{
    return (s_available & MAO_OBS_MASK(kind)) != 0;
}

/* ------------------------------------------------------------------------ */
/* Publish / subscribe                                                      */
/* ------------------------------------------------------------------------ */

void sense_publish(const mao_obs_t *obs)
{
    const mao_obs_kind_t k = obs->kind;
    portENTER_CRITICAL(&s_lock);
    s_latest[k] = *obs;
    s_have[k] = true;
    const size_t count = s_sub_count;
    portEXIT_CRITICAL(&s_lock);

    /* Every kind is published by the sense task only, so the per-kind rate
     * state needs no lock. */
    for (size_t i = 0; i < count; i++) {
        subscriber_t *s = &s_subs[i];
        if (!(s->kinds & MAO_OBS_MASK(k))) {
            continue;
        }
        if (!obs->event && s->sent[k] && obs->time_ms - s->last_ms[k] < s->min_interval_ms) {
            continue;
        }
        s->sent[k] = true;
        s->last_ms[k] = obs->time_ms;
        if (s->cb) {
            s->cb(obs, s->ctx);
        } else if (s->queue) {
            xQueueSend(s->queue, obs, 0);
        }
    }
}

static esp_err_t add_subscriber(const subscriber_t *sub)
{
    ESP_RETURN_ON_FALSE(sub->kinds & MAO_OBS_ALL, ESP_ERR_INVALID_ARG, TAG, "no kinds");
    esp_err_t err = ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&s_lock);
    if (s_sub_count < MAX_SUBSCRIBERS) {
        s_subs[s_sub_count] = *sub;
        s_sub_count++;
        err = ESP_OK;
    }
    portEXIT_CRITICAL(&s_lock);
    return err;
}

esp_err_t mao_sense_subscribe(uint32_t kinds, uint32_t min_interval_ms, mao_sense_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(cb, ESP_ERR_INVALID_ARG, TAG, "no callback");
    const subscriber_t sub = { .kinds = kinds, .min_interval_ms = min_interval_ms, .cb = cb, .ctx = ctx };
    return add_subscriber(&sub);
}

esp_err_t mao_sense_subscribe_queue(uint32_t kinds, uint32_t min_interval_ms, QueueHandle_t queue)
{
    ESP_RETURN_ON_FALSE(queue, ESP_ERR_INVALID_ARG, TAG, "no queue");
    const subscriber_t sub = { .kinds = kinds, .min_interval_ms = min_interval_ms, .queue = queue };
    return add_subscriber(&sub);
}

bool mao_sense_latest(mao_obs_kind_t kind, mao_obs_t *out)
{
    if (kind >= MAO_OBS_KIND_COUNT || !out) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    const bool have = s_have[kind];
    if (have) {
        *out = s_latest[kind];
    }
    portEXIT_CRITICAL(&s_lock);
    return have;
}

uint32_t mao_sense_available(void)
{
    return s_available;
}

esp_err_t mao_sense_imu_gyro(bool on)
{
    ESP_RETURN_ON_FALSE(available(MAO_OBS_IMU), ESP_ERR_NOT_SUPPORTED, TAG, "no imu");
    xSemaphoreTake(s_dev_lock, portMAX_DELAY);
    const esp_err_t err = sense_imu_gyro(on);
    xSemaphoreGive(s_dev_lock);
    return err;
}

esp_err_t mao_sense_imu_temperature(float *celsius)
{
    ESP_RETURN_ON_FALSE(celsius, ESP_ERR_INVALID_ARG, TAG, "bad args");
    if (!available(MAO_OBS_IMU) || !s_dev_lock) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_dev_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = sense_imu_temperature(celsius);
    xSemaphoreGive(s_dev_lock);
    return err;
}

esp_err_t mao_sense_identify(mao_obs_kind_t kind, uint32_t *id)
{
    ESP_RETURN_ON_FALSE(id, ESP_ERR_INVALID_ARG, TAG, "bad args");
    if (kind >= MAO_OBS_KIND_COUNT || !available(kind) || !s_dev_lock) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_dev_lock, portMAX_DELAY);
    switch (kind) {
    case MAO_OBS_IMU: err = sense_imu_id(id); break;
    case MAO_OBS_TOF: err = sense_tof_id(id); break;
    default: break;
    }
    xSemaphoreGive(s_dev_lock);
    return err;
}

esp_err_t mao_sense_set_level(mao_sense_level_t level)
{
    if (!s_dev_lock) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    xSemaphoreTake(s_dev_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (available(MAO_OBS_IMU)) {
        err = sense_imu_mode(level);
    }
    if (available(MAO_OBS_TOF)) {
        const esp_err_t e = sense_tof_mode(level);
        err = err == ESP_OK ? e : err;
    }
    s_level = level;
    xSemaphoreGive(s_dev_lock);
    if (s_task) {
        xTaskNotify(s_task, SENSE_BIT_MODE, eSetBits);
    }
    return err;
}

/* ------------------------------------------------------------------------ */
/* Task                                                                     */
/* ------------------------------------------------------------------------ */

static void line_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    if (s_task) {
        xTaskNotifyFromISR(s_task, (uint32_t)(uintptr_t)arg, eSetBits, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static bool due(uint32_t period, uint32_t next, uint32_t now)
{
    return period > 0 && (int32_t)(now - next) >= 0;
}

static void sample_imu(bool interrupt, bool periodic)
{
    mao_obs_t obs = { .kind = MAO_OBS_IMU, .time_ms = sense_now_ms() };
    if (sense_imu_sample(&obs.imu) != ESP_OK) {
        return;
    }
    obs.event = obs.imu.events != 0;
    if (obs.event || periodic || interrupt) {
        sense_publish(&obs);
    }
}

static void sample_tof(void)
{
    mao_obs_t obs = { .kind = MAO_OBS_TOF, .time_ms = sense_now_ms() };
    bool ready = false;
    if (sense_tof_read(&obs.tof, &ready) == ESP_OK && ready) {
        obs.event = obs.tof.threshold;
        sense_publish(&obs);
    }
}

static void sense_task(void *arg)
{
    (void)arg;
    uint32_t now = sense_now_ms();
    uint32_t next_imu = now, next_tof = now;
    for (;;) {
        const bool awake = s_level == MAO_SENSE_AWAKE;
        const uint32_t p_imu = available(MAO_OBS_IMU) && awake ? IMU_PERIOD_MS : 0;
        const uint32_t p_tof = available(MAO_OBS_TOF) && awake ? 2 * sense_tof_period_ms() + TOF_WATCHDOG_EXTRA_MS : 0;

        /* Sleep until the earliest periodic deadline (or an interrupt). */
        now = sense_now_ms();
        int32_t wait_ms = INT32_MAX;
        const uint32_t periods[] = { p_imu, p_tof };
        const uint32_t nexts[] = { next_imu, next_tof };
        for (size_t i = 0; i < 2; i++) {
            if (periods[i]) {
                const int32_t left = (int32_t)(nexts[i] - now);
                wait_ms = left < wait_ms ? left : wait_ms;
            }
        }
        uint32_t bits = 0;
        const TickType_t ticks = wait_ms == INT32_MAX ? portMAX_DELAY
                                 : (wait_ms <= 0 ? 0 : pdMS_TO_TICKS(wait_ms) + 1);
        xTaskNotifyWait(0, UINT32_MAX, &bits, ticks);

        now = sense_now_ms();
        if (bits & SENSE_BIT_MODE) {
            next_imu = next_tof = now;      /* new level: sample once now */
        }
        xSemaphoreTake(s_dev_lock, portMAX_DELAY);
        if (available(MAO_OBS_IMU)) {
            const bool periodic = due(p_imu, next_imu, now);
            if ((bits & SENSE_BIT_IMU) || periodic) {
                sample_imu((bits & SENSE_BIT_IMU) != 0, periodic);
            }
            if (periodic) {
                next_imu = now + p_imu;
            }
        }
        if (available(MAO_OBS_TOF) && ((bits & SENSE_BIT_TOF) || due(p_tof, next_tof, now))) {
            sample_tof();
            next_tof = now + p_tof;
        }
        xSemaphoreGive(s_dev_lock);
    }
}

/* ------------------------------------------------------------------------ */
/* Development console                                                      */
/* ------------------------------------------------------------------------ */

#if CONFIG_MAO_DEV_CONSOLE

static const char *orientation_name(mao_orientation_t o)
{
    static const char *const kNames[] = { "?", "+X up", "-X up", "+Y up", "-Y up", "+Z up", "-Z up" };
    return (unsigned)o < sizeof(kNames) / sizeof(kNames[0]) ? kNames[o] : "?";
}

static void devcmd_sense(char *arg)
{
    (void)arg;
    const uint32_t now = sense_now_ms();
    for (int k = 0; k < MAO_OBS_KIND_COUNT; k++) {
        if (!available((mao_obs_kind_t)k)) {
            ESP_LOGI(TAG, "%-3s: not available", mao_obs_kind_name((mao_obs_kind_t)k));
            continue;
        }
        mao_obs_t o;
        if (!mao_sense_latest((mao_obs_kind_t)k, &o)) {
            ESP_LOGI(TAG, "%-3s: no observation yet", mao_obs_kind_name((mao_obs_kind_t)k));
            continue;
        }
        const uint32_t age = now - o.time_ms;
        if (o.kind == MAO_OBS_IMU) {
            ESP_LOGI(TAG, "imu: %" PRIu32 " ms ago, accel %.3f %.3f %.3f g, gyro %s%.1f %.1f %.1f dps, "
                     "events 0x%02x, %s (ICM-42670-P)", age, (double)o.imu.accel_g.x, (double)o.imu.accel_g.y,
                     (double)o.imu.accel_g.z, o.imu.gyro_valid ? "" : "(off) ", (double)o.imu.gyro_dps.x,
                     (double)o.imu.gyro_dps.y, (double)o.imu.gyro_dps.z, o.imu.events,
                     orientation_name(o.imu.orientation));
        } else {
            ESP_LOGI(TAG, "tof: %" PRIu32 " ms ago, %u mm, status %u, signal %u kcps, sigma %u mm%s", age,
                     o.tof.distance_mm, o.tof.range_status, o.tof.signal_kcps, o.tof.sigma_mm,
                     o.tof.threshold ? ", threshold" : "");
        }
    }
    float t = 0.0f;
    if (mao_sense_imu_temperature(&t) == ESP_OK) {
        ESP_LOGI(TAG, "imu die temperature %.1f C", (double)t);
    }
}

static void register_devcmds(void)
{
    mao_devcmd_register("sense", devcmd_sense);
}

#else

static void register_devcmds(void)
{
}

#endif

/* ------------------------------------------------------------------------ */
/* Init                                                                     */
/* ------------------------------------------------------------------------ */

static esp_err_t install_isr(mao_board_irq_t irq, uint32_t bit)
{
    mao_board_irq_desc_t d;
    ESP_RETURN_ON_ERROR(mao_board_irq_get(irq, &d), TAG, "irq");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(d.gpio, d.active_low ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE),
                        TAG, "intr type");
    return gpio_isr_handler_add(d.gpio, line_isr, (void *)(uintptr_t)bit);
}

static void bring_up(const char *name, bool fitted, esp_err_t (*init)(void), mao_obs_kind_t kind)
{
    if (fitted && mao_system_report(name, init()) == ESP_OK) {
        s_available |= MAO_OBS_MASK(kind);
    }
}

esp_err_t mao_sense_init(void)
{
    mao_board_caps_t caps;
    mao_board_get_caps(&caps);
    if (!caps.imu && !caps.tof) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_dev_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_dev_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    bring_up("IMU", caps.imu, sense_imu_init, MAO_OBS_IMU);
    bring_up("proximity", caps.tof, sense_tof_init, MAO_OBS_TOF);

    if (xTaskCreate(sense_task, "mao_sense", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "isr service");
    if (available(MAO_OBS_IMU)) {
        ESP_RETURN_ON_ERROR(install_isr(MAO_IRQ_IMU_INT1, SENSE_BIT_IMU), TAG, "imu int1");
        /* The IMU die is the board thermometer for the charge limit. */
        mao_battery_set_temp_source(mao_sense_imu_temperature);
    }
    if (available(MAO_OBS_TOF)) {
        ESP_RETURN_ON_ERROR(install_isr(MAO_IRQ_TOF, SENSE_BIT_TOF), TAG, "tof int");
    }
    register_devcmds();
    ESP_LOGI(TAG, "sense: %s%s", available(MAO_OBS_IMU) ? "imu " : "", available(MAO_OBS_TOF) ? "tof" : "");
    return ESP_OK;
}
