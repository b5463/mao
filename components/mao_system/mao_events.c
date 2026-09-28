#include "mao_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

#define MAO_EVENT_QUEUE_LEN     16
#define MAO_EVENT_MAX_HANDLERS  8

static const char *TAG = "MAO_SYSTEM";

typedef struct {
    mao_event_handler_t fn;
    void *ctx;
} handler_slot_t;

static QueueHandle_t s_queue;
static handler_slot_t s_handlers[MAO_EVENT_MAX_HANDLERS];
static size_t s_handler_count;
static volatile uint32_t s_dropped;

esp_err_t mao_events_init(void)
{
    if (s_queue) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(MAO_EVENT_QUEUE_LEN, sizeof(mao_event_t));
    return s_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t mao_event_subscribe(mao_event_handler_t handler, void *ctx)
{
    if (!handler) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_handler_count >= MAO_EVENT_MAX_HANDLERS) {
        return ESP_ERR_NO_MEM;
    }
    /* Subscriptions happen during init, before the dispatcher starts. */
    s_handlers[s_handler_count].fn = handler;
    s_handlers[s_handler_count].ctx = ctx;
    s_handler_count++;
    return ESP_OK;
}

esp_err_t mao_event_post(mao_event_type_t type, int32_t value)
{
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    const mao_event_t ev = {
        .type = type,
        .value = value,
        .time_ms = (uint32_t)(esp_timer_get_time() / 1000),
    };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        s_dropped++;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void mao_events_dispatch_next(void)
{
    mao_event_t ev;
    if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < s_handler_count; i++) {
        s_handlers[i].fn(&ev, s_handlers[i].ctx);
    }
}

uint32_t mao_events_dropped(void)
{
    return s_dropped;
}

const char *mao_event_name(mao_event_type_t type)
{
    switch (type) {
    case MAO_EVENT_NONE:               return "NONE";
    case MAO_EVENT_INPUT_CW:           return "CW";
    case MAO_EVENT_INPUT_CCW:          return "CCW";
    case MAO_EVENT_INPUT_PRESS:        return "PRESS";
    case MAO_EVENT_INPUT_RELEASE:      return "RELEASE";
    case MAO_EVENT_INPUT_CLICK:        return "CLICK";
    case MAO_EVENT_INPUT_LONG_PRESS:   return "LONG_PRESS";
    case MAO_EVENT_INPUT_DOUBLE_CLICK: return "DOUBLE_CLICK";
    case MAO_EVENT_SYSTEM_READY:       return "SYSTEM_READY";
    case MAO_EVENT_IDLE_TIMEOUT:       return "IDLE_TIMEOUT";
    case MAO_EVENT_DEV_COMMAND:        return "DEV_COMMAND";
    case MAO_EVENT_DEVICE_FOUND:       return "DEVICE_FOUND";
    case MAO_EVENT_DEVICE_LOST:        return "DEVICE_LOST";
    case MAO_EVENT_DEVICE_CHANGED:     return "DEVICE_CHANGED";
    case MAO_EVENT_REL_CHANGED:        return "REL_CHANGED";
    case MAO_EVENT_LINK_CHANGED:       return "LINK_CHANGED";
    case MAO_EVENT_PAGE_IDLE:          return "PAGE_IDLE";
    default:                           break;
    }
    ESP_LOGD(TAG, "unknown event %d", (int)type);
    return "?";
}
