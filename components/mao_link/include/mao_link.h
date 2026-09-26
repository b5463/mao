/*
 * MAO link security glue (below mao_devices, above mao_radio): the secure
 * link sessions with paired devices, their encrypted ESP-NOW peers, the
 * authenticated DATA envelope, and MAO's side of the pairing ceremony. The
 * protocol itself is the shared odd_link component (docs/link_security.md).
 *
 * Credentials are pushed in by mao_relationships (which persists them); this
 * module never touches NVS. Everything secret stays in here.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "odd_link_pair.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mao_link_init(void);

/* ---- credentials (from mao_relationships) ---- */
/* The ceremony's commit point on MAO: persist the relationship + credential
 * (mao_relationships registers this). Must return true only once committed. */
typedef bool (*mao_link_persist_fn)(uint64_t id, const char *name, uint16_t type, const uint8_t mac[6],
                                    const uint8_t key[32]);
void mao_link_set_persist(mao_link_persist_fn fn);
void mao_link_set_credential(uint64_t id, const uint8_t mac[6], const uint8_t key[32]);
void mao_link_clear(uint64_t id);
/* A credential exists for id: its traffic must be authenticated. */
bool mao_link_requires_auth(uint64_t id);

/* ---- radio path (mao_devices task) ---- */
typedef enum {
    MAO_LINK_RX_DROP = 0,        /* refused (counted) */
    MAO_LINK_RX_CONSUMED,        /* a link frame, handled here */
    MAO_LINK_RX_ODD_PLAIN,       /* plaintext ODD: hand to ODD BUS, NOT authenticated */
    MAO_LINK_RX_ODD_AUTH,        /* ODD frame out of a valid envelope from peer_id */
} mao_link_rx_t;

mao_link_rx_t mao_link_rx(const uint8_t mac[6], const uint8_t *frame, size_t len, bool bcast,
                          const uint8_t **odd, size_t *odd_len, uint64_t *peer_id);
/* ODD BUS send hook: enveloped unicast within a secure session; plaintext
 * broadcast for everything else; refused for a paired device without a
 * session (no plaintext fallback). */
esp_err_t mao_link_tx(const uint8_t *dst_mac, const uint8_t *frame, size_t len);
/* Plaintext ANNOUNCE from a paired id: "a candidate is nearby" (never proof). */
void mao_link_hint(uint64_t id, const uint8_t mac[6]);
/* Called by the link layer when a secure session is up (describe it again,
 * over the secure path). */
void mao_link_set_secure_cb(void (*cb)(uint64_t id, const uint8_t mac[6]));
/* Send an ODD identity query to id inside its session (a secure probe). */
void mao_link_set_probe_cb(void (*cb)(uint64_t id, const uint8_t mac[6]));
/* For each paired device with a live session (liveness probes). */
void mao_link_foreach_secure(void (*cb)(uint64_t id, const uint8_t mac[6]));

/* ---- state for the device world ---- */
typedef enum {
    MAO_LINK_NONE = 0,           /* no credential */
    MAO_LINK_OFFLINE,            /* credential, nothing heard */
    MAO_LINK_VERIFYING,          /* a secure hello is in flight */
    MAO_LINK_SECURE,             /* proven this session */
    MAO_LINK_FAILED,             /* heard, but it could not prove the stored identity */
} mao_link_state_t;
mao_link_state_t mao_link_state(uint64_t id);
const char *mao_link_state_name(mao_link_state_t s);

/* ---- pairing ceremony (app) ---- */
typedef struct {
    uint64_t dev_id;
    odl_c_state_t st;
    odl_fail_t fail;
    uint32_t sas;                /* valid from SAS_READY */
} mao_link_pair_status_t;

/* Start a ceremony with a device heard at dev_mac. name/type describe it
 * for the relationship created at the commit point. */
esp_err_t mao_link_pair_start(uint64_t dev_id, const uint8_t dev_mac[6], const char *name, uint16_t type);
void mao_link_pair_confirm(void);            /* the user saw the same code: MATCH */
void mao_link_pair_cancel(void);
void mao_link_pair_reset(void);              /* after the UI consumed a terminal state */
void mao_link_pair_status(mao_link_pair_status_t *out);

/* ---- revocation (FORGET) ---- */
/* Ask a device with a live session to drop its authorization. Completion is
 * reported with MAO_EVENT_LINK_CHANGED (value MAO_LINK_EV_REVOKED) and
 * mao_link_revoke_confirmed(). Returns ESP_ERR_INVALID_STATE without a
 * session (nothing can be proven: forget locally). */
esp_err_t mao_link_revoke(uint64_t id);
bool mao_link_revoke_busy(void);
bool mao_link_revoke_confirmed(uint64_t *id);

/* MAO_EVENT_LINK_CHANGED values */
enum { MAO_LINK_EV_STATE = 1, MAO_LINK_EV_PAIR = 2, MAO_LINK_EV_REVOKED = 3 };

#ifdef __cplusplus
}
#endif
