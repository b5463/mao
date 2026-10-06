#include "mao_system.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "sdkconfig.h"
#include "mao_settings.h"

static const char *TAG = "MAO_SYSTEM";

#define MAO_DISPATCH_TASK_STACK   5120   /* dev snapshot streams from here */
#define MAO_DISPATCH_TASK_PRIO    4
#define MAO_HEALTH_PERIOD_US      (60 * 1000 * 1000)

#define MAO_REPORT_MAX            40

typedef struct {
    const char *name;
    esp_err_t err;
} report_t;

static report_t s_reports[MAO_REPORT_MAX];
static size_t s_report_count;
static TaskHandle_t s_dispatch_task;
static esp_timer_handle_t s_health_timer;
static esp_timer_handle_t s_idle_timer;
static bool s_all_ok = true;

static void idle_timer_cb(void *arg)
{
    (void)arg;
    mao_event_post(MAO_EVENT_IDLE_TIMEOUT, 0);
}

void mao_system_idle_kick(uint32_t timeout_ms)
{
    if (!s_idle_timer) {
        return;
    }
    esp_timer_stop(s_idle_timer);
    if (timeout_ms > 0) {
        esp_timer_start_once(s_idle_timer, (uint64_t)timeout_ms * 1000);
    }
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:    return "power-on";
    case ESP_RST_EXT:        return "external pin";
    case ESP_RST_SW:         return "software";
    case ESP_RST_PANIC:      return "PANIC";
    case ESP_RST_INT_WDT:    return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT:   return "TASK WATCHDOG";
    case ESP_RST_WDT:        return "WATCHDOG";
    case ESP_RST_DEEPSLEEP:  return "deep-sleep wake";
    case ESP_RST_BROWNOUT:   return "BROWNOUT";
    case ESP_RST_USB:        return "USB";
    case ESP_RST_JTAG:       return "JTAG";
    case ESP_RST_CPU_LOCKUP: return "CPU LOCKUP";
    default:                 return "unknown";
    }
}

static const char *chip_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32S3: return "ESP32-S3";
    default:           return CONFIG_IDF_TARGET;
    }
}

void mao_system_log_heap(const char *tag, const char *label)
{
    ESP_LOGI(tag, "heap %-22s free=%" PRIu32 " min=%" PRIu32 " largest=%u dma_free=%u",
             label,
             esp_get_free_heap_size(),
             esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
}

esp_err_t mao_system_init(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "MAO");
    ESP_LOGI(TAG, "firmware: %s (%s, IDF %s)", MAO_FIRMWARE_STAGE, app->version, app->idf_ver);
    ESP_LOGI(TAG, "chip: %s rev v%d.%d, %d core(s)",
             chip_name(chip.model), chip.revision / 100, chip.revision % 100, chip.cores);
    ESP_LOGI(TAG, "flash: %" PRIu32 " MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "reset reason: %s", reset_reason_str(esp_reset_reason()));
    mao_system_log_heap(TAG, "at boot");

    esp_err_t err = mao_system_report("events", mao_events_init());

    const esp_timer_create_args_t idle_args = {
        .callback = idle_timer_cb,
        .name = "mao_idle",
    };
    esp_timer_create(&idle_args, &s_idle_timer);

    /* Settings failure is not fatal: MAO runs on defaults. */
    mao_system_report("settings", mao_settings_init());
    return err;
}

/* Reports happen during bring-up, in app_main and the init functions it
 * calls (one task), so the table needs no lock. */
static void record(const char *name, esp_err_t err)
{
    for (size_t i = 0; i < s_report_count; i++) {
        if (strcmp(s_reports[i].name, name) == 0) {
            s_reports[i].err = err;
            return;
        }
    }
    if (s_report_count < MAO_REPORT_MAX) {
        s_reports[s_report_count++] = (report_t) { .name = name, .err = err };
    }
}

esp_err_t mao_system_report_status(const char *name)
{
    for (size_t i = 0; name && i < s_report_count; i++) {
        if (strcmp(s_reports[i].name, name) == 0) {
            return s_reports[i].err;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t mao_system_report(const char *name, esp_err_t err)
{
    record(name, err);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[OK] %s", name);
    } else {
        s_all_ok = false;
        ESP_LOGE(TAG, "[!!] %s: %s", name, esp_err_to_name(err));
    }
    return err;
}

void mao_system_report_disabled(const char *name, const char *reason)
{
    record(name, ESP_ERR_NOT_SUPPORTED);
    ESP_LOGI(TAG, "[--] %s %s", name, reason);
}

esp_err_t mao_system_report_optional(const char *name, esp_err_t err)
{
    if (err == ESP_ERR_NOT_SUPPORTED) {
        mao_system_report_disabled(name, "not fitted");
        return ESP_OK;
    }
    return mao_system_report(name, err);
}

static void dispatch_task(void *arg)
{
    (void)arg;
    for (;;) {
        mao_events_dispatch_next();
    }
}

/* Tasks whose unused stack (high-water mark, bytes) is reported by health_cb. */
static const char *const kWatchedTasks[] = {
    "mao_dispatch", "mao_input", "mao_audio", "taskLVGL", "esp_timer", "mao_devcmd",
    "mao_devices", "wifi", "sys_evt", "mao_link",
};
/* Tasks only some boards run (A1): listed only when they exist. */
static const char *const kWatchedOptional[] = {
    "mao_battery", "mao_haptics", "mao_sense", "mao_ir", "mao_percept", "mao_selftest",
};

static void health_cb(void *arg)
{
    (void)arg;
    mao_system_log_heap(TAG, "health");

    char line[400];
    int n = 0;
    for (size_t i = 0; i < sizeof(kWatchedTasks) / sizeof(kWatchedTasks[0]) && n < (int)sizeof(line); i++) {
        TaskHandle_t h = xTaskGetHandle(kWatchedTasks[i]);
        n += snprintf(line + n, sizeof(line) - n, " %s=%u", kWatchedTasks[i],
                      h ? (unsigned)uxTaskGetStackHighWaterMark(h) : 0u);
    }
    for (size_t i = 0; i < sizeof(kWatchedOptional) / sizeof(kWatchedOptional[0]) && n < (int)sizeof(line); i++) {
        TaskHandle_t h = xTaskGetHandle(kWatchedOptional[i]);
        if (h) {
            n += snprintf(line + n, sizeof(line) - n, " %s=%u", kWatchedOptional[i],
                          (unsigned)uxTaskGetStackHighWaterMark(h));
        }
    }
    ESP_LOGI(TAG, "uptime=%" PRIu32 "s events_dropped=%" PRIu32 " stack_free:%s",
             (uint32_t)(esp_timer_get_time() / 1000000), mao_events_dropped(), line);
}

esp_err_t mao_system_start(void)
{
    if (xTaskCreate(dispatch_task, "mao_dispatch", MAO_DISPATCH_TASK_STACK, NULL,
                    MAO_DISPATCH_TASK_PRIO, &s_dispatch_task) != pdPASS) {
        return mao_system_report("dispatcher", ESP_ERR_NO_MEM);
    }

    const esp_timer_create_args_t args = {
        .callback = health_cb,
        .name = "mao_health",
    };
    if (esp_timer_create(&args, &s_health_timer) == ESP_OK) {
        esp_timer_start_periodic(s_health_timer, MAO_HEALTH_PERIOD_US);
    }

    mao_devcmd_start();
    mao_event_post(MAO_EVENT_SYSTEM_READY, 0);
    mao_system_log_heap(TAG, "after startup");
    if (s_all_ok) {
        ESP_LOGI(TAG, "MAO READY");
    } else {
        ESP_LOGW(TAG, "MAO READY (with subsystem errors, see above)");
    }
    return ESP_OK;
}
