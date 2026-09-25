/*
 * MAO devices: ODD BUS controller side.
 *
 *   ESP-NOW -> mao_radio -> (queue) -> mao_devices task -> ODD BUS decode
 *           -> registry / command engine -> MAO event bus -> mao_app
 *
 * Owns a RAM-only registry of nearby ODD devices, discovery pacing,
 * online/offline tracking, and optimistic value control with sequence-matched
 * ACKs and bounded retries. Knows capabilities, never device kinds.
 * All functions are safe from any task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "odd_capability.h"
#include "odd_device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_DEVICES_MAX 8

typedef struct {
    odd_capability_t cap;
    int32_t value;          /* what MAO shows: desired (optimistic) value */
    int32_t confirmed;      /* last value the device confirmed */
    bool known;             /* confirmed at least once */
    bool pending;           /* a change is not yet confirmed */
} mao_device_cap_t;

/* Consistent copy of one registry entry for the UI / app. */
typedef struct {
    odd_device_info_t info;
    uint8_t mac[6];
    bool online;
    bool described;         /* capabilities and state received */
    bool link_problem;      /* recent commands went unconfirmed */
    int8_t rssi;
    uint32_t last_seen_ms;
    uint8_t cap_count;
    mao_device_cap_t caps[ODD_MAX_CAPS];
} mao_device_t;

/* Generic control model derived from capabilities (mao_device_view.c):
 * the dial drives one bounded LEVEL-like value, a press toggles one
 * POWER-like value. Either may be absent. */
typedef struct {
    int level_idx;          /* index into caps[], -1 if none */
    int toggle_idx;         /* index into caps[], -1 if none */
} mao_device_controls_t;

typedef struct {
    uint32_t discoveries_sent;
    uint32_t announces_rx;
    uint32_t commands_sent;     /* distinct SET_VALUE commands */
    uint32_t acks_rx;
    uint32_t acks_stale;        /* ACK for a superseded / unknown seq */
    uint32_t retries;
    uint32_t timeouts;          /* commands abandoned after all retries */
    uint32_t coalesced;         /* dial updates folded into a later command */
    uint32_t inbox_dropped;
    uint32_t devices_online;
    uint32_t rtt_count;
    uint32_t rtt_min_us, rtt_max_us;
    uint64_t rtt_sum_us;
    uint32_t input_to_ack_max_us;
    uint64_t input_to_ack_sum_us;
    uint32_t input_to_ack_count;
} mao_devices_stats_t;

/* Bring up radio + ODD BUS and start discovery (slow cadence). */
esp_err_t mao_devices_init(void);

/* Fast discovery and short offline timeout while device views are open. */
void mao_devices_set_active(bool active);

int mao_devices_count(void);
bool mao_devices_get(int index, mao_device_t *out);
int mao_devices_find(uint64_t id);

/* Optimistic set: the returned snapshot value changes immediately; the
 * command is coalesced (<= 25 ms) and confirmed by ACK. */
esp_err_t mao_devices_set_value(uint64_t id, uint8_t cap_id, int32_t value);

/* Reachability probe (transfer handshake): sends GET_STATE to the device.
 * Any answer updates last_seen_ms; the caller polls the snapshot to see
 * whether the device really responded. ESP_ERR_NOT_FOUND if unknown. */
esp_err_t mao_devices_refresh(uint64_t id);

void mao_device_controls(const mao_device_t *dev, mao_device_controls_t *out);

void mao_devices_get_stats(mao_devices_stats_t *out);
void mao_devices_reset_latency(void);

/* Development: broadcast DISCOVER at 50 Hz for the given time (radio load test). */
void mao_devices_debug_flood(uint32_t duration_ms);
void mao_devices_log_status(void);

#ifdef __cplusplus
}
#endif
