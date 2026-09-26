/*
 * MAO device relationships: which devices the user has deliberately made
 * part of MAO's setup (KNOWN), as opposed to devices MAO merely hears
 * (DISCOVERED).
 *
 *   mao_radio -> odd_bus -> mao_devices (live, transient registry)
 *                                \
 *                                 mao_relationships (persistent, NVS "mao_rel")
 *                                /
 *                   mao_app / mao_ui (merged view, mao_world.h)
 *
 * PAIR in M3.0 means "create a persistent known-device relationship". It
 * does NOT authenticate the device or the controller and does NOT encrypt
 * anything: the stable device_id is not cryptographic proof of identity.
 *
 * The relationship is keyed by device_id only; name and type are last-known
 * description. Records are loaded once at boot and served from RAM; flash is
 * written only on PAIR, FORGET and a real name/type change.
 * All functions are safe from any task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "odd_device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_REL_MAX_KNOWN 8

typedef struct {
    uint64_t id;
    char name[ODD_NAME_MAX + 1];    /* last known name */
    uint16_t device_type;           /* last known odd_device_type_t */
    uint32_t order;                 /* stable pair order */
    bool has_cred;                  /* PAIRED_AUTHENTICATED (else KNOWN_UNVERIFIED) */
} mao_rel_info_t;

/* Load the relationship DB (NVS must be initialised). Never fails boot: a
 * missing or unreadable namespace means "no known devices yet". */
esp_err_t mao_rel_init(void);

bool mao_rel_is_known(uint64_t id);
bool mao_rel_get(uint64_t id, mao_rel_info_t *out);
int mao_rel_count(void);
/* Known devices in stable pair order; returns the count. */
int mao_rel_list(mao_rel_info_t out[MAO_REL_MAX_KNOWN]);

/* M3.0-style relationship WITHOUT a credential (development only since
 * M3.1: product pairing creates the relationship at the ceremony's commit
 * point, mao_link -> mao_rel persist). KNOWN only once committed:
 * ESP_OK (also for an already-known id: idempotent, order kept),
 * ESP_ERR_NO_MEM (full: nothing is evicted), or a storage error. */
esp_err_t mao_rel_pair(uint64_t id, const char *name, uint16_t type);
/* Deliberate user act: ESP_OK once the record is erased and committed. */
esp_err_t mao_rel_forget(uint64_t id);
/* Live ANNOUNCE of any device: for a known one whose name/type changed, the
 * fallback metadata is updated (one write). No-op for strangers. */
void mao_rel_note_live(uint64_t id, const char *name, uint16_t type);

/* Dev: drop the stored credential (MAO key loss) or corrupt it (wrong key). */
esp_err_t mao_rel_debug_drop_key(uint64_t id);
esp_err_t mao_rel_debug_corrupt_key(uint64_t id);

/* Successful relationship writes (pair/forget/metadata) since boot. */
uint32_t mao_rel_write_count(void);

#ifdef __cplusplus
}
#endif
