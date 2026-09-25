/*
 * LAMP 01: minimal ODD BUS test device (not product firmware).
 *
 * Exposes POWER and LEVEL 0..100, answers DISCOVER / GET_CAPS / GET_STATE,
 * applies SET_VALUE and ACKs it, announces itself at boot, and notifies the
 * last controller of local (console) changes. Same threading rule as MAO: the
 * ESP-NOW callback only queues frames; all ODD BUS work happens in lamp_task.
 */
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "odd_bus.h"
#include "lamp.h"

static const char *TAG = "LAMP01";

#define LAMP_NAME       "LAMP 01"
#define INBOX_LEN       12
#define MAX_SOURCES     4

typedef enum { ITEM_RX = 0, ITEM_CMD } item_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t cmd;        /* ITEM_CMD: lamp_cmd_t */
    uint8_t mac[6];
    int8_t rssi;
    uint8_t len;
    int32_t arg;
    uint8_t data[ODD_MAX_FRAME];
} item_t;

/* Per controller: the active session (current incarnation), the immediately
 * previous one (a delayed old SESSION_OPEN or command must never reclaim or
 * execute), and per-capability sequence history WITH the result each applied
 * command produced - a duplicate is answered with the original result, never
 * re-executed (the contract future one-shot ACTIONs will rely on). */
typedef struct {
    bool used;
    uint64_t id;
    uint64_t cur_inc;            /* active incarnation (0 = no session) */
    uint64_t prev_inc;           /* the one before it (refused, not forgotten) */
    bool seq_valid[LAMP_CAP_TOP + 1];
    uint16_t last_seq[LAMP_CAP_TOP + 1];
    uint8_t last_status[LAMP_CAP_TOP + 1];   /* ACK-status cache for duplicate re-ACKs */
    int32_t last_value[LAMP_CAP_TOP + 1];
    uint8_t act_result[LAMP_CAP_TOP + 1];    /* cached odd_action_result_t (0 = none/running) */
} source_t;

static QueueHandle_t s_inbox;
static bool s_power = true;
static int32_t s_level = 42;
static bool s_offline;
static source_t s_src[MAX_SOURCES];
static bool s_have_controller;
static uint8_t s_ctrl_mac[6];
static uint64_t s_ctrl_id;
static uint32_t s_applied, s_duplicates, s_stale;
static uint32_t s_no_session, s_stale_session, s_sessions;

/* The one in-flight action job (a tiny fixed "table"; see the milestone:
 * one concurrent action is enough while the architecture stays generic). */
typedef struct {
    bool active;
    uint8_t cap;
    uint16_t seq;
    uint64_t src_id;
    uint64_t inc;
    uint8_t mac[6];
    int64_t due_us;
    bool fail;                   /* complete FAILED (test mode captured at accept) */
} action_job_t;
static action_job_t s_job;
static uint32_t s_act_delay_ms = 450;    /* genuinely asynchronous by default */
static bool s_act_busy_mode, s_act_fail_mode;
static uint32_t s_drop_results;
static uint32_t s_identify_count, s_act_accepted, s_act_busy_refused, s_results_sent, s_results_dropped;
static struct {
    bool valid;
    uint8_t cap;
    uint16_t seq;
    uint64_t src_id;
    uint64_t inc;
    uint8_t mac[6];
    uint8_t result;
} s_last_result;

/* Test controls: lost / late ACKs and traffic floods. */
typedef struct {
    bool used;
    int64_t due_us;
    uint8_t mac[6];
    uint64_t id;
    uint64_t inc;        /* echo the command's incarnation (0 = legacy) */
    odd_message_t ack;
} late_ack_t;
#define LATE_MAX 8

static uint32_t s_drop_acks;        /* ACKs still to drop */
static uint32_t s_acks_dropped;
static uint32_t s_delay_ack_ms;
static late_ack_t s_late[LATE_MAX];
static uint32_t s_flood_hz, s_flood_mode, s_flood_sent;
static int64_t s_flood_until_us, s_flood_next_us;

static const odd_capability_t kCaps[] = {
    { .id = LAMP_CAP_POWER, .type = ODD_CAP_POWER, .flags = ODD_CAP_F_READ | ODD_CAP_F_WRITE | ODD_CAP_F_NOTIFY,
      .min = 0, .max = 1, .step = 1 },
    { .id = LAMP_CAP_LEVEL, .type = ODD_CAP_LEVEL, .flags = ODD_CAP_F_READ | ODD_CAP_F_WRITE | ODD_CAP_F_NOTIFY,
      .min = 0, .max = 100, .step = 1 },
    /* Generic discrete operation (ODD_CAP_ACTION): min = max = semantic. */
    { .id = LAMP_CAP_IDENTIFY, .type = ODD_CAP_ACTION, .flags = ODD_CAP_F_WRITE,
      .min = ODD_ACTION_IDENTIFY, .max = ODD_ACTION_IDENTIFY, .step = 1 },
};
#define CAP_COUNT ((uint8_t)(sizeof(kCaps) / sizeof(kCaps[0])))

/* ---------------------------------------------------------------------- */
/* ESP-NOW glue                                                           */
/* ---------------------------------------------------------------------- */

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || len < ODD_HEADER_LEN + ODD_CRC_LEN || len > ODD_MAX_FRAME) {
        return;
    }
    item_t it = { .kind = ITEM_RX, .len = (uint8_t)len, .rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0 };
    memcpy(it.mac, info->src_addr, 6);
    memcpy(it.data, data, len);
    xQueueSend(s_inbox, &it, 0);
}

static esp_err_t ensure_peer(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }
    esp_now_peer_info_t p = { .channel = 0, .ifidx = WIFI_IF_STA, .encrypt = false };
    memcpy(p.peer_addr, mac, 6);
    return esp_now_add_peer(&p);
}

static esp_err_t odd_send(const uint8_t *dst, const uint8_t *frame, size_t len, void *ctx)
{
    static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    (void)ctx;
    const uint8_t *to = dst ? dst : bcast;
    esp_err_t err = ensure_peer(to);
    return err == ESP_OK ? esp_now_send(to, frame, len) : err;
}

static void radio_init(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    /* LOLIN C3 Mini: the board's RF matching is poor and nothing it sends
     * arrives at full TX power (the classic symptom: it hears everything,
     * nobody hears it). The documented fix is capping TX at ~8.5 dBm. */
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(34));   /* units of 0.25 dBm */
    ESP_ERROR_CHECK(esp_wifi_set_channel(ODD_BUS_DEV_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
}

/* ---------------------------------------------------------------------- */
/* State                                                                  */
/* ---------------------------------------------------------------------- */

static void show(const char *why)
{
    lamp_output_apply(s_power, s_level);
    ESP_LOGI(TAG, "LAMP 01 power=%s level=%" PRId32 " (%s)", s_power ? "ON" : "OFF", s_level, why);
}

static int32_t value_of(uint8_t cap)
{
    return cap == LAMP_CAP_POWER ? (s_power ? 1 : 0) : s_level;
}

static void fill_state(odd_message_t *body, uint16_t in_reply_to)
{
    body->u.state.in_reply_to = in_reply_to;
    body->u.state.count = 0;
    for (uint8_t i = 0; i < CAP_COUNT; i++) {
        if (kCaps[i].type == ODD_CAP_ACTION) {
            continue;   /* an action has no stored value */
        }
        body->u.state.value[body->u.state.count].cap_id = kCaps[i].id;
        body->u.state.value[body->u.state.count].value = value_of(kCaps[i].id);
        body->u.state.count++;
    }
}

static source_t *source_for(uint64_t id)
{
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (s_src[i].used && s_src[i].id == id) {
            return &s_src[i];
        }
    }
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (!s_src[i].used) {
            s_src[i] = (source_t) { .used = true, .id = id };
            return &s_src[i];
        }
    }
    s_src[0] = (source_t) { .used = true, .id = id };   /* recycle */
    return &s_src[0];
}

static void remember_controller(const odd_message_t *m)
{
    memcpy(s_ctrl_mac, m->src_mac, 6);
    s_ctrl_id = m->hdr.src_id;
    s_have_controller = true;
}

/* ---------------------------------------------------------------------- */
/* ODD BUS handling (lamp task)                                           */
/* ---------------------------------------------------------------------- */

static void send_ack(const odd_message_t *m, odd_message_t *ack)
{
    if (s_drop_acks) {
        s_drop_acks--;
        s_acks_dropped++;
        ESP_LOGW(TAG, "test: dropped ACK for seq %u (%" PRIu32 " more to drop)", m->hdr.seq, s_drop_acks);
        return;
    }
    if (s_delay_ack_ms) {
        for (int i = 0; i < LATE_MAX; i++) {
            if (!s_late[i].used) {
                s_late[i] = (late_ack_t) { .used = true, .due_us = esp_timer_get_time() + s_delay_ack_ms * 1000LL,
                                           .id = m->hdr.src_id, .inc = m->incarnation, .ack = *ack };
                memcpy(s_late[i].mac, m->src_mac, 6);
                return;
            }
        }
        ESP_LOGW(TAG, "test: late-ACK slots full, sending now");
    }
    if (m->incarnation) {
        odd_bus_send_session(m->src_mac, m->hdr.src_id, ODD_MSG_ACK, ack, m->incarnation, NULL);
    } else {
        odd_bus_send(m->src_mac, m->hdr.src_id, ODD_MSG_ACK, ack, NULL);
    }
}

/* Apply a validated SET and remember its result for duplicate re-ACKs. */
static uint8_t apply_set(source_t *src, uint8_t cap, int32_t req, uint16_t seq)
{
    const odd_capability_t *c = &kCaps[cap - 1];
    int32_t v = req < c->min ? c->min : (req > c->max ? c->max : req);
    if (cap == LAMP_CAP_POWER) {
        s_power = v != 0;
    } else {
        s_level = v;
    }
    s_applied++;
    src->seq_valid[cap] = true;
    src->last_seq[cap] = seq;
    src->last_status[cap] = v == req ? ODD_ACK_OK : ODD_ACK_CLAMPED;
    src->last_value[cap] = value_of(cap);
    char why[40];
    snprintf(why, sizeof(why), "SET seq %u", seq);
    show(why);
    return src->last_status[cap];
}

static void handle_set(const odd_message_t *m)
{
    odd_message_t ack = { 0 };
    ack.u.ack.acked_seq = m->hdr.seq;
    ack.u.ack.applied.cap_id = m->u.set.cap_id;
    const uint8_t cap = m->u.set.cap_id;

    if (cap != LAMP_CAP_POWER && cap != LAMP_CAP_LEVEL) {
        ack.u.ack.status = ODD_ACK_UNKNOWN_CAP;
        ack.u.ack.applied.value = value_of(cap);
        send_ack(m, &ack);
        return;
    }
    source_t *src = source_for(m->hdr.src_id);

    if (m->incarnation) {
        /* Session-aware command. INVARIANT: a command from a non-current
         * incarnation must never execute. No sequence-distance heuristics. */
        if (src->cur_inc == 0 || m->incarnation != src->cur_inc) {
            const bool prev = src->prev_inc && m->incarnation == src->prev_inc;
            ack.u.ack.status = prev ? ODD_ACK_STALE_SESSION : ODD_ACK_NO_SESSION;
            if (prev) {
                s_stale_session++;
                ESP_LOGW(TAG, "stale incarnation %016llx from %016llx: NOT executed",
                         (unsigned long long)m->incarnation, (unsigned long long)m->hdr.src_id);
            } else {
                s_no_session++;
            }
        } else if (src->seq_valid[cap] && m->hdr.seq == src->last_seq[cap]) {
            s_duplicates++;                   /* retry: re-answer, never re-execute */
            ack.u.ack.status = src->last_status[cap];
            ack.u.ack.applied.value = src->last_value[cap];
            send_ack(m, &ack);
            return;
        } else if (src->seq_valid[cap] && !odd_seq_newer(m->hdr.seq, src->last_seq[cap])) {
            s_stale++;                        /* arrived after a newer one: ignore */
            ack.u.ack.status = ODD_ACK_STALE;
        } else {
            ack.u.ack.status = apply_set(src, cap, m->u.set.value, m->hdr.seq);
            ack.u.ack.applied.value = src->last_value[cap];
            send_ack(m, &ack);
            return;
        }
        ack.u.ack.applied.value = value_of(cap);
        send_ack(m, &ack);
        return;
    }

    /* LEGACY controller (no incarnation on the wire). The old backward-jump
     * heuristic survives ONLY here: it cannot influence session-aware
     * commands and must never be the basis for one-shot actions. */
    const bool restarted = src->seq_valid[cap] && !odd_seq_newer(m->hdr.seq, src->last_seq[cap]) &&
                           (uint16_t)(src->last_seq[cap] - m->hdr.seq) > 4096;
    if (src->seq_valid[cap] && m->hdr.seq == src->last_seq[cap]) {
        s_duplicates++;
        ack.u.ack.status = src->last_status[cap];
        ack.u.ack.applied.value = src->last_value[cap];
        send_ack(m, &ack);
        return;
    }
    if (src->seq_valid[cap] && !restarted && !odd_seq_newer(m->hdr.seq, src->last_seq[cap])) {
        s_stale++;
        ack.u.ack.status = ODD_ACK_STALE;
        ack.u.ack.applied.value = value_of(cap);
        send_ack(m, &ack);
        return;
    }
    if (restarted) {
        ESP_LOGW(TAG, "LEGACY controller %016llx sequence restarted (%u -> %u): accepting",
                 (unsigned long long)m->hdr.src_id, src->last_seq[cap], m->hdr.seq);
    }
    ack.u.ack.status = apply_set(src, cap, m->u.set.value, m->hdr.seq);
    ack.u.ack.applied.value = src->last_value[cap];
    send_ack(m, &ack);
}

static void send_result(const uint8_t *mac, uint64_t src_id, uint64_t inc, uint8_t cap, uint16_t seq,
                        uint8_t result)
{
    s_last_result.valid = true;
    s_last_result.cap = cap;
    s_last_result.seq = seq;
    s_last_result.src_id = src_id;
    s_last_result.inc = inc;
    s_last_result.result = result;
    memcpy(s_last_result.mac, mac, 6);
    if (s_drop_results) {
        s_drop_results--;
        s_results_dropped++;
        ESP_LOGW(TAG, "test: dropped ACTION_RESULT for seq %u (%" PRIu32 " more to drop)", seq, s_drop_results);
        return;
    }
    odd_message_t body = { 0 };
    body.u.action_result.cap_id = cap;
    body.u.action_result.action_seq = seq;
    body.u.action_result.result = result;
    odd_bus_send_session(mac, src_id, ODD_MSG_ACTION_RESULT, &body, inc, NULL);
    s_results_sent++;
}

/* The generic ACTION handler. INVARIANTS (see docs/odd_bus_identity.md):
 * a duplicate (source, incarnation, action, seq) never executes twice; a
 * non-current incarnation never executes; a BUSY refusal stays a refusal on
 * retry - only a new sequence is a new user intention. */
static void handle_action(const odd_message_t *m)
{
    odd_message_t ack = { 0 };
    ack.u.ack.acked_seq = m->hdr.seq;
    ack.u.ack.applied.cap_id = m->u.action.cap_id;
    ack.u.ack.applied.value = 0;
    const uint8_t cap = m->u.action.cap_id;
    source_t *src = source_for(m->hdr.src_id);

    /* Same session gate as SET; there is no legacy path at all. */
    if (src->cur_inc == 0 || m->incarnation != src->cur_inc) {
        const bool prev = src->prev_inc && m->incarnation == src->prev_inc;
        ack.u.ack.status = prev ? ODD_ACK_STALE_SESSION : ODD_ACK_NO_SESSION;
        if (prev) {
            s_stale_session++;
            ESP_LOGW(TAG, "stale-incarnation ACTION: NOT executed");
        } else {
            s_no_session++;
        }
        send_ack(m, &ack);
        return;
    }
    if (cap != LAMP_CAP_IDENTIFY) {
        ack.u.ack.status = ODD_ACK_UNKNOWN_CAP;
        send_ack(m, &ack);
        return;
    }
    if (src->seq_valid[cap] && m->hdr.seq == src->last_seq[cap]) {
        /* Duplicate: replay the original outcome, never re-execute. */
        s_duplicates++;
        ack.u.ack.status = src->last_status[cap];
        send_ack(m, &ack);
        if (src->act_result[cap] != 0) {
            send_result(m->src_mac, m->hdr.src_id, m->incarnation, cap, m->hdr.seq, src->act_result[cap]);
        }
        return;
    }
    if (src->seq_valid[cap] && !odd_seq_newer(m->hdr.seq, src->last_seq[cap])) {
        s_stale++;
        ack.u.ack.status = ODD_ACK_STALE;
        send_ack(m, &ack);
        return;
    }
    /* A new action identity. */
    src->seq_valid[cap] = true;
    src->last_seq[cap] = m->hdr.seq;
    src->act_result[cap] = 0;
    if (s_act_busy_mode || s_job.active) {
        /* Refused. The refusal IS this identity's outcome: a later retry of
         * the same seq replays BUSY even if the busy condition has cleared. */
        src->last_status[cap] = ODD_ACK_BUSY;
        s_act_busy_refused++;
        ack.u.ack.status = ODD_ACK_BUSY;
        send_ack(m, &ack);
        return;
    }
    src->last_status[cap] = ODD_ACK_ACCEPTED;
    s_act_accepted++;
    s_job = (action_job_t) { .active = true, .cap = cap, .seq = m->hdr.seq, .src_id = m->hdr.src_id,
                             .inc = m->incarnation, .due_us = esp_timer_get_time() + s_act_delay_ms * 1000LL,
                             .fail = s_act_fail_mode };
    memcpy(s_job.mac, m->src_mac, 6);
    ack.u.ack.status = ODD_ACK_ACCEPTED;
    send_ack(m, &ack);
}

static void handle_session_open(const odd_message_t *m)
{
    source_t *src = source_for(m->hdr.src_id);
    odd_message_t ack = { 0 };
    ack.u.ack.acked_seq = m->hdr.seq;
    ack.u.ack.applied = (odd_value_t) { 0, 0 };

    if (src->cur_inc && m->incarnation == src->cur_inc) {
        /* Idempotent reopen (a lost ACK): same epoch, history preserved. */
        ack.u.ack.status = ODD_ACK_OK;
    } else if (src->prev_inc && m->incarnation == src->prev_inc) {
        /* A delayed old SESSION_OPEN can never reclaim the session. */
        ack.u.ack.status = ODD_ACK_STALE_SESSION;
        s_stale_session++;
        ESP_LOGW(TAG, "stale SESSION_OPEN %016llx from %016llx refused",
                 (unsigned long long)m->incarnation, (unsigned long long)m->hdr.src_id);
    } else {
        ESP_LOGI(TAG, "controller %016llx incarnation: %016llx -> %016llx (sequence history reset)",
                 (unsigned long long)m->hdr.src_id, (unsigned long long)src->cur_inc,
                 (unsigned long long)m->incarnation);
        src->prev_inc = src->cur_inc;
        src->cur_inc = m->incarnation;
        memset(src->seq_valid, 0, sizeof(src->seq_valid));   /* new epoch, fresh history */
        memset(src->act_result, 0, sizeof(src->act_result));
        s_sessions++;
        ack.u.ack.status = ODD_ACK_OK;
    }
    odd_bus_send_session(m->src_mac, m->hdr.src_id, ODD_MSG_ACK, &ack, m->incarnation, NULL);
}

/* Due late ACKs and flood traffic. Returns ms until the next timed work. */
static uint32_t service_tests(void)
{
    const int64_t now = esp_timer_get_time();
    int64_t next = now + 1000 * 1000;
    if (s_job.active) {
        if (now >= s_job.due_us) {
            /* Execute exactly once, cache the outcome, report it. */
            s_job.active = false;
            const uint8_t result = s_job.fail ? ODD_ACTION_R_FAILED : ODD_ACTION_R_DONE;
            if (!s_job.fail) {
                s_identify_count++;
                ESP_LOGI(TAG, "IDENTIFY: *** LAMP 01 here *** (execution #%" PRIu32 ", seq %u)",
                         s_identify_count, s_job.seq);
            } else {
                ESP_LOGW(TAG, "IDENTIFY: executing as FAILED (test mode, seq %u)", s_job.seq);
            }
            for (int i = 0; i < MAX_SOURCES; i++) {
                if (s_src[i].used && s_src[i].id == s_job.src_id) {
                    s_src[i].act_result[s_job.cap] = result;
                }
            }
            send_result(s_job.mac, s_job.src_id, s_job.inc, s_job.cap, s_job.seq, result);
        } else if (s_job.due_us < next) {
            next = s_job.due_us;
        }
    }
    for (int i = 0; i < LATE_MAX; i++) {
        if (!s_late[i].used) {
            continue;
        }
        if (now >= s_late[i].due_us) {
            s_late[i].used = false;
            if (s_late[i].inc) {
                odd_bus_send_session(s_late[i].mac, s_late[i].id, ODD_MSG_ACK, &s_late[i].ack,
                                     s_late[i].inc, NULL);
            } else {
                odd_bus_send(s_late[i].mac, s_late[i].id, ODD_MSG_ACK, &s_late[i].ack, NULL);
            }
            ESP_LOGI(TAG, "test: late ACK for seq %u sent", s_late[i].ack.u.ack.acked_seq);
        } else if (s_late[i].due_us < next) {
            next = s_late[i].due_us;
        }
    }
    if (s_flood_hz) {
        if (now >= s_flood_until_us) {
            ESP_LOGW(TAG, "test: flood done, %" PRIu32 " frames", s_flood_sent);
            s_flood_hz = 0;
        } else {
            if (now >= s_flood_next_us) {
                if (!s_offline && s_flood_mode == LAMP_FLOOD_ANNOUNCE) {
                    odd_bus_announce(NULL, ODD_ID_ANY);
                    s_flood_sent++;
                } else if (!s_offline && s_have_controller) {
                    odd_message_t body = { 0 };
                    fill_state(&body, 0);
                    odd_bus_send(s_ctrl_mac, s_ctrl_id, ODD_MSG_STATE, &body, NULL);
                    s_flood_sent++;
                }
                s_flood_next_us += 1000000 / s_flood_hz;
                if (s_flood_next_us < now) {
                    s_flood_next_us = now + 1000000 / s_flood_hz;
                }
            }
            if (s_flood_next_us < next) {
                next = s_flood_next_us;
            }
        }
    }
    const int64_t ms = (next - now + 999) / 1000;
    return ms < 1 ? 1 : (uint32_t)ms;
}

static void on_message(const odd_message_t *m, void *ctx)
{
    (void)ctx;
    if (s_offline) {
        return;
    }
    odd_message_t body = { 0 };
    switch (m->hdr.type) {
    case ODD_MSG_DISCOVER:
        remember_controller(m);
        /* Small random jitter so many devices don't answer in the same slot. */
        vTaskDelay(pdMS_TO_TICKS(esp_random() % 20));
        odd_bus_announce(m->src_mac, m->hdr.src_id);
        break;
    case ODD_MSG_GET_CAPS:
        body.u.caps.count = CAP_COUNT;
        memcpy(body.u.caps.cap, kCaps, sizeof(kCaps));
        odd_bus_send(m->src_mac, m->hdr.src_id, ODD_MSG_CAPABILITIES, &body, NULL);
        break;
    case ODD_MSG_GET_STATE:
        remember_controller(m);
        fill_state(&body, m->hdr.seq);
        odd_bus_send(m->src_mac, m->hdr.src_id, ODD_MSG_STATE, &body, NULL);
        break;
    case ODD_MSG_SET_VALUE:
        remember_controller(m);
        handle_set(m);
        break;
    case ODD_MSG_SESSION_OPEN:
        remember_controller(m);
        handle_session_open(m);
        break;
    case ODD_MSG_ACTION:
        remember_controller(m);
        handle_action(m);
        break;
    default:
        break;
    }
}

static void send_junk(void)
{
    uint8_t f[ODD_MAX_FRAME];
    /* 1: not ODD at all. */
    memset(f, 0xA5, 40);
    odd_send(NULL, f, 40, NULL);
    /* 2..4: a valid ANNOUNCE, then corrupted three ways. */
    odd_header_t h = { .type = ODD_MSG_ANNOUNCE, .seq = 1, .src_id = odd_bus_self()->id };
    odd_message_t b = { 0 };
    b.u.info = *odd_bus_self();
    const size_t n = odd_encode(&h, &b, f, sizeof(f));
    uint8_t g[ODD_MAX_FRAME];
    memcpy(g, f, n); g[2] = 2;                        /* unsupported version */
    odd_send(NULL, g, n, NULL);
    memcpy(g, f, n); g[n - 1] ^= 0xFF;                /* bad CRC */
    odd_send(NULL, g, n, NULL);
    memcpy(g, f, n); g[7] = (uint8_t)(g[7] + 1);      /* payload length mismatch */
    odd_send(NULL, g, n, NULL);
    ESP_LOGW(TAG, "sent 4 invalid frames (not-ODD, v2, bad CRC, bad length)");
}

static void handle_command(lamp_cmd_t cmd, int32_t arg)
{
    switch (cmd) {
    case LAMP_CMD_STATUS: {
        odd_bus_stats_t st;
        odd_bus_get_stats(&st);
        ESP_LOGI(TAG, "status: %s power=%s level=%" PRId32 " applied=%" PRIu32 " dup=%" PRIu32 " stale=%" PRIu32
                 " odd_rx=%" PRIu32 " odd_tx=%" PRIu32 " set_rx=%" PRIu32 " discover_rx=%" PRIu32
                 " acks_dropped=%" PRIu32 " drop_pending=%" PRIu32 " ack_delay=%" PRIu32 "ms"
                 " sessions=%" PRIu32 " no_session=%" PRIu32 " stale_session=%" PRIu32,
                 s_offline ? "OFFLINE" : "ONLINE", s_power ? "ON" : "OFF", s_level, s_applied, s_duplicates,
                 s_stale, st.rx, st.tx, st.per_type_rx[ODD_MSG_SET_VALUE], st.per_type_rx[ODD_MSG_DISCOVER],
                 s_acks_dropped, s_drop_acks, s_delay_ack_ms, s_sessions, s_no_session, s_stale_session);
        break;
    }
    case LAMP_CMD_OFFLINE:
        s_offline = true;
        ESP_LOGW(TAG, "simulating power loss: not answering");
        break;
    case LAMP_CMD_ONLINE:
        s_offline = false;
        ESP_LOGW(TAG, "back: announcing");
        odd_bus_announce(NULL, ODD_ID_ANY);
        break;
    case LAMP_CMD_SET_POWER:
    case LAMP_CMD_SET_LEVEL: {
        if (cmd == LAMP_CMD_SET_POWER) {
            s_power = arg != 0;
        } else {
            s_level = arg < 0 ? 0 : (arg > 100 ? 100 : arg);
        }
        show("local");
        if (s_have_controller && !s_offline) {
            odd_message_t body = { 0 };
            fill_state(&body, 0);   /* notification */
            odd_bus_send(s_ctrl_mac, s_ctrl_id, ODD_MSG_STATE, &body, NULL);
        }
        break;
    }
    case LAMP_CMD_JUNK:
        send_junk();
        break;
    case LAMP_CMD_DROP_ACK:
        s_drop_acks = arg < 0 ? 0 : (uint32_t)arg;
        ESP_LOGW(TAG, "test: dropping the next %" PRIu32 " ACKs (commands are still applied)", s_drop_acks);
        break;
    case LAMP_CMD_DELAY_ACK:
        s_delay_ack_ms = arg < 0 ? 0 : (uint32_t)arg;
        ESP_LOGW(TAG, "test: ACK delay %" PRIu32 " ms", s_delay_ack_ms);
        break;
    case LAMP_CMD_FLOOD: {
        s_flood_hz = (uint32_t)(arg & 0xFF);
        s_flood_mode = (uint32_t)((arg >> 24) & 0xFF);
        const uint32_t secs = (uint32_t)((arg >> 8) & 0xFF);
        s_flood_until_us = esp_timer_get_time() + secs * 1000000LL;
        s_flood_next_us = esp_timer_get_time();
        s_flood_sent = 0;
        ESP_LOGW(TAG, "test: flood %" PRIu32 " Hz %s for %" PRIu32 " s%s", s_flood_hz,
                 s_flood_mode == LAMP_FLOOD_ANNOUNCE ? "ANNOUNCE" : "STATE", secs,
                 (s_flood_mode == LAMP_FLOOD_STATE && !s_have_controller) ? " (no controller yet: nothing sent)" : "");
        break;
    }
    case LAMP_CMD_SESSION:
        for (int i = 0; i < MAX_SOURCES; i++) {
            if (s_src[i].used) {
                ESP_LOGI(TAG, "session: controller %016llx cur=%016llx prev=%016llx",
                         (unsigned long long)s_src[i].id, (unsigned long long)s_src[i].cur_inc,
                         (unsigned long long)s_src[i].prev_inc);
            }
        }
        break;
    case LAMP_CMD_DROP_SESSION:
        /* Simulates this device rebooting (sessions are never persisted). */
        memset(s_src, 0, sizeof(s_src));
        ESP_LOGW(TAG, "test: all controller sessions forgotten");
        break;
    case LAMP_CMD_INJECT_PREV: {
        /* The milestone's safety test: loop a well-formed SET carrying the
         * PREVIOUS incarnation into our own input path, as if a delayed old
         * packet arrived. It must be refused and must not change state. */
        source_t *src = NULL;
        for (int i = 0; i < MAX_SOURCES; i++) {
            if (s_src[i].used && s_src[i].prev_inc) {
                src = &s_src[i];
            }
        }
        if (!src) {
            ESP_LOGW(TAG, "test: no previous incarnation known; reboot MAO first");
            break;
        }
        odd_message_t body = { 0 };
        body.u.set.cap_id = (uint8_t)(arg / 1000);
        body.u.set.value = arg % 1000;
        body.incarnation = src->prev_inc;
        const odd_header_t hdr = {
            .type = ODD_MSG_SET_VALUE, .seq = (uint16_t)(esp_random() & 0x7FFF),
            .flags = ODD_FRAME_F_INCARNATION, .src_id = src->id, .dst_id = odd_bus_self()->id,
        };
        uint8_t frame[ODD_MAX_FRAME];
        const size_t n = odd_encode(&hdr, &body, frame, sizeof(frame));
        ESP_LOGW(TAG, "test: injecting delayed SET from PREVIOUS incarnation %016llx (cap %u value %ld)",
                 (unsigned long long)src->prev_inc, body.u.set.cap_id, (long)body.u.set.value);
        if (n) {
            odd_bus_input(s_ctrl_mac, frame, n, 0);
        }
        break;
    }
    case LAMP_CMD_IDENTIFY:
        s_identify_count++;
        ESP_LOGI(TAG, "IDENTIFY: *** LAMP 01 here *** (local, count %" PRIu32 ")", s_identify_count);
        break;
    case LAMP_CMD_ACT_DELAY:
        s_act_delay_ms = arg < 0 ? 0 : (uint32_t)arg;
        ESP_LOGW(TAG, "test: action completion delay %" PRIu32 " ms", s_act_delay_ms);
        break;
    case LAMP_CMD_ACT_BUSY:
        s_act_busy_mode = arg != 0;
        ESP_LOGW(TAG, "test: action BUSY mode %s", s_act_busy_mode ? "on" : "off");
        break;
    case LAMP_CMD_ACT_FAIL:
        s_act_fail_mode = arg != 0;
        ESP_LOGW(TAG, "test: action FAIL mode %s", s_act_fail_mode ? "on" : "off");
        break;
    case LAMP_CMD_ACT_DROP_RESULT:
        s_drop_results = arg < 0 ? 0 : (uint32_t)arg;
        ESP_LOGW(TAG, "test: dropping the next %" PRIu32 " ACTION_RESULTs", s_drop_results);
        break;
    case LAMP_CMD_ACT_DUP_RESULT:
        if (s_last_result.valid) {
            ESP_LOGW(TAG, "test: re-sending the last ACTION_RESULT (seq %u)", s_last_result.seq);
            odd_message_t body = { 0 };
            body.u.action_result.cap_id = s_last_result.cap;
            body.u.action_result.action_seq = s_last_result.seq;
            body.u.action_result.result = s_last_result.result;
            odd_bus_send_session(s_last_result.mac, s_last_result.src_id, ODD_MSG_ACTION_RESULT, &body,
                                 s_last_result.inc, NULL);
        }
        break;
    case LAMP_CMD_ACT_STATUS:
        ESP_LOGI(TAG, "action: identify=%" PRIu32 " accepted=%" PRIu32 " busy_refused=%" PRIu32
                 " results_sent=%" PRIu32 " results_dropped=%" PRIu32 " job=%s delay=%" PRIu32
                 "ms busy=%d fail=%d drop_result=%" PRIu32,
                 s_identify_count, s_act_accepted, s_act_busy_refused, s_results_sent, s_results_dropped,
                 s_job.active ? "RUNNING" : "idle", s_act_delay_ms, s_act_busy_mode, s_act_fail_mode,
                 s_drop_results);
        break;
    case LAMP_CMD_REBOOT:
        ESP_LOGW(TAG, "test: rebooting");
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
        break;
    }
}

void lamp_post_command(lamp_cmd_t cmd, int32_t arg)
{
    const item_t it = { .kind = ITEM_CMD, .cmd = (uint8_t)cmd, .arg = arg };
    xQueueSend(s_inbox, &it, pdMS_TO_TICKS(50));
}

static void lamp_task(void *arg)
{
    (void)arg;
    /* Announce at boot so a listening controller finds us without waiting
     * for its next discovery round. */
    odd_bus_announce(NULL, ODD_ID_ANY);
    for (;;) {
        item_t it;
        const uint32_t wait_ms = service_tests();
        if (xQueueReceive(s_inbox, &it, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
            continue;
        }
        if (it.kind == ITEM_RX) {
            odd_bus_input(it.mac, it.data, it.len, it.rssi);
        } else {
            handle_command((lamp_cmd_t)it.cmd, it.arg);
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());   /* test device: its own flash, safe to reset */
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_inbox = xQueueCreate(INBOX_LEN, sizeof(item_t));
    radio_init();
    ESP_ERROR_CHECK(lamp_output_init());

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    odd_device_info_t self = {
        .id = odd_id_from_mac(mac),
        .device_type = ODD_DEVICE_LIGHT,
        .cap_count = CAP_COUNT,
    };
    strncpy(self.name, LAMP_NAME, sizeof(self.name) - 1);
    ESP_ERROR_CHECK(odd_bus_init(&self, odd_send, NULL, on_message, NULL));

    show("boot");
    ESP_LOGI(TAG, "LAMP 01 ready on channel %d, mac " MACSTR ", caps: POWER, LEVEL 0-100",
             ODD_BUS_DEV_CHANNEL, MAC2STR(mac));
    xTaskCreate(lamp_task, "lamp", 4096, NULL, 5, NULL);
    lamp_console_start();
}
