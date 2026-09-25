/*
 * ODD BUS controller: registry, discovery, liveness and the command engine.
 *
 * Threading: the ESP-NOW receive callback only copies frames into `s_inbox`.
 * One task ("mao_devices") decodes them, owns the command state machine and
 * posts MAO events. The public registry snapshot (`pub`) is guarded by a
 * mutex so the app/UI can read it and write desired values from other tasks.
 *
 * Command model per capability: the UI value changes immediately
 * (optimistic). A SET_VALUE carrying the newest desired value is sent at most
 * every COALESCE_US; it is confirmed by an ACK with the same sequence number,
 * retried (same seq, so the device can recognise duplicates) up to
 * MAX_RETRIES times, then abandoned and counted. Repeated failures raise
 * `link_problem`; the value stays marked unconfirmed rather than pretending.
 */
#include "mao_devices.h"

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mao_events.h"
#include "mao_radio.h"
#include "odd_bus.h"

static const char *TAG = "MAO_DEVICES";

#define TASK_STACK            4096
#define TASK_PRIO             5
#define INBOX_LEN             12

#define COALESCE_US           (25 * 1000)
#define ACK_TIMEOUT_US        (40 * 1000)
#define MAX_RETRIES           2
#define LINK_PROBLEM_AFTER    2          /* consecutive abandoned commands */

#define DISCOVERY_ACTIVE_MS   1500
#define DISCOVERY_IDLE_MS     10000
#define OFFLINE_ACTIVE_MS     5000       /* > 3 missed active rounds */
#define OFFLINE_IDLE_MS       32000      /* > 3 missed idle rounds */
#define MODE_GRACE_MS         3000       /* after speeding up, let devices answer first */
#define DESCRIBE_RETRY_MS     1000

typedef enum { ITEM_RX = 0, ITEM_WAKE } item_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t mac[6];
    int8_t rssi;
    uint8_t len;
    uint8_t data[ODD_MAX_FRAME];
} inbox_item_t;

typedef struct {
    bool dirty;              /* desired value not yet sent */
    bool in_flight;
    uint16_t seq;
    int32_t sent_value;
    int64_t first_tx_us;
    int64_t last_tx_us;
    int64_t input_us;        /* user input represented by the in-flight value */
    int64_t latest_input_us;
    uint8_t retries;
} cmd_t;

typedef struct {
    bool used;
    mao_device_t pub;
    cmd_t cmd[ODD_MAX_CAPS];
    uint8_t failures;
    uint32_t describe_req_ms;
    volatile bool refresh_req;   /* reachability probe requested (any task) */
    bool probe_armed;            /* next direct answer posts MAO_EVENT_DEVICE_PROBED */
} entry_t;

static QueueHandle_t s_inbox;
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static entry_t s_dev[MAO_DEVICES_MAX];
static mao_devices_stats_t s_stats;
static odd_discovery_t s_discovery;
static volatile bool s_active;
static volatile bool s_active_changed;
static uint32_t s_grace_until_ms;
static volatile uint32_t s_flood_until_ms;
static uint32_t s_flood_next_ms;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void lock(void)   { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mutex); }

static void wake_task(void)
{
    const inbox_item_t wake = { .kind = ITEM_WAKE };
    xQueueSend(s_inbox, &wake, 0);   /* if full, the task is busy anyway */
}

/* ---------------------------------------------------------------------- */
/* Radio glue (Wi-Fi task context: copy only)                             */
/* ---------------------------------------------------------------------- */

static void radio_rx(const uint8_t mac[6], const uint8_t *data, size_t len, int8_t rssi, void *ctx)
{
    (void)ctx;
    if (len < ODD_HEADER_LEN + ODD_CRC_LEN || len > ODD_MAX_FRAME || data[0] != ODD_MAGIC0) {
        return;   /* not ODD BUS: cheapest possible rejection */
    }
    inbox_item_t item = { .kind = ITEM_RX, .rssi = rssi, .len = (uint8_t)len };
    memcpy(item.mac, mac, 6);
    memcpy(item.data, data, len);
    if (xQueueSend(s_inbox, &item, 0) != pdTRUE) {
        mao_radio_count_rx_drop();
        s_stats.inbox_dropped++;
    }
}

static esp_err_t odd_send(const uint8_t *dst_mac, const uint8_t *frame, size_t len, void *ctx)
{
    (void)ctx;
    return mao_radio_send(dst_mac, frame, len);
}

/* ---------------------------------------------------------------------- */
/* Registry helpers (task context unless noted)                           */
/* ---------------------------------------------------------------------- */

static int find_locked(uint64_t id)
{
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        if (s_dev[i].used && s_dev[i].pub.info.id == id) {
            return i;
        }
    }
    return -1;
}

static int cap_index(const entry_t *e, uint8_t cap_id)
{
    for (int i = 0; i < e->pub.cap_count; i++) {
        if (e->pub.caps[i].cap.id == cap_id) {
            return i;
        }
    }
    return -1;
}

static void post(mao_event_type_t type, int index)
{
    mao_event_post(type, index);
}

static void request(entry_t *e, odd_msg_type_t type)
{
    odd_bus_send(e->pub.mac, e->pub.info.id, type, NULL, NULL);
}

static void count_online_locked(void)
{
    uint32_t n = 0;
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        n += s_dev[i].used && s_dev[i].pub.online;
    }
    s_stats.devices_online = n;
}

/* ---------------------------------------------------------------------- */
/* ODD BUS message handling (task context)                                */
/* ---------------------------------------------------------------------- */

static void on_announce(const odd_message_t *m, uint32_t now)
{
    s_stats.announces_rx++;
    lock();
    int idx = find_locked(m->hdr.src_id);
    bool found = false;
    if (idx < 0) {
        for (int i = 0; i < MAO_DEVICES_MAX; i++) {
            if (!s_dev[i].used) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            unlock();
            ESP_LOGW(TAG, "registry full, ignoring '%s'", m->u.info.name);
            return;
        }
        memset(&s_dev[idx], 0, sizeof(s_dev[idx]));
        s_dev[idx].used = true;
        found = true;
        ESP_LOGI(TAG, "new device '%s' (%s) id %016llx rssi %d", m->u.info.name,
                 odd_device_type_name(m->u.info.device_type), (unsigned long long)m->hdr.src_id, m->rssi);
    } else if (!s_dev[idx].pub.online) {
        found = true;
        ESP_LOGI(TAG, "'%s' is back online", m->u.info.name);
    }
    entry_t *e = &s_dev[idx];
    e->pub.info = m->u.info;
    memcpy(e->pub.mac, m->src_mac, 6);
    e->pub.online = true;
    e->pub.rssi = m->rssi;
    e->pub.last_seen_ms = now;
    count_online_locked();
    unlock();

    if (found) {
        /* Refresh the description: capabilities if unknown, state always. */
        e->describe_req_ms = now;
        request(e, e->pub.cap_count ? ODD_MSG_GET_STATE : ODD_MSG_GET_CAPS);
        post(MAO_EVENT_DEVICE_FOUND, idx);
    } else if (!e->pub.described && now - e->describe_req_ms > DESCRIBE_RETRY_MS) {
        e->describe_req_ms = now;
        request(e, e->pub.cap_count ? ODD_MSG_GET_STATE : ODD_MSG_GET_CAPS);
    } else if (s_active && e->pub.described) {
        /* While the user is looking at devices, refresh state each round:
         * catches a device that rebooted or changed without a notification. */
        request(e, ODD_MSG_GET_STATE);
    }
}

static void on_capabilities(entry_t *e, int idx, const odd_message_t *m)
{
    lock();
    e->pub.cap_count = m->u.caps.count;
    for (int i = 0; i < m->u.caps.count; i++) {
        e->pub.caps[i] = (mao_device_cap_t) { .cap = m->u.caps.cap[i] };
        e->cmd[i] = (cmd_t) { 0 };
    }
    e->pub.described = false;
    unlock();
    ESP_LOGI(TAG, "'%s': %u capabilities", e->pub.info.name, m->u.caps.count);
    for (int i = 0; i < m->u.caps.count; i++) {
        const odd_capability_t *c = &m->u.caps.cap[i];
        ESP_LOGI(TAG, "  cap %u %s [%" PRId32 "..%" PRId32 " step %" PRId32 "] flags 0x%02x",
                 c->id, odd_cap_type_name(c->type), c->min, c->max, c->step, c->flags);
    }
    request(e, ODD_MSG_GET_STATE);
    post(MAO_EVENT_DEVICE_CHANGED, idx);
}

static void on_state(entry_t *e, int idx, const odd_message_t *m)
{
    bool changed = false;
    lock();
    for (int i = 0; i < m->u.state.count; i++) {
        const int c = cap_index(e, m->u.state.value[i].cap_id);
        if (c < 0) {
            continue;
        }
        mao_device_cap_t *dc = &e->pub.caps[c];
        dc->confirmed = m->u.state.value[i].value;
        dc->known = true;
        /* Don't overwrite an optimistic value the user is still steering. */
        if (!e->cmd[c].dirty && !e->cmd[c].in_flight && dc->value != dc->confirmed) {
            dc->value = dc->confirmed;
            changed = true;
        }
        if (!e->cmd[c].dirty && !e->cmd[c].in_flight) {
            dc->pending = false;
        }
    }
    bool all_known = e->pub.cap_count > 0;
    for (int i = 0; i < e->pub.cap_count; i++) {
        all_known = all_known && e->pub.caps[i].known;
    }
    if (all_known && !e->pub.described) {
        e->pub.described = true;
        changed = true;
    }
    unlock();
    if (changed) {
        post(MAO_EVENT_DEVICE_CHANGED, idx);
    }
}

static void on_ack(entry_t *e, int idx, const odd_message_t *m, int64_t now_us)
{
    const int c = cap_index(e, m->u.ack.applied.cap_id);
    if (c < 0) {
        s_stats.acks_stale++;
        return;
    }
    cmd_t *cmd = &e->cmd[c];
    if (!cmd->in_flight || m->u.ack.acked_seq != cmd->seq) {
        s_stats.acks_stale++;   /* late ACK of a retried/superseded command */
        return;
    }
    s_stats.acks_rx++;
    const uint32_t rtt = (uint32_t)(now_us - cmd->first_tx_us);
    s_stats.rtt_count++;
    s_stats.rtt_sum_us += rtt;
    if (s_stats.rtt_min_us == 0 || rtt < s_stats.rtt_min_us) {
        s_stats.rtt_min_us = rtt;
    }
    if (rtt > s_stats.rtt_max_us) {
        s_stats.rtt_max_us = rtt;
    }
    const uint32_t lat = (uint32_t)(now_us - cmd->input_us);
    s_stats.input_to_ack_count++;
    s_stats.input_to_ack_sum_us += lat;
    if (lat > s_stats.input_to_ack_max_us) {
        s_stats.input_to_ack_max_us = lat;
    }

    bool changed = false;
    lock();
    mao_device_cap_t *dc = &e->pub.caps[c];
    cmd->in_flight = false;
    cmd->retries = 0;
    dc->confirmed = m->u.ack.applied.value;
    dc->known = true;
    if (!cmd->dirty) {
        /* Caught up. If the device adjusted the value (clamped), show its. */
        if (dc->value != dc->confirmed) {
            dc->value = dc->confirmed;
            changed = true;
        }
        if (dc->pending) {
            dc->pending = false;
            changed = true;
        }
    }
    e->failures = 0;
    if (e->pub.link_problem) {
        e->pub.link_problem = false;
        changed = true;
    }
    unlock();
    if (changed) {
        post(MAO_EVENT_DEVICE_CHANGED, idx);
    }
}

static void on_message(const odd_message_t *m, void *ctx)
{
    (void)ctx;
    const uint32_t now = now_ms();
    if (m->hdr.type == ODD_MSG_ANNOUNCE) {
        on_announce(m, now);
        return;
    }
    if (m->hdr.type == ODD_MSG_DISCOVER) {
        return;   /* another controller looking around: nothing to answer */
    }

    lock();
    const int idx = find_locked(m->hdr.src_id);
    bool revived = false;
    if (idx >= 0) {
        s_dev[idx].pub.last_seen_ms = now;
        s_dev[idx].pub.rssi = m->rssi;
        /* Any direct answer proves it is alive: a device marked offline that
         * still replies (e.g. to a reachability probe) comes back at once. */
        if (!s_dev[idx].pub.online) {
            s_dev[idx].pub.online = true;
            revived = true;
            count_online_locked();
        }
    }
    unlock();
    if (idx < 0) {
        return;   /* unknown device: wait for its ANNOUNCE */
    }
    if (revived) {
        ESP_LOGI(TAG, "'%s' answered while marked offline: back online", s_dev[idx].pub.info.name);
        post(MAO_EVENT_DEVICE_FOUND, idx);
    }
    if (s_dev[idx].probe_armed) {
        /* The answer IS the reachability result: no polling in this path. */
        s_dev[idx].probe_armed = false;
        post(MAO_EVENT_DEVICE_PROBED, idx);
    }
    entry_t *e = &s_dev[idx];
    switch (m->hdr.type) {
    case ODD_MSG_CAPABILITIES: on_capabilities(e, idx, m); break;
    case ODD_MSG_STATE:        on_state(e, idx, m); break;
    case ODD_MSG_ACK:          on_ack(e, idx, m, esp_timer_get_time()); break;
    default: break;
    }
}

/* ---------------------------------------------------------------------- */
/* Periodic work                                                          */
/* ---------------------------------------------------------------------- */

static void send_set(entry_t *e, int c, cmd_t *cmd, int64_t now_us, bool retry)
{
    odd_message_t body = { 0 };
    body.u.set.cap_id = e->pub.caps[c].cap.id;
    body.u.set.value = cmd->sent_value;
    if (retry) {
        odd_bus_send_seq(e->pub.mac, e->pub.info.id, ODD_MSG_SET_VALUE, &body, cmd->seq);
    } else {
        odd_bus_send(e->pub.mac, e->pub.info.id, ODD_MSG_SET_VALUE, &body, &cmd->seq);
        cmd->first_tx_us = now_us;
    }
    cmd->last_tx_us = now_us;
}

/* Returns true while commands still need servicing soon. */
static bool service_commands(int64_t now_us)
{
    bool busy = false;
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        entry_t *e = &s_dev[i];
        if (!e->used || !e->pub.online) {
            continue;
        }
        for (int c = 0; c < e->pub.cap_count; c++) {
            cmd_t *cmd = &e->cmd[c];
            if (cmd->in_flight) {
                busy = true;
                if (now_us - cmd->last_tx_us < ACK_TIMEOUT_US) {
                    continue;
                }
                if (cmd->retries < MAX_RETRIES) {
                    cmd->retries++;
                    s_stats.retries++;
                    send_set(e, c, cmd, now_us, true);
                    continue;
                }
                /* Abandon: do not retry forever, do not pretend. */
                s_stats.timeouts++;
                bool raise = false;
                lock();
                cmd->in_flight = false;
                cmd->dirty = false;
                e->pub.caps[c].pending = true;
                if (++e->failures >= LINK_PROBLEM_AFTER && !e->pub.link_problem) {
                    e->pub.link_problem = true;
                    raise = true;
                }
                unlock();
                ESP_LOGW(TAG, "'%s' cap %u: no ACK for seq %u after %d retries",
                         e->pub.info.name, e->pub.caps[c].cap.id, cmd->seq, MAX_RETRIES);
                if (raise) {
                    post(MAO_EVENT_DEVICE_CHANGED, i);
                }
                continue;
            }
            if (!cmd->dirty) {
                continue;
            }
            busy = true;
            if (now_us - cmd->last_tx_us < COALESCE_US) {
                continue;
            }
            lock();
            cmd->sent_value = e->pub.caps[c].value;
            cmd->input_us = cmd->latest_input_us;
            cmd->dirty = false;
            unlock();
            cmd->in_flight = true;
            cmd->retries = 0;
            s_stats.commands_sent++;
            send_set(e, c, cmd, now_us, false);
        }
    }
    return busy;
}

static void service_liveness(uint32_t now)
{
    if ((int32_t)(now - s_grace_until_ms) < 0) {
        return;
    }
    const uint32_t timeout = s_active ? OFFLINE_ACTIVE_MS : OFFLINE_IDLE_MS;
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        entry_t *e = &s_dev[i];
        if (!e->used || !e->pub.online || now - e->pub.last_seen_ms <= timeout) {
            continue;
        }
        lock();
        e->pub.online = false;
        for (int c = 0; c < e->pub.cap_count; c++) {
            if (e->cmd[c].in_flight || e->cmd[c].dirty) {
                e->pub.caps[c].pending = true;
            }
            e->cmd[c].in_flight = false;
            e->cmd[c].dirty = false;
        }
        count_online_locked();
        unlock();
        ESP_LOGI(TAG, "'%s' offline (not seen for %" PRIu32 " ms)", e->pub.info.name, now - e->pub.last_seen_ms);
        post(MAO_EVENT_DEVICE_LOST, i);
    }
}

static void devices_task(void *arg)
{
    (void)arg;
    TickType_t wait = 0;
    for (;;) {
        inbox_item_t item;
        if (xQueueReceive(s_inbox, &item, wait) == pdTRUE && item.kind == ITEM_RX) {
            odd_bus_input(item.mac, item.data, item.len, item.rssi);
        }
        const uint32_t now = now_ms();
        if (s_active_changed) {
            s_active_changed = false;
            odd_discovery_set_interval(&s_discovery, s_active ? DISCOVERY_ACTIVE_MS : DISCOVERY_IDLE_MS, now);
            if (s_active) {
                s_grace_until_ms = now + MODE_GRACE_MS;
            }
        }
        /* Reachability probes requested from other tasks. */
        for (int i = 0; i < MAO_DEVICES_MAX; i++) {
            if (s_dev[i].used && s_dev[i].refresh_req) {
                s_dev[i].refresh_req = false;
                s_dev[i].probe_armed = true;
                request(&s_dev[i], ODD_MSG_GET_STATE);
            }
        }
        const bool busy = service_commands(esp_timer_get_time());
        const uint32_t until_discovery = odd_discovery_poll(&s_discovery, now);
        s_stats.discoveries_sent = s_discovery.sent;
        service_liveness(now);

        uint32_t wait_ms = until_discovery < 500 ? until_discovery : 500;
        if ((int32_t)(s_flood_until_ms - now) > 0) {
            if ((int32_t)(now - s_flood_next_ms) >= 0) {
                odd_bus_discover();
                s_flood_next_ms = now + 20;
            }
            wait_ms = wait_ms > 5 ? 5 : wait_ms;
        }
        if (busy && wait_ms > 5) {
            wait_ms = 5;
        }
        wait = pdMS_TO_TICKS(wait_ms);
    }
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

esp_err_t mao_devices_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_inbox = xQueueCreate(INBOX_LEN, sizeof(inbox_item_t));
    ESP_RETURN_ON_FALSE(s_mutex && s_inbox, ESP_ERR_NO_MEM, TAG, "alloc");

    ESP_RETURN_ON_ERROR(mao_radio_init(ODD_BUS_DEV_CHANNEL), TAG, "radio");

    uint8_t mac[6];
    mao_radio_get_mac(mac);
    odd_device_info_t self = {
        .id = odd_id_from_mac(mac),
        .device_type = ODD_DEVICE_CONTROLLER,
        .cap_count = 0,
    };
    strncpy(self.name, "MAO", sizeof(self.name) - 1);
    ESP_RETURN_ON_ERROR(odd_bus_init(&self, odd_send, NULL, on_message, NULL), TAG, "odd bus");

    odd_discovery_init(&s_discovery, DISCOVERY_IDLE_MS);
    mao_radio_set_rx_handler(radio_rx, NULL);
    if (xTaskCreate(devices_task, "mao_devices", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "controller ready: registry %d, discovery %d ms idle / %d ms active, "
             "offline after %d / %d ms", MAO_DEVICES_MAX, DISCOVERY_IDLE_MS, DISCOVERY_ACTIVE_MS,
             OFFLINE_IDLE_MS, OFFLINE_ACTIVE_MS);
    return ESP_OK;
}

void mao_devices_set_active(bool active)
{
    if (active != s_active) {
        s_active = active;
        s_active_changed = true;
        if (s_inbox) {
            wake_task();
        }
    }
}

int mao_devices_count(void)
{
    int n = 0;
    lock();
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        n += s_dev[i].used;
    }
    unlock();
    return n;
}

bool mao_devices_get(int index, mao_device_t *out)
{
    if (index < 0 || index >= MAO_DEVICES_MAX || !out) {
        return false;
    }
    lock();
    const bool used = s_dev[index].used;
    if (used) {
        *out = s_dev[index].pub;
    }
    unlock();
    return used;
}

int mao_devices_find(uint64_t id)
{
    lock();
    const int idx = find_locked(id);
    unlock();
    return idx;
}

esp_err_t mao_devices_set_value(uint64_t id, uint8_t cap_id, int32_t value)
{
    esp_err_t err = ESP_OK;
    lock();
    const int idx = find_locked(id);
    entry_t *e = idx >= 0 ? &s_dev[idx] : NULL;
    const int c = e ? cap_index(e, cap_id) : -1;
    if (c < 0) {
        err = ESP_ERR_NOT_FOUND;
    } else if (!e->pub.online) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        const odd_capability_t *cap = &e->pub.caps[c].cap;
        if (value < cap->min) {
            value = cap->min;
        } else if (value > cap->max) {
            value = cap->max;
        }
        value = cap->min + ((value - cap->min) / cap->step) * cap->step;
        mao_device_cap_t *dc = &e->pub.caps[c];
        if (e->cmd[c].dirty || e->cmd[c].in_flight) {
            s_stats.coalesced++;
        }
        dc->value = value;
        dc->pending = true;
        e->cmd[c].dirty = true;
        e->cmd[c].latest_input_us = esp_timer_get_time();
    }
    unlock();
    if (err == ESP_OK) {
        wake_task();
    }
    return err;
}

esp_err_t mao_devices_refresh(uint64_t id)
{
    lock();
    const int idx = find_locked(id);
    if (idx >= 0) {
        s_dev[idx].refresh_req = true;
    }
    unlock();
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    wake_task();
    return ESP_OK;
}

void mao_devices_debug_flood(uint32_t duration_ms)
{
    s_flood_next_ms = now_ms();
    s_flood_until_ms = now_ms() + duration_ms;
    wake_task();
}

void mao_devices_reset_latency(void)
{
    lock();
    s_stats.rtt_count = 0;
    s_stats.rtt_sum_us = 0;
    s_stats.rtt_min_us = 0;
    s_stats.rtt_max_us = 0;
    s_stats.input_to_ack_count = 0;
    s_stats.input_to_ack_sum_us = 0;
    s_stats.input_to_ack_max_us = 0;
    unlock();
}

void mao_devices_get_stats(mao_devices_stats_t *out)
{
    if (out) {
        lock();
        *out = s_stats;
        unlock();
    }
}

void mao_devices_log_status(void)
{
    odd_bus_stats_t o;
    odd_bus_get_stats(&o);
    mao_radio_stats_t r;
    mao_radio_get_stats(&r);
    mao_devices_stats_t d;
    mao_devices_get_stats(&d);

    ESP_LOGI(TAG, "radio: tx=%" PRIu32 " tx_fail=%" PRIu32 " tx_rej=%" PRIu32 " rx=%" PRIu32 " rx_drop=%" PRIu32,
             r.tx_frames, r.tx_failed, r.tx_rejected, r.rx_frames, r.rx_dropped);
    ESP_LOGI(TAG, "odd: tx=%" PRIu32 " rx=%" PRIu32 " not_odd=%" PRIu32 " bad_ver=%" PRIu32 " bad_crc=%" PRIu32
             " malformed=%" PRIu32 " not_for_us=%" PRIu32 " own=%" PRIu32,
             o.tx, o.rx, o.rx_not_odd, o.rx_bad_version, o.rx_bad_crc, o.rx_malformed, o.rx_not_for_us, o.rx_own);
    ESP_LOGI(TAG, "devices: online=%" PRIu32 " discover=%" PRIu32 " announce=%" PRIu32 " cmd=%" PRIu32
             " ack=%" PRIu32 " stale=%" PRIu32 " retry=%" PRIu32 " timeout=%" PRIu32 " coalesced=%" PRIu32
             " inbox_drop=%" PRIu32,
             d.devices_online, d.discoveries_sent, d.announces_rx, d.commands_sent, d.acks_rx, d.acks_stale,
             d.retries, d.timeouts, d.coalesced, d.inbox_dropped);
    if (d.rtt_count) {
        ESP_LOGI(TAG, "latency: cmd->ack rtt avg=%.2fms min=%.2fms max=%.2fms (n=%" PRIu32 "), "
                 "input->ack avg=%.2fms max=%.2fms",
                 (double)d.rtt_sum_us / d.rtt_count / 1000.0, d.rtt_min_us / 1000.0, d.rtt_max_us / 1000.0,
                 d.rtt_count, (double)d.input_to_ack_sum_us / d.input_to_ack_count / 1000.0,
                 d.input_to_ack_max_us / 1000.0);
    }
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        mao_device_t dev;
        if (!mao_devices_get(i, &dev)) {
            continue;
        }
        char vals[64] = "";
        int n = 0;
        for (int c = 0; c < dev.cap_count && n < (int)sizeof(vals); c++) {
            n += snprintf(vals + n, sizeof(vals) - n, " %s=%" PRId32 "%s%s", odd_cap_type_name(dev.caps[c].cap.type),
                          dev.caps[c].value, dev.caps[c].pending ? "*" : "",
                          dev.caps[c].value != dev.caps[c].confirmed ? "?" : "");
        }
        ESP_LOGI(TAG, "  [%d] '%s' %s%s rssi=%d seen=%" PRIu32 "ms ago%s", i, dev.info.name,
                 dev.online ? "ONLINE" : "OFFLINE", dev.link_problem ? " LINK-PROBLEM" : "", dev.rssi,
                 now_ms() - dev.last_seen_ms, vals);
    }
}
