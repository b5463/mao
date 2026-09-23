/*
 * MAO radio: Wi-Fi (station mode, never associated) + ESP-NOW transport.
 *
 * Knows nothing about ODD BUS. Received frames are handed to one registered
 * handler which runs in the Wi-Fi task: it must only copy/queue the bytes.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_RADIO_MAX_PAYLOAD 250   /* ESP-NOW v1 frame limit */

/* Called from the Wi-Fi task. Copy the data and return quickly. */
typedef void (*mao_radio_rx_handler_t)(const uint8_t src_mac[6], const uint8_t *data, size_t len,
                                       int8_t rssi, void *ctx);

typedef struct {
    uint32_t tx_frames;        /* esp_now_send accepted */
    uint32_t tx_failed;        /* send callback reported no MAC-level ACK (unicast) */
    uint32_t tx_rejected;      /* esp_now_send returned an error */
    uint32_t rx_frames;
    uint32_t rx_dropped;       /* handler reported it had no room */
} mao_radio_stats_t;

/* Bring up Wi-Fi STA on a fixed channel and ESP-NOW. */
esp_err_t mao_radio_init(uint8_t channel);

/* Register the receive handler (one). */
void mao_radio_set_rx_handler(mao_radio_rx_handler_t handler, void *ctx);

/* For the handler: count a frame it had no room for. */
void mao_radio_count_rx_drop(void);

/* Send one frame. dst_mac NULL = broadcast. Unicast peers are added on demand. */
esp_err_t mao_radio_send(const uint8_t *dst_mac, const void *data, size_t len);

void mao_radio_get_mac(uint8_t mac[6]);
void mao_radio_get_stats(mao_radio_stats_t *out);

#ifdef __cplusplus
}
#endif
