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
#include "esp_random.h"
#include "esp_timer.h"
#include "mao_events.h"
#include "mao_link.h"
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
    uint8_t bcast;           /* sent to the broadcast address */
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

/* A device's in-flight action transaction: one per device (M3.2), so an
 * action on one device never blocks or answers for another. */
typedef struct {
    volatile bool req;        /* invoke requested (any task) */
    uint64_t dev;
    uint8_t cap;
    uint8_t state;            /* mao_action_state_t */
    uint16_t seq;
    bool seq_set;
    int64_t start_us;
    int64_t last_tx_us;
    int64_t invoke_us;        /* latency instrumentation: requested */
    int64_t first_tx_us;      /* first ACTION on the radio */
    uint8_t fast_retries;
} action_tx_t;

typedef struct {
    bool used;
    mao_device_t pub;
    action_tx_t act;
    cmd_t cmd[ODD_MAX_CAPS];
    uint8_t failures;
    uint32_t describe_req_ms;
    volatile bool refresh_req;   /* reachability probe requested (any task) */
    bool probe_armed;            /* next direct answer posts MAO_EVENT_DEVICE_PROBED */
    /* Controller session with this device (see odd_message.h identity
     * model). Not persisted anywhere: a reboot on either side heals through
     * NO_SESSION -> SESSION_OPEN automatically. */
    bool session_ok;
    bool session_inflight;
    uint16_t session_seq;
    int64_t session_tx_us;
    uint8_t session_retries;
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
static uint64_t s_incarnation;   /* this boot's identity; generated once, never stored */
/* The ODD frame being processed came out of a valid link envelope from
 * s_rx_peer (M3.1). Plaintext from a paired id is only a presence hint. */
static bool s_rx_auth;
static uint64_t s_rx_peer;
static uint32_t s_plain_hints, s_plain_ignored, s_auth_mismatch, s_plain_gated, s_describe_skipped;
static bool (*s_remembered)(uint64_t id);   /* relationship layer: needs M3.1 proof */

#define ACTION_RECOVER_US   (800 * 1000)        /* status-recovery cadence after ACCEPTED */
#define ACTION_DEADLINE_US  (8LL * 1000 * 1000) /* development completion timeout */
static uint32_t s_results_dup, s_results_stale;
static void action_post(int idx, uint8_t state);
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

static void radio_rx(const uint8_t mac[6], const uint8_t *data, size_t len, int8_t rssi, bool bcast, void *ctx)
{
    (void)ctx;
    const bool odd = len >= ODD_HEADER_LEN + ODD_CRC_LEN && data[0] == ODD_MAGIC0;
    const bool link = len >= 4 && data[0] == 'L';
    if (len > ODD_MAX_FRAME || (!odd && !link)) {
        return;   /* neither ODD BUS nor its link layer: cheapest possible rejection */
    }
    inbox_item_t item = { .kind = ITEM_RX, .rssi = rssi, .len = (uint8_t)len, .bcast = bcast };
    memcpy(item.mac, mac, 6);
    memcpy(item.data, data, len);
    if (xQueueSend(s_inbox, &item, 0) != pdTRUE) {
        mao_radio_count_rx_drop();
        s_stats.inbox_dropped++;
    }
}

/* Every ODD frame leaves through the link layer: enveloped inside a secure
 * session, plaintext broadcast for strangers, refused for a paired device
 * that has not proven itself (mao_link.c). */
static bool operational(uint8_t type)
{
    return type == ODD_MSG_GET_CAPS || type == ODD_MSG_GET_STATE || type == ODD_MSG_SET_VALUE ||
           type == ODD_MSG_SESSION_OPEN || type == ODD_MSG_ACTION;
}

/* Remembered, but not proven this session (no credential: VERIFY; a
 * credential that failed or is still verifying: REPAIR): no operational
 * traffic until the link is SECURE (see mao_devices_set_auth_gate). */
static bool needs_proof(uint64_t id)
{
    return s_remembered && mao_link_state(id) != MAO_LINK_SECURE && s_remembered(id);
}

static esp_err_t odd_send(const uint8_t *dst_mac, const uint8_t *frame, size_t len, void *ctx)
{
    (void)ctx;
    /* A remembered relationship without a credential (VERIFY / lost key)
     * must prove itself first: a plaintext ANNOUNCE makes it SEEN, never a
     * target of operational queries. (With a credential mao_link_tx itself
     * refuses plaintext.) Discovery metadata and strangers are unaffected. */
    if (dst_mac && len >= ODD_HEADER_LEN && s_remembered && operational(frame[3])) {
        uint64_t dst = 0;
        for (int i = 7; i >= 0; i--) {
            dst = dst << 8 | frame[16 + i];
        }
        if (needs_proof(dst)) {
            s_plain_gated++;                 /* safety net: request() already skips these */
            return ESP_ERR_INVALID_STATE;
        }
    }
    return mao_link_tx(dst_mac, frame, len);
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
    if (needs_proof(e->pub.info.id)) {
        s_describe_skipped++;                /* SEEN only: described once the link is secure */
        return;
    }
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
    /* Same identity, different shape: the device's capability set changed
     * (a reflash or an update). Drop everything we thought we knew. */
    const bool reshaped = e->pub.cap_count > 0 &&
                          (e->pub.info.cap_count != m->u.info.cap_count ||
                           e->pub.info.device_type != m->u.info.device_type);
    if (reshaped) {
        e->pub.cap_count = 0;
        e->pub.described = false;
        for (int c = 0; c < ODD_MAX_CAPS; c++) {
            e->cmd[c] = (cmd_t) { 0 };
        }
    }
    e->pub.info = m->u.info;
    memcpy(e->pub.mac, m->src_mac, 6);
    e->pub.online = true;
    e->pub.rssi = m->rssi;
    e->pub.last_seen_ms = now;
    count_online_locked();
    unlock();
    if (reshaped) {
        ESP_LOGI(TAG, "'%s' capability set changed: re-describing", e->pub.info.name);
        e->describe_req_ms = now;
        request(e, ODD_MSG_GET_CAPS);
        post(MAO_EVENT_DEVICE_CHANGED, idx);
    }

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
        e->pub.caps[i].known = m->u.caps.cap[i].type == ODD_CAP_ACTION;   /* no value to wait for */
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
    /* INVARIANT: an ACK from another controller incarnation (an earlier MAO
     * boot, or another controller's exchange) can never confirm anything. */
    if (m->incarnation != s_incarnation) {
        s_stats.acks_stale++;
        ESP_LOGD(TAG, "ACK with foreign incarnation ignored");
        return;
    }
    /* A session reply carries no capability. */
    if (m->u.ack.applied.cap_id == 0) {
        if (e->session_inflight && m->u.ack.acked_seq == e->session_seq) {
            e->session_inflight = false;
            if (m->u.ack.status == ODD_ACK_OK) {
                e->session_ok = true;
                s_stats.sessions_opened++;
                ESP_LOGI(TAG, "session established with '%s' (incarnation %016llx)",
                         e->pub.info.name, (unsigned long long)s_incarnation);
            } else {
                /* Only possible if the device believes a newer incarnation of
                 * us exists - impossible within one boot. Log loudly. */
                ESP_LOGE(TAG, "'%s' refused our SESSION_OPEN (status %u)", e->pub.info.name, m->u.ack.status);
            }
        }
        return;
    }
    /* This ACK answers the action transaction? */
    if (e->act.seq_set && m->u.ack.acked_seq == e->act.seq && m->u.ack.applied.cap_id == e->act.cap &&
        (e->act.state == MAO_ACTION_SENDING || e->act.state == MAO_ACTION_ACCEPTED)) {
        switch (m->u.ack.status) {
        case ODD_ACK_ACCEPTED:
            if (e->act.state == MAO_ACTION_SENDING) {
                ESP_LOGI(TAG, "latency: radio -> ACK %lld us", (long long)(now_us - e->act.first_tx_us));
                action_post(idx, MAO_ACTION_ACCEPTED);
            }
            e->act.last_tx_us = now_us;   /* result recovery paces from here */
            break;
        case ODD_ACK_BUSY:
            action_post(idx, MAO_ACTION_BUSY);
            break;
        case ODD_ACK_UNKNOWN_CAP:
        case ODD_ACK_READ_ONLY:
        case ODD_ACK_STALE:
            action_post(idx, MAO_ACTION_FAILED);
            break;
        case ODD_ACK_NO_SESSION:
        case ODD_ACK_STALE_SESSION:
            e->session_ok = false;
            e->session_inflight = false;
            if (e->act.state == MAO_ACTION_ACCEPTED) {
                /* Accepted, then the device forgot us: it rebooted mid-run.
                 * NEVER re-invoke - the outcome is unknown, and only a new
                 * explicit user intention may create a new action. */
                action_post(idx, MAO_ACTION_UNKNOWN);
            } else {
                /* Never accepted: the gate refuses before any execution, so
                 * re-sending the SAME identity after reopening is safe. */
                e->act.req = true;
                e->act.state = MAO_ACTION_IDLE;
                wake_task();
            }
            break;
        default:
            break;
        }
        return;
    }
    /* The device lost our session (it rebooted): re-open and re-send. */
    if (m->u.ack.status == ODD_ACK_NO_SESSION || m->u.ack.status == ODD_ACK_STALE_SESSION) {
        const int cc = cap_index(e, m->u.ack.applied.cap_id);
        s_stats.no_session_acks++;
        e->session_ok = false;
        e->session_inflight = false;
        if (cc >= 0 && e->cmd[cc].in_flight && m->u.ack.acked_seq == e->cmd[cc].seq) {
            e->cmd[cc].in_flight = false;
            lock();
            e->cmd[cc].dirty = true;   /* re-sent automatically once the session is back */
            e->cmd[cc].latest_input_us = e->cmd[cc].input_us;
            unlock();
            ESP_LOGW(TAG, "'%s': %s - reopening session and re-sending", e->pub.info.name,
                     m->u.ack.status == ODD_ACK_NO_SESSION ? "no session (device rebooted?)" : "stale session");
        }
        wake_task();
        return;
    }
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
    if (s_rx_auth && m->hdr.src_id != s_rx_peer) {
        s_auth_mismatch++;
        return;   /* an envelope proves its sender: the ODD frame must claim the same id */
    }
    if (!s_rx_auth && mao_link_requires_auth(m->hdr.src_id)) {
        /* A paired device: plaintext is at most "something claiming that
         * identity is nearby". It never touches the registry (no online, no
         * metadata, no capabilities) - the link layer asks for proof. */
        if (m->hdr.type == ODD_MSG_ANNOUNCE) {
            s_plain_hints++;
            mao_link_hint(m->hdr.src_id, m->src_mac);
        } else {
            s_plain_ignored++;
        }
        return;
    }
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
    case ODD_MSG_ACTION_RESULT:
        /* INVARIANT: a result from another incarnation affects nothing. */
        if (m->incarnation != s_incarnation) {
            s_results_stale++;
            ESP_LOGW(TAG, "ACTION_RESULT with foreign incarnation ignored");
            break;
        }
        if (e->act.seq_set && m->u.action_result.action_seq == e->act.seq &&
            m->u.action_result.cap_id == e->act.cap) {
            if (e->act.state == MAO_ACTION_ACCEPTED || e->act.state == MAO_ACTION_SENDING) {
                action_post(idx, m->u.action_result.result == ODD_ACTION_R_DONE ? MAO_ACTION_DONE
                                                                                : MAO_ACTION_FAILED);
            } else {
                s_results_dup++;   /* the outcome is already known: ignore */
            }
        } else {
            s_results_stale++;
        }
        break;
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
        odd_bus_send_session_seq(e->pub.mac, e->pub.info.id, ODD_MSG_SET_VALUE, &body, s_incarnation, cmd->seq);
    } else {
        odd_bus_send_session(e->pub.mac, e->pub.info.id, ODD_MSG_SET_VALUE, &body, s_incarnation, &cmd->seq);
        cmd->first_tx_us = now_us;
    }
    cmd->last_tx_us = now_us;
}

static void action_post(int idx, uint8_t state)
{
    action_tx_t *a = &s_dev[idx].act;
    a->state = state;
    ESP_LOGI(TAG, "action %s <- '%s' cap %u seq %u", mao_devices_action_state_name(state),
             s_dev[idx].pub.info.name, a->cap, a->seq);
    post(MAO_EVENT_ACTION_UPDATE, (idx << 4) | state);
}

static void send_action(entry_t *e, bool fresh, int64_t now_us)
{
    odd_message_t body = { 0 };
    body.u.action.cap_id = e->act.cap;
    if (fresh) {
        odd_bus_send_session(e->pub.mac, e->pub.info.id, ODD_MSG_ACTION, &body, s_incarnation, &e->act.seq);
        e->act.seq_set = true;
    } else {
        /* The SAME identity: status recovery, never a new execution. */
        odd_bus_send_session_seq(e->pub.mac, e->pub.info.id, ODD_MSG_ACTION, &body, s_incarnation, e->act.seq);
    }
    e->act.last_tx_us = now_us;
}

static bool action_open(const action_tx_t *a)
{
    return a->req || a->state == MAO_ACTION_SENDING || a->state == MAO_ACTION_ACCEPTED;
}

/* Drive one device's action transaction (task context). */
static void service_action_one(entry_t *e, int idx, int64_t now_us)
{
    action_tx_t *const act = &e->act;
    if (!action_open(act)) {
        return;
    }
    if (act->req) {
        if (!e->pub.online) {
            act->req = false;
            action_post(idx, MAO_ACTION_FAILED);   /* nothing was sent: definitely not executed */
            return;
        }
        if (!e->session_ok) {
            return;   /* service_session is opening it; we go right after */
        }
        act->req = false;
        act->start_us = now_us;
        act->fast_retries = 0;
        act->state = MAO_ACTION_SENDING;
        send_action(e, true, now_us);
        act->first_tx_us = esp_timer_get_time();
        ESP_LOGI(TAG, "latency: invoke -> radio %lld us", (long long)(act->first_tx_us - act->invoke_us));
        return;
    }
    if (now_us - act->start_us >= ACTION_DEADLINE_US) {
        /* The remote action may have executed; only the outcome is lost. */
        ESP_LOGW(TAG, "'%s': action seq %u unresolved after deadline", e->pub.info.name, act->seq);
        action_post(idx, MAO_ACTION_UNKNOWN);
        return;
    }
    if (act->state == MAO_ACTION_SENDING) {
        if (now_us - act->last_tx_us >= ACK_TIMEOUT_US && act->fast_retries < MAX_RETRIES) {
            act->fast_retries++;
            s_stats.retries++;
            send_action(e, false, now_us);
        } else if (now_us - act->last_tx_us >= ACTION_RECOVER_US) {
            send_action(e, false, now_us);   /* slow knocking; it may have executed */
        }
    } else if (now_us - act->last_tx_us >= ACTION_RECOVER_US) {
        send_action(e, false, now_us);       /* ACCEPTED: recover the result */
    }
}

static void service_action(int64_t now_us)
{
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        if (s_dev[i].used) {
            service_action_one(&s_dev[i], i, now_us);
        }
    }
}

static bool any_action_open(void)
{
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        if (s_dev[i].used && action_open(&s_dev[i].act)) {
            return true;
        }
    }
    return false;
}

/* Open (or retry opening) this boot's session with a device. Returns true
 * while commands for it must wait. Same pacing as commands: 40 ms, twice. */
static bool service_session(entry_t *e, int i, int64_t now_us)
{
    if (e->session_ok) {
        return false;
    }
    if (!e->session_inflight) {
        odd_message_t body = { 0 };
        odd_bus_send_session(e->pub.mac, e->pub.info.id, ODD_MSG_SESSION_OPEN, &body,
                             s_incarnation, &e->session_seq);
        e->session_inflight = true;
        e->session_retries = 0;
        e->session_tx_us = now_us;
        return true;
    }
    if (now_us - e->session_tx_us < ACK_TIMEOUT_US) {
        return true;
    }
    if (e->session_retries < MAX_RETRIES) {
        e->session_retries++;
        s_stats.retries++;
        odd_message_t body = { 0 };
        odd_bus_send_session_seq(e->pub.mac, e->pub.info.id, ODD_MSG_SESSION_OPEN, &body,
                                 s_incarnation, e->session_seq);
        e->session_tx_us = now_us;
        return true;
    }
    /* Abandon like a command: don't pretend, don't spin. Dirty commands are
     * dropped with their caps marked pending; the next user input tries anew. */
    e->session_inflight = false;
    s_stats.timeouts++;
    bool raise = false;
    lock();
    for (int c = 0; c < e->pub.cap_count; c++) {
        if (e->cmd[c].dirty || e->cmd[c].in_flight) {
            e->pub.caps[c].pending = true;
        }
        e->cmd[c].dirty = false;
        e->cmd[c].in_flight = false;
    }
    if (++e->failures >= LINK_PROBLEM_AFTER && !e->pub.link_problem) {
        e->pub.link_problem = true;
        raise = true;
    }
    unlock();
    ESP_LOGW(TAG, "'%s': no answer to SESSION_OPEN after %d retries", e->pub.info.name, MAX_RETRIES);
    if (raise) {
        post(MAO_EVENT_DEVICE_CHANGED, i);
    }
    return true;
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
        bool want_session = e->act.req && e->pub.online;
        for (int c = 0; c < e->pub.cap_count; c++) {
            want_session = want_session || e->cmd[c].dirty || e->cmd[c].in_flight;
        }
        if (want_session && service_session(e, i, now_us)) {
            busy = true;
            continue;   /* commands wait until this boot's session is open */
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
        if (action_open(&e->act)) {
            e->act.req = false;
            action_post(i, MAO_ACTION_UNKNOWN);   /* it may have run; never auto re-invoke */
        }
        post(MAO_EVENT_DEVICE_LOST, i);
    }
}

static void secure_probe(uint64_t id, const uint8_t mac[6])
{
    odd_bus_discover_to(mac, id);
}

/* A fresh secure session: whatever MAO learned about this id before it was
 * proven is re-learned over the secure path (task context: called from
 * mao_link_rx on this task). */
static void on_secure(uint64_t id, const uint8_t mac[6])
{
    lock();
    const int idx = find_locked(id);
    if (idx >= 0) {
        s_dev[idx].pub.cap_count = 0;
        s_dev[idx].pub.described = false;
        s_dev[idx].describe_req_ms = 0;
    }
    unlock();
    odd_bus_discover_to(mac, id);
}

static void devices_task(void *arg)
{
    (void)arg;
    TickType_t wait = 0;
    for (;;) {
        inbox_item_t item;
        if (xQueueReceive(s_inbox, &item, wait) == pdTRUE && item.kind == ITEM_RX) {
            const uint8_t *odd = NULL;
            size_t odd_len = 0;
            uint64_t peer = 0;
            const mao_link_rx_t r = mao_link_rx(item.mac, item.data, item.len, item.bcast, &odd, &odd_len, &peer);
            if (r == MAO_LINK_RX_ODD_AUTH || r == MAO_LINK_RX_ODD_PLAIN) {
                s_rx_auth = r == MAO_LINK_RX_ODD_AUTH;
                s_rx_peer = peer;
                odd_bus_input(item.mac, odd, odd_len, item.rssi);
                s_rx_auth = false;
            }
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
        service_action(esp_timer_get_time());
        const bool busy = service_commands(esp_timer_get_time()) || any_action_open();
        const uint32_t sent_before = s_discovery.sent;
        const uint32_t until_discovery = odd_discovery_poll(&s_discovery, now);
        s_stats.discoveries_sent = s_discovery.sent;
        if (s_discovery.sent != sent_before) {
            /* Liveness of paired devices comes only from authenticated
             * answers: one enveloped identity query per secure peer. */
            mao_link_foreach_secure(secure_probe);
        }
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

esp_err_t mao_devices_invoke_action(uint64_t id, uint8_t cap_id)
{
    lock();
    const int idx = find_locked(id);
    unlock();
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    action_tx_t *const act = &s_dev[idx].act;   /* registry slots are never reused */
    if (action_open(act)) {
        return ESP_ERR_INVALID_STATE;   /* one transaction at a time per device */
    }
    *act = (action_tx_t) { .dev = id, .cap = cap_id, .invoke_us = esp_timer_get_time() };
    act->req = true;
    wake_task();
    return ESP_OK;
}

mao_action_state_t mao_devices_action_state(uint64_t id)
{
    lock();
    const int idx = find_locked(id);
    unlock();
    return idx < 0 ? MAO_ACTION_IDLE : (mao_action_state_t)s_dev[idx].act.state;
}

const char *mao_devices_action_state_name(mao_action_state_t st)
{
    static const char *const kNames[] = { "IDLE", "SENDING", "ACCEPTED", "DONE", "FAILED", "BUSY", "UNKNOWN" };
    return st <= MAO_ACTION_UNKNOWN ? kNames[st] : "?";
}

void mao_devices_action_dump(void)
{
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        if (s_dev[i].used && s_dev[i].act.dev) {
            const action_tx_t *a = &s_dev[i].act;
            ESP_LOGI(TAG, "action: '%s' state=%s cap=%u seq=%u", s_dev[i].pub.info.name,
                     mao_devices_action_state_name((mao_action_state_t)a->state), a->cap, a->seq);
        }
    }
    ESP_LOGI(TAG, "action: results_dup=%" PRIu32 " results_stale=%" PRIu32, s_results_dup, s_results_stale);
}

/* This boot's controller incarnation (see odd_message.h): random, non-zero,
 * never persisted. A reboot IS a new incarnation - that is the point. */
uint64_t mao_devices_incarnation(void)
{
    return s_incarnation;
}

/* Development: one SET carrying a fabricated foreign incarnation. The device
 * must refuse it (NO_SESSION) and change nothing. */
void mao_devices_debug_stale_set(void)
{
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        entry_t *e = &s_dev[i];
        if (!e->used || !e->pub.online || e->pub.cap_count == 0) {
            continue;
        }
        odd_message_t body = { 0 };
        body.u.set.cap_id = e->pub.caps[0].cap.id;
        body.u.set.value = e->pub.caps[0].cap.max;
        const uint64_t fake = s_incarnation ^ 0xDEADBEEFCAFE0001ULL;
        ESP_LOGW(TAG, "dev: sending SET to '%s' with foreign incarnation %016llx (must be refused)",
                 e->pub.info.name, (unsigned long long)fake);
        odd_bus_send_session(e->pub.mac, e->pub.info.id, ODD_MSG_SET_VALUE, &body, fake, NULL);
        return;
    }
    ESP_LOGW(TAG, "dev: no online device to target");
}

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

    do {
        s_incarnation = ((uint64_t)esp_random() << 32) | esp_random();
    } while (s_incarnation == 0);
    ESP_LOGI(TAG, "MAO ODD incarnation = %016llx", (unsigned long long)s_incarnation);

    odd_discovery_init(&s_discovery, DISCOVERY_IDLE_MS);
    mao_radio_set_rx_handler(radio_rx, NULL);
    mao_link_set_secure_cb(on_secure);
    mao_link_set_probe_cb(secure_probe);
    if (xTaskCreate(devices_task, "mao_devices", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "controller ready: registry %d, discovery %d ms idle / %d ms active, "
             "offline after %d / %d ms", MAO_DEVICES_MAX, DISCOVERY_IDLE_MS, DISCOVERY_ACTIVE_MS,
             OFFLINE_IDLE_MS, OFFLINE_ACTIVE_MS);
    return ESP_OK;
}

void mao_devices_set_auth_gate(bool (*remembered)(uint64_t id))
{
    s_remembered = remembered;
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
             " inbox_drop=%" PRIu32 " sessions=%" PRIu32 " no_session=%" PRIu32,
             d.devices_online, d.discoveries_sent, d.announces_rx, d.commands_sent, d.acks_rx, d.acks_stale,
             d.retries, d.timeouts, d.coalesced, d.inbox_dropped, d.sessions_opened, d.no_session_acks);
    ESP_LOGI(TAG, "security: plaintext hints=%" PRIu32 " ignored=%" PRIu32 " describe skipped(unverified)=%" PRIu32
             " gated tx(unverified)=%" PRIu32 " auth id mismatch=%" PRIu32, s_plain_hints, s_plain_ignored,
             s_describe_skipped, s_plain_gated, s_auth_mismatch);
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
