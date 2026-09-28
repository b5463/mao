/*
 * ODD BUS: the common protocol of ODD JOBS devices (MAO, LAMP, KINO, CLOCK...).
 *
 * This component is transport- and product-agnostic: it encodes, validates
 * and decodes messages and keeps protocol counters. The owner supplies a send
 * function (ESP-NOW today) and feeds received frames in from a normal task,
 * never from a radio callback.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "odd_capability.h"
#include "odd_device.h"
#include "odd_message.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ODD_BUS_PROTOCOL_VERSION  1

/* Development radio channel shared by every ODD device (fixed in M2; no
 * channel coordination yet). */
#define ODD_BUS_DEV_CHANNEL       1

/* Transport: dst_mac NULL = broadcast. */
typedef esp_err_t (*odd_send_fn_t)(const uint8_t *dst_mac, const uint8_t *frame, size_t len, void *ctx);

/* Called for every valid message addressed to us (or to anyone). */
typedef void (*odd_rx_fn_t)(const odd_message_t *msg, void *ctx);

/* Called for a frame whose header parsed but whose payload is malformed
 * (M4.0: lets the owner attribute an invalid description to a device). The
 * header is only a claim: the owner decides whether it is authenticated. */
typedef void (*odd_malformed_fn_t)(const odd_header_t *hdr, void *ctx);
void odd_bus_set_malformed_handler(odd_malformed_fn_t fn, void *ctx);

typedef struct {
    uint32_t tx;
    uint32_t tx_errors;
    uint32_t rx;               /* valid messages delivered */
    uint32_t rx_not_odd;       /* wrong magic / too short */
    uint32_t rx_bad_version;
    uint32_t rx_bad_crc;
    uint32_t rx_malformed;
    uint32_t rx_not_for_us;    /* dst_id set to another device */
    uint32_t rx_own;           /* our own broadcast echoed back */
    uint32_t per_type_tx[ODD_MSG_TYPE_MAX + 1];
    uint32_t per_type_rx[ODD_MSG_TYPE_MAX + 1];
} odd_bus_stats_t;

esp_err_t odd_bus_init(const odd_device_info_t *self, odd_send_fn_t send, void *send_ctx,
                       odd_rx_fn_t on_rx, void *rx_ctx);

const odd_device_info_t *odd_bus_self(void);

/* Feed one received frame (task context). */
void odd_bus_input(const uint8_t src_mac[6], const uint8_t *frame, size_t len, int8_t rssi);

/* Send a message with a fresh sequence number (returned via seq_out). body
 * may be NULL for empty payloads. dst_mac NULL = broadcast. */
esp_err_t odd_bus_send(const uint8_t *dst_mac, uint64_t dst_id, odd_msg_type_t type,
                       const odd_message_t *body, uint16_t *seq_out);

/* Re-send with an explicit sequence number (retries reuse the original seq so
 * the receiver can recognise duplicates). */
/* Incarnation-aware send: sets ODD_FRAME_F_INCARNATION and prefixes the
 * payload with `incarnation` (SESSION_OPEN / SET_VALUE / ACK only). */
esp_err_t odd_bus_send_session(const uint8_t *dst_mac, uint64_t dst_id, odd_msg_type_t type,
                               const odd_message_t *body, uint64_t incarnation, uint16_t *seq_out);
esp_err_t odd_bus_send_session_seq(const uint8_t *dst_mac, uint64_t dst_id, odd_msg_type_t type,
                                   const odd_message_t *body, uint64_t incarnation, uint16_t seq);

/* Development only: force the next sequence number (tests sequence shapes
 * and wrap against a real peer). */
void odd_bus_debug_set_seq(uint16_t seq);

esp_err_t odd_bus_send_seq(const uint8_t *dst_mac, uint64_t dst_id, odd_msg_type_t type,
                           const odd_message_t *body, uint16_t seq);

/* Convenience: our identity as DISCOVER / ANNOUNCE. */
esp_err_t odd_bus_discover(void);
esp_err_t odd_bus_announce(const uint8_t *dst_mac, uint64_t dst_id);
/* DISCOVER addressed to one device (a controller's direct identity query).
 * Same message and wire format as the broadcast DISCOVER. */
esp_err_t odd_bus_discover_to(const uint8_t *dst_mac, uint64_t dst_id);

void odd_bus_get_stats(odd_bus_stats_t *out);

/* Codec self-test (no radio). Returns the number of failed checks. */
int odd_bus_selftest(void);

/* Discovery pacing helper for controllers. */
typedef struct {
    uint32_t interval_ms;
    uint32_t next_ms;
    uint32_t sent;
} odd_discovery_t;

void odd_discovery_init(odd_discovery_t *d, uint32_t interval_ms);
/* Change interval; if shorter, the next round is brought forward. */
void odd_discovery_set_interval(odd_discovery_t *d, uint32_t interval_ms, uint32_t now_ms);
/* Send DISCOVER if due. Returns ms until the next round. */
uint32_t odd_discovery_poll(odd_discovery_t *d, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
