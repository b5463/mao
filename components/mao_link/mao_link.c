/*
 * MAO link security glue (see mao_link.h).
 *
 * All public-key and key-derivation work runs on the "mao_link" task: never
 * in the ESP-NOW receive callback, the LVGL task or the console task
 * (X25519 needs several KB of stack and takes ~0.15 s per operation).
 */
#include "mao_link.h"

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mao_system.h"
#include "odd_link_selftest.h"

static const char *TAG = "MAO_LINK";

#define LINK_TASK_STACK 6144
#define LINK_TASK_PRIO  4
#define LINK_QUEUE_LEN  8

typedef enum { JOB_SELFTEST = 1, JOB_BENCH } job_t;

static QueueHandle_t s_jobs;
static TaskHandle_t s_task;

static void link_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint8_t job;
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (job == JOB_SELFTEST) {
            int total = 0;
            const int pass = odl_selftest(&total);
            ESP_LOGI(TAG, "link selftest: %d/%d (stack free %u B)", pass, total,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        } else if (job == JOB_BENCH) {
            odl_bench();
            ESP_LOGI(TAG, "bench done (stack free %u B)", (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

static void post(uint8_t job)
{
    if (s_jobs) {
        xQueueSend(s_jobs, &job, 0);
    }
}

#if CONFIG_MAO_DEV_CONSOLE
static void dev_command(char *arg)
{
    if (arg && strcmp(arg, "selftest") == 0) {
        post(JOB_SELFTEST);
    } else if (arg && strcmp(arg, "bench") == 0) {
        post(JOB_BENCH);
    } else {
        ESP_LOGW(TAG, "link selftest | bench");
    }
}
#endif

esp_err_t mao_link_init(void)
{
    s_jobs = xQueueCreate(LINK_QUEUE_LEN, sizeof(uint8_t));
    if (!s_jobs || xTaskCreate(link_task, "mao_link", LINK_TASK_STACK, NULL, LINK_TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_MAO_DEV_CONSOLE
    mao_devcmd_register("link", dev_command);
#endif
    return ESP_OK;
}
