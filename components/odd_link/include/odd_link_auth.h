/*
 * Link sessions (docs/link_security.md, "Link session") and the device-side
 * authorization store.
 *
 * HELLO helpers are used by both roles. odl_devauth_t is a device's whole
 * security state: the one authorized controller (active credential), a
 * pending replacement from a completed ceremony (commit-last), the current
 * link session and the envelope gate for operational traffic.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "odd_link.h"
#include "odd_link_pair.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HELLO (both roles) ---- */

/* Controller: a HELLO frame for cred with a fresh nonce_c (kept in nonce_c). */
size_t odl_hello_build(const odl_credential_t *cred, uint64_t self_id, const uint8_t self_mac[6],
                       uint8_t nonce_c[ODL_NONCE_LEN], uint8_t out[ODL_MAX_FRAME]);
/* Controller: verify a HELLO_ACK from src_mac for the nonce_c we sent; on
 * success derive the session keys. */
bool odl_hello_ack_verify(const odl_credential_t *cred, uint64_t self_id, const uint8_t self_mac[6],
                          const uint8_t nonce_c[ODL_NONCE_LEN], const odl_msg_t *ack, const uint8_t src_mac[6],
                          odl_session_keys_t *keys);

/* ---- device authorization ---- */

typedef enum { ODL_SLOT_ACTIVE = 0, ODL_SLOT_PENDING = 1 } odl_slot_t;

typedef struct {
    bool (*store)(void *ctx, odl_slot_t slot, const odl_credential_t *c);   /* persist + commit */
    bool (*erase)(void *ctx, odl_slot_t slot);
    /* Encrypted ESP-NOW peer for the controller with this session's LMK;
     * lmk NULL removes it. */
    void (*peer)(void *ctx, const uint8_t mac[6], const uint8_t *lmk);
    void (*send)(void *ctx, const uint8_t *frame, size_t len);             /* broadcast */
    void *ctx;
} odl_devauth_ops_t;

#define ODL_HELLO_SEEN 8

typedef struct {
    odl_devauth_ops_t ops;
    uint64_t self_id;
    uint8_t self_mac[6];
    bool has_active, has_pending;
    odl_credential_t active, pending;
    odl_session_t sess;
    uint8_t sess_mac[6];
    uint8_t last_nc[ODL_NONCE_LEN];          /* the HELLO the current session answers */
    uint8_t last_ack[ODL_MAX_FRAME];
    size_t last_ack_len;
    uint8_t seen[ODL_HELLO_SEEN][ODL_NONCE_LEN];
    uint8_t seen_i;
    uint32_t hello_ok, hello_bad, promoted, discarded;
} odl_devauth_t;

typedef enum {
    ODL_HELLO_IGNORED = 0,       /* not ours / malformed */
    ODL_HELLO_REFUSED,           /* no valid credential for it (wrong key, wrong radio, stranger) */
    ODL_HELLO_REPLAYED,          /* an old HELLO: dropped */
    ODL_HELLO_REPEATED,          /* the current session's HELLO again: same ACK resent */
    ODL_HELLO_NEW_SESSION,       /* new session (active credential) */
    ODL_HELLO_PROMOTED,          /* new session under the PENDING credential: it is now active */
} odl_hello_result_t;

void odl_devauth_init(odl_devauth_t *d, const odl_devauth_ops_t *ops, uint64_t self_id, const uint8_t self_mac[6],
                      const odl_credential_t *active, const odl_credential_t *pending);
/* The ceremony's persist callback on the device: store as PENDING. */
bool odl_devauth_set_pending(odl_devauth_t *d, const odl_credential_t *c);
odl_hello_result_t odl_devauth_hello(odl_devauth_t *d, const odl_msg_t *m, const uint8_t src_mac[6]);
/* An authorized controller exists (operational plaintext is then refused). */
bool odl_devauth_locked(const odl_devauth_t *d);
bool odl_devauth_has_session(const odl_devauth_t *d);
/* Operational traffic: envelope from the authorized controller's radio only. */
odl_rx_t odl_devauth_unwrap(odl_devauth_t *d, const uint8_t src_mac[6], const uint8_t *f, size_t len,
                            const uint8_t **inner, size_t *inner_len);
size_t odl_devauth_wrap(odl_devauth_t *d, const uint8_t *inner, size_t len, uint8_t out[ODL_MAX_FRAME]);
/* Is dst the controller of the live session (send enveloped, unicast)? */
bool odl_devauth_is_session_peer(const odl_devauth_t *d, const uint8_t mac[6]);
/* Forget the controller entirely (revocation / reset): both slots, session, peer. */
bool odl_devauth_revoke(odl_devauth_t *d);
const char *odl_hello_result_name(odl_hello_result_t r);

#ifdef __cplusplus
}
#endif
