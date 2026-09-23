/*
 * ESP-NOW glue. Callbacks run in the Wi-Fi task: the receive path does a
 * minimal size check and hands the bytes to the registered handler (which
 * copies them into its own queue); the send path only updates counters.
 */
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "mao_radio.h"
#include "mao_radio_priv.h"

static const char *TAG = "MAO_RADIO";

static const uint8_t kBroadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static mao_radio_rx_handler_t s_rx_handler;
static void *s_rx_ctx;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static mao_radio_stats_t s_stats;

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len <= 0 || len > MAO_RADIO_MAX_PAYLOAD) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_stats.rx_frames++;
    portEXIT_CRITICAL(&s_lock);
    if (s_rx_handler) {
        const int8_t rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
        s_rx_handler(info->src_addr, data, (size_t)len, rssi, s_rx_ctx);
    }
}

static void on_sent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    if (status != ESP_NOW_SEND_SUCCESS) {
        portENTER_CRITICAL(&s_lock);
        s_stats.tx_failed++;
        portEXIT_CRITICAL(&s_lock);
    }
}

static esp_err_t ensure_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }
    esp_now_peer_info_t peer = {
        .channel = 0,             /* current channel */
        .ifidx = WIFI_IF_STA,
        .encrypt = false,         /* development network: see README */
    };
    memcpy(peer.peer_addr, mac, 6);
    return esp_now_add_peer(&peer);
}

esp_err_t mao_espnow_init(void)
{
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now_init");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_recv), TAG, "recv cb");
    ESP_RETURN_ON_ERROR(esp_now_register_send_cb(on_sent), TAG, "send cb");
    ESP_RETURN_ON_ERROR(ensure_peer(kBroadcast), TAG, "broadcast peer");
    return ESP_OK;
}

void mao_radio_set_rx_handler(mao_radio_rx_handler_t handler, void *ctx)
{
    s_rx_ctx = ctx;
    s_rx_handler = handler;
}

void mao_radio_count_rx_drop(void)
{
    portENTER_CRITICAL(&s_lock);
    s_stats.rx_dropped++;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t mao_radio_send(const uint8_t *dst_mac, const void *data, size_t len)
{
    const uint8_t *dst = dst_mac ? dst_mac : kBroadcast;
    esp_err_t err = ensure_peer(dst);
    if (err == ESP_OK) {
        err = esp_now_send(dst, data, len);
    }
    portENTER_CRITICAL(&s_lock);
    if (err == ESP_OK) {
        s_stats.tx_frames++;
    } else {
        s_stats.tx_rejected++;
    }
    portEXIT_CRITICAL(&s_lock);
    return err;
}

void mao_radio_get_stats(mao_radio_stats_t *out)
{
    if (out) {
        portENTER_CRITICAL(&s_lock);
        *out = s_stats;
        portEXIT_CRITICAL(&s_lock);
    }
}
