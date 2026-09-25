/*
 * MAO internal event bus.
 *
 * A single FreeRTOS queue drained by one dispatcher task (owned by
 * mao_system). Producers (input driver, system) post small fixed-size events;
 * subscribers are plain callbacks invoked sequentially in the dispatcher task.
 * Nothing here knows about LVGL, audio or LEDs.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_EVENT_NONE = 0,

    /* Input. For CW/CCW, value = number of detents (>= 1, coalesced). */
    MAO_EVENT_INPUT_CW,
    MAO_EVENT_INPUT_CCW,
    MAO_EVENT_INPUT_PRESS,
    MAO_EVENT_INPUT_RELEASE,
    MAO_EVENT_INPUT_CLICK,
    MAO_EVENT_INPUT_LONG_PRESS,
    MAO_EVENT_INPUT_DOUBLE_CLICK,

    /* System. */
    MAO_EVENT_SYSTEM_READY,
    MAO_EVENT_IDLE_TIMEOUT,      /* no input for the configured idle period */
    MAO_EVENT_DEV_COMMAND,       /* value = mao_devcmd_t (development console) */

    /* External ODD devices. value = registry index. */
    MAO_EVENT_DEVICE_FOUND,      /* new, or back online after being offline */
    MAO_EVENT_DEVICE_LOST,       /* went offline */
    MAO_EVENT_DEVICE_CHANGED,    /* description, state or link status changed */
    MAO_EVENT_TRANSFER_STEP,     /* transfer state machine timer; value = transfer id */
    MAO_EVENT_DEVICE_PROBED,     /* a reachability probe was answered; value = registry index */
    MAO_EVENT_ACTION_UPDATE,     /* action transaction changed; value = (index << 4) | mao_action_state_t */
    MAO_EVENT_UI_SETTLE,         /* one-shot app timer: feedback presentation may retire */

    MAO_EVENT_COUNT,
} mao_event_type_t;

typedef struct {
    mao_event_type_t type;
    int32_t value;
    uint32_t time_ms;   /* ms since boot when the event was generated */
} mao_event_t;

typedef void (*mao_event_handler_t)(const mao_event_t *event, void *ctx);

/* Create the queue. Called by mao_system_init(). */
esp_err_t mao_events_init(void);

/* Register a handler for all events. Handlers run in the dispatcher task and
 * must not block for long. Returns ESP_ERR_NO_MEM when the table is full. */
esp_err_t mao_event_subscribe(mao_event_handler_t handler, void *ctx);

/* Non-blocking post from task context. Returns ESP_ERR_TIMEOUT if the queue
 * is full (the event is dropped and counted). */
esp_err_t mao_event_post(mao_event_type_t type, int32_t value);

/* Block until an event arrives and dispatch it to all subscribers.
 * Used by the mao_system dispatcher task. */
void mao_events_dispatch_next(void);

/* Number of events dropped because the queue was full. */
uint32_t mao_events_dropped(void);

const char *mao_event_name(mao_event_type_t type);

#ifdef __cplusplus
}
#endif
