/*
 * ODD BUS node: identity, sequence numbers, send helpers, validated receive
 * dispatch and protocol counters. Single owner task; not re-entrant.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "odd_bus.h"

static const char *TAG = "ODD_BUS";

#define BAD_VERSION_LOG_GAP_MS 5000

static odd_device_info_t s_self;
static odd_send_fn_t s_send;
static void *s_send_ctx;
static odd_rx_fn_t s_on_rx;
static void *s_rx_ctx;
static uint16_t s_seq;
static odd_bus_stats_t s_stats;
static uint32_t s_last_version_log_ms;

uint64_t odd_id_from_mac(const uint8_t mac[6])
{
    uint64_t id = 0x0DD0ULL << 48;
    for (int i = 0; i < 6; i++) {
        id |= (uint64_t)mac[i] << (8 * (5 - i));
    }
    return id;
}

const char *odd_device_type_name(uint16_t type)
{
    switch (type) {
    case ODD_DEVICE_CONTROLLER: return "CONTROLLER";
    case ODD_DEVICE_LIGHT:      return "LIGHT";
    case ODD_DEVICE_DISPLAY:    return "DISPLAY";
    case ODD_DEVICE_CLOCK:      return "CLOCK";
    case ODD_DEVICE_SPEAKER:    return "SPEAKER";
    default:                    return "UNKNOWN";
    }
}

const char *odd_cap_type_name(uint8_t type)
{
    switch (type) {
    case ODD_CAP_POWER: return "POWER";
    case ODD_CAP_LEVEL: return "LEVEL";
    default:            return "?";
    }
}

esp_err_t odd_bus_init(const odd_device_info_t *self, odd_send_fn_t send, void *send_ctx,
                       odd_rx_fn_t on_rx, void *rx_ctx)
{
    if (!self || !send || !on_rx || self->id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_self = *self;
    s_self.protocol_version = ODD_BUS_PROTOCOL_VERSION;
    s_send = send;
    s_send_ctx = send_ctx;
    s_on_rx = on_rx;
    s_rx_ctx = rx_ctx;
    /* Random start so a rebooted node's sequence numbers don't collide with
     * the ones a peer remembers from before. */
    s_seq = (uint16_t)esp_random();
    ESP_LOGI(TAG, "ODD BUS v%d node '%s' (%s) id %016llx", ODD_BUS_PROTOCOL_VERSION, s_self.name,
             odd_device_type_name(s_self.device_type), (unsigned long long)s_self.id);
    return ESP_OK;
}

const odd_device_info_t *odd_bus_self(void)
{
    return &s_self;
}

esp_err_t odd_bus_send_seq(const uint8_t *dst_mac, uint64_t dst_id, odd_msg_type_t type,
                           const odd_message_t *body, uint16_t seq)
{
    const odd_header_t hdr = {
        .version = ODD_BUS_PROTOCOL_VERSION,
        .type = (uint8_t)type,
        .seq = seq,
        .src_id = s_self.id,
        .dst_id = dst_id,
    };
    uint8_t frame[ODD_MAX_FRAME];
    const size_t len = odd_encode(&hdr, body, frame, sizeof(frame));
    if (len == 0) {
        s_stats.tx_errors++;
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = s_send(dst_mac, frame, len, s_send_ctx);
    if (err == ESP_OK) {
        s_stats.tx++;
        s_stats.per_type_tx[type]++;
    } else {
        s_stats.tx_errors++;
    }
    return err;
}

esp_err_t odd_bus_send(const uint8_t *dst_mac, uint64_t dst_id, odd_msg_type_t type,
                       const odd_message_t *body, uint16_t *seq_out)
{
    const uint16_t seq = ++s_seq;
    if (seq_out) {
        *seq_out = seq;
    }
    return odd_bus_send_seq(dst_mac, dst_id, type, body, seq);
}

static esp_err_t send_identity(odd_msg_type_t type, const uint8_t *dst_mac, uint64_t dst_id)
{
    odd_message_t body = { 0 };
    body.u.info = s_self;
    return odd_bus_send(dst_mac, dst_id, type, &body, NULL);
}

esp_err_t odd_bus_discover(void)
{
    return send_identity(ODD_MSG_DISCOVER, NULL, ODD_ID_ANY);
}

esp_err_t odd_bus_announce(const uint8_t *dst_mac, uint64_t dst_id)
{
    return send_identity(ODD_MSG_ANNOUNCE, dst_mac, dst_id);
}

void odd_bus_input(const uint8_t src_mac[6], const uint8_t *frame, size_t len, int8_t rssi)
{
    odd_message_t msg;
    switch (odd_decode(frame, len, &msg)) {
    case ODD_DECODE_OK:
        break;
    case ODD_DECODE_NOT_ODD:
        s_stats.rx_not_odd++;
        return;
    case ODD_DECODE_BAD_VERSION: {
        s_stats.rx_bad_version++;
        const uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now - s_last_version_log_ms > BAD_VERSION_LOG_GAP_MS) {
            s_last_version_log_ms = now;
            ESP_LOGW(TAG, "rejected ODD BUS v%u frame (we speak v%d)", msg.hdr.version, ODD_BUS_PROTOCOL_VERSION);
        }
        return;
    }
    case ODD_DECODE_BAD_CRC:
        s_stats.rx_bad_crc++;
        return;
    case ODD_DECODE_MALFORMED:
    default:
        s_stats.rx_malformed++;
        return;
    }

    if (msg.hdr.src_id == s_self.id) {
        s_stats.rx_own++;
        return;
    }
    if (msg.hdr.dst_id != ODD_ID_ANY && msg.hdr.dst_id != s_self.id) {
        s_stats.rx_not_for_us++;
        return;
    }
    memcpy(msg.src_mac, src_mac, 6);
    msg.rssi = rssi;
    s_stats.rx++;
    s_stats.per_type_rx[msg.hdr.type]++;
    s_on_rx(&msg, s_rx_ctx);
}

void odd_bus_get_stats(odd_bus_stats_t *out)
{
    if (out) {
        *out = s_stats;
    }
}
