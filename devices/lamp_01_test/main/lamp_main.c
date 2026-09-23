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

/* Per controller and capability: last applied SET sequence (duplicate and
 * out-of-order detection must be per capability, since one controller
 * interleaves commands for several capabilities). */
typedef struct {
    bool used;
    uint64_t id;
    bool seq_valid[3];
    uint16_t last_seq[3];
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

static const odd_capability_t kCaps[] = {
    { .id = LAMP_CAP_POWER, .type = ODD_CAP_POWER, .flags = ODD_CAP_F_READ | ODD_CAP_F_WRITE | ODD_CAP_F_NOTIFY,
      .min = 0, .max = 1, .step = 1 },
    { .id = LAMP_CAP_LEVEL, .type = ODD_CAP_LEVEL, .flags = ODD_CAP_F_READ | ODD_CAP_F_WRITE | ODD_CAP_F_NOTIFY,
      .min = 0, .max = 100, .step = 1 },
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
    body->u.state.count = CAP_COUNT;
    for (uint8_t i = 0; i < CAP_COUNT; i++) {
        body->u.state.value[i].cap_id = kCaps[i].id;
        body->u.state.value[i].value = value_of(kCaps[i].id);
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

static void handle_set(const odd_message_t *m)
{
    odd_message_t ack = { 0 };
    ack.u.ack.acked_seq = m->hdr.seq;
    ack.u.ack.applied.cap_id = m->u.set.cap_id;
    const uint8_t cap = m->u.set.cap_id;

    if (cap != LAMP_CAP_POWER && cap != LAMP_CAP_LEVEL) {
        ack.u.ack.status = ODD_ACK_UNKNOWN_CAP;
    } else {
        source_t *src = source_for(m->hdr.src_id);
        if (src->seq_valid[cap] && m->hdr.seq == src->last_seq[cap]) {
            s_duplicates++;                   /* retry of an applied command: re-ACK only */
            ack.u.ack.status = ODD_ACK_OK;
        } else if (src->seq_valid[cap] && !odd_seq_newer(m->hdr.seq, src->last_seq[cap])) {
            s_stale++;                        /* arrived after a newer one: ignore */
            ack.u.ack.status = ODD_ACK_STALE;
        } else {
            src->seq_valid[cap] = true;
            src->last_seq[cap] = m->hdr.seq;
            int32_t v = m->u.set.value;
            const odd_capability_t *c = &kCaps[cap - 1];
            const int32_t req = v;
            v = v < c->min ? c->min : (v > c->max ? c->max : v);
            ack.u.ack.status = v == req ? ODD_ACK_OK : ODD_ACK_CLAMPED;
            if (cap == LAMP_CAP_POWER) {
                s_power = v != 0;
            } else {
                s_level = v;
            }
            s_applied++;
            char why[40];
            snprintf(why, sizeof(why), "SET seq %u", m->hdr.seq);
            show(why);
        }
    }
    ack.u.ack.applied.value = value_of(cap);
    odd_bus_send(m->src_mac, m->hdr.src_id, ODD_MSG_ACK, &ack, NULL);
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
                 " odd_rx=%" PRIu32 " odd_tx=%" PRIu32 " set_rx=%" PRIu32 " discover_rx=%" PRIu32,
                 s_offline ? "OFFLINE" : "ONLINE", s_power ? "ON" : "OFF", s_level, s_applied, s_duplicates,
                 s_stale, st.rx, st.tx, st.per_type_rx[ODD_MSG_SET_VALUE], st.per_type_rx[ODD_MSG_DISCOVER]);
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
        if (xQueueReceive(s_inbox, &it, portMAX_DELAY) != pdTRUE) {
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
