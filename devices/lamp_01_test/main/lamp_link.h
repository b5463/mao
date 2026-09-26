/*
 * LAMP 01 / CAMERA 01 test endpoint: ODD link security glue (the device side
 * of docs/link_security.md). Profile-independent: the authorization lives in
 * NVS "odd_sec" and survives CAMERA <-> LIGHT reflashes of the same board.
 * Everything runs on the lamp task except the console, which posts commands.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void lamp_link_init(uint64_t self_id, const uint8_t self_mac[6]);
/* A received radio frame (task context). Link frames are handled here; ODD
 * frames that pass the gate are handed to odd_bus_input(). */
void lamp_link_rx(const uint8_t mac[6], const uint8_t *data, size_t len, int8_t rssi, bool bcast);
/* ODD BUS send hook: enveloped unicast to the session controller, otherwise
 * plaintext broadcast - and only what an authorized device may say in
 * plaintext (ANNOUNCE). */
int lamp_link_tx(const uint8_t *dst, const uint8_t *frame, size_t len);
/* The ODD frame being processed arrived authenticated (or the device has no
 * authorization at all, i.e. M3.0-compatible open mode). */
bool lamp_link_rx_trusted(void);
/* Periodic: pairing timers. Returns the next wanted wake-up in ms. */
uint32_t lamp_link_tick(void);
/* Console commands, executed on the lamp task. */
void lamp_link_command(int sub, int32_t value);

enum {
    LINK_CMD_PAIRMODE = 1,  /* value = seconds (0 closes) */
    LINK_CMD_ACCEPT,
    LINK_CMD_REJECT,
    LINK_CMD_STATUS,
    LINK_CMD_RESET,         /* dev: forget the authorized controller (endpoint security reset) */
    LINK_CMD_CORRUPT,       /* dev: corrupt the stored link key (wrong-key tests) */
    LINK_CMD_FORCESAS,      /* dev: value 1/0 - corrupt our transcript copy (SAS mismatch) */
    LINK_CMD_PEERPLAIN,     /* dev: make the controller's peer entry plaintext (app-gate test) */
    LINK_CMD_SELFTEST,
};
