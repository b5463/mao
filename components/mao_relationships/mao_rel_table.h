/*
 * Relationship table: the in-memory model of MAO's KNOWN devices and its
 * persistence rules, independent of the storage backend (NVS on target, a
 * fake in the host tests).
 *
 * A record is a persistent local user choice ("this device is part of my
 * setup"), keyed by the stable ODD device_id. It is NOT authentication.
 * Only descriptive fallback metadata is stored; everything live (online,
 * capabilities, values, sessions) stays in the ODD registry.
 *
 * Every change is persisted first and applied to RAM only when the backend
 * reports success, so the table always equals what the next boot will load.
 *
 * Schema 2 (M3.1) adds the pair credential of an authenticated relationship
 * (docs/link_security.md): the bound radio MAC and K_link, in the same blob
 * as the relationship, so both always change together. Schema 1 records
 * (M3.0) load as KNOWN but unverified and are not rewritten until changed.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "odd_device.h"

#define MAO_REL_MAX        8       /* matches MAO_DEVICES_MAX: one setup, one budget */
#define MAO_REL_SCHEMA     2
#define MAO_REL_SCHEMA_V1  1
#define MAO_REL_BLOB_LEN_V1 33     /* schema(1) rsvd(1) type(2) order(4) id(8) name(17) */
#define MAO_REL_BLOB_LEN   73      /* v1 fields + auth(1) peer_mac(6) cred_gen(1) k_link(32) */
#define MAO_REL_KEY_LEN    32
#define MAO_REL_ID_MARK    0x0DD0u /* top 16 bits of every ODD device_id */

typedef struct {
    uint64_t id;                    /* stable ODD device_id: the relationship key */
    char name[ODD_NAME_MAX + 1];    /* last known display name (fallback while offline) */
    uint16_t device_type;           /* last known odd_device_type_t (description only) */
    uint32_t order;                 /* stable pair order (presentation) */
    /* Pair credential (schema 2). has_cred false = KNOWN_UNVERIFIED. */
    bool has_cred;
    uint8_t peer_mac[6];            /* the radio bound at pairing */
    uint8_t cred_gen;               /* +1 on every re-pair */
    uint8_t k_link[MAO_REL_KEY_LEN];
    bool cred_dropped;              /* RAM only: a stored credential was malformed and ignored */
} mao_rel_record_t;

/* Storage: one blob per slot. Each call must be durable (committed) when it
 * returns ESP_OK. */
typedef struct {
    esp_err_t (*write)(void *ctx, int slot, const uint8_t blob[MAO_REL_BLOB_LEN]);
    esp_err_t (*erase)(void *ctx, int slot);
    void *ctx;
} mao_rel_backend_t;

typedef enum {
    MAO_REL_SLOT_FREE = 0,
    MAO_REL_SLOT_USED,
    MAO_REL_SLOT_RESERVED,   /* holds a record of an unknown (newer) schema: never overwritten */
} mao_rel_slot_state_t;

typedef struct {
    uint8_t state[MAO_REL_MAX];     /* mao_rel_slot_state_t */
    mao_rel_record_t rec[MAO_REL_MAX];
    uint32_t next_order;
    uint32_t writes;                /* successful backend writes/erases since init */
    const mao_rel_backend_t *be;
} mao_rel_table_t;

typedef enum {
    MAO_REL_LOAD_OK = 0,
    MAO_REL_LOAD_BAD_SIZE,
    MAO_REL_LOAD_UNKNOWN_SCHEMA,
    MAO_REL_LOAD_BAD_ID,
    MAO_REL_LOAD_BAD_NAME,
    MAO_REL_LOAD_DUPLICATE,
} mao_rel_load_t;

void mao_rel_table_init(mao_rel_table_t *t, const mao_rel_backend_t *be);
/* Boot: offer the blob stored in one slot. Invalid records are skipped (the
 * slot stays reusable); unknown-schema records reserve their slot. */
mao_rel_load_t mao_rel_table_load_slot(mao_rel_table_t *t, int slot, const uint8_t *blob, size_t len);
const char *mao_rel_load_name(mao_rel_load_t r);

int mao_rel_table_find(const mao_rel_table_t *t, uint64_t id);     /* slot, or -1 */
int mao_rel_table_count(const mao_rel_table_t *t);
/* Records in stable pair order. Returns the count. */
int mao_rel_table_list(const mao_rel_table_t *t, mao_rel_record_t out[MAO_REL_MAX]);

/* Create a relationship. Idempotent for a known id (metadata refreshed if it
 * changed, order kept). ESP_ERR_NO_MEM when full (nothing is evicted),
 * ESP_ERR_INVALID_ARG for a malformed id or name, backend errors as-is. */
esp_err_t mao_rel_table_pair(mao_rel_table_t *t, uint64_t id, const char *name, uint16_t type, bool *created);
/* ESP_ERR_NOT_FOUND when not known. */
esp_err_t mao_rel_table_forget(mao_rel_table_t *t, uint64_t id);
/* Live description of a known device: persisted only when name/type differ.
 * ESP_ERR_NOT_FOUND when not known (nothing is written for strangers). */
esp_err_t mao_rel_table_note(mao_rel_table_t *t, uint64_t id, const char *name, uint16_t type, bool *changed);
/* Commit a pair credential (the ceremony's commit point on MAO). Creates the
 * relationship if it does not exist (new device), otherwise upgrades /
 * replaces the credential keeping order and metadata (cred_gen + 1).
 * RAM changes only after the backend confirms. */
esp_err_t mao_rel_table_set_cred(mao_rel_table_t *t, uint64_t id, const char *name, uint16_t type,
                                 const uint8_t mac[6], const uint8_t key[MAO_REL_KEY_LEN], bool *created);
/* Drop the credential, keep the relationship (development: MAO key loss). */
esp_err_t mao_rel_table_clear_cred(mao_rel_table_t *t, uint64_t id);

void mao_rel_encode(const mao_rel_record_t *r, uint8_t out[MAO_REL_BLOB_LEN]);
mao_rel_load_t mao_rel_decode(const uint8_t *blob, size_t len, mao_rel_record_t *out);
