/*
 * MAO's device world: the one merged model the UI shows, built from
 *   - the persistent relationship DB (mao_rel.h: who is KNOWN), and
 *   - the live ODD registry (mao_devices.h: who is ONLINE right now).
 * Merged exclusively by the stable device_id; never by name, type or row.
 *
 * Order is stable spatial memory: KNOWN devices in pair order, then
 * DISCOVERED (not known) devices that are online, in first-seen order. Never
 * re-sorted by RSSI, reachability or activity. A KNOWN device never leaves
 * the list by going offline; a DISCOVERED one leaves when the registry
 * declares it offline (and is never persisted).
 *
 * M3.1: for a device with a pair credential, "online" means PROVEN this
 * session (a secure link session) and live - never "a plaintext ANNOUNCE
 * with its device_id was heard".
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "odd_device.h"
#include "mao_link.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_WORLD_MAX 8        /* rows DEVICES can show */

typedef struct {
    uint64_t id;
    char name[ODD_NAME_MAX + 1];    /* live name when heard this boot, else last known */
    uint16_t device_type;
    bool known;                     /* relationship: KNOWN (else DISCOVERED) */
    bool has_cred;                  /* PAIRED_AUTHENTICATED (else KNOWN_UNVERIFIED when known) */
    mao_link_state_t link;          /* secure link state (MAO_LINK_NONE without a credential) */
    bool online;                    /* reachable: proven (paired) or heard (not paired) */
    bool auth_failed;               /* heard, but it could not prove the stored identity */
    int slot;                       /* live registry slot, -1 = not heard this boot */
} mao_world_entry_t;

/* The DEVICES list. Returns the count (<= MAO_WORLD_MAX). */
int mao_world_list(mao_world_entry_t out[MAO_WORLD_MAX]);
/* One device by id, whether or not it is currently in the list (a
 * discovered device that just went offline is still describable while its
 * page is open). False if MAO neither knows nor has heard it. */
bool mao_world_get(uint64_t id, mao_world_entry_t *out);

#ifdef __cplusplus
}
#endif
