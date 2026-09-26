/*
 * Pairing ceremony state machines (docs/link_security.md, "Pairing
 * ceremony"). One explicit state machine per role, one transaction at a
 * time, no I/O: frames leave through ops.send (always broadcast), time comes
 * in through tick(), and user decisions through the confirm/accept calls.
 * Everything secret is wiped on every terminal state.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "odd_link.h"
#include "odd_link_crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ODL_PAIR_RETRY_MS       400      /* retransmit period while waiting */
#define ODL_PAIR_NO_REPLY_MS    3000     /* no COMMIT for our START: device not pairing */
#define ODL_PAIR_TX_TIMEOUT_MS  45000    /* whole attempt, including both confirmations */
#define ODL_PAIR_MODE_MS        60000    /* default device pair-mode window */

/* The persistent result of a ceremony. */
typedef struct {
    uint64_t peer_id;
    uint8_t peer_mac[6];
    uint8_t k_link[ODL_KEY_LEN];
} odl_credential_t;

typedef struct {
    void (*send)(void *ctx, const uint8_t *frame, size_t len);
    /* Commit point. Controller: store the final credential. Device: store it
     * as PENDING (the old one stays active). Return false if it failed. */
    bool (*persist)(void *ctx, const odl_credential_t *cred);
    void (*changed)(void *ctx);          /* state changed (UI / log) */
    void *ctx;
} odl_pair_ops_t;

/* ---------------------------------------------------------------------- */
/* Controller (MAO)                                                       */
/* ---------------------------------------------------------------------- */

typedef enum {
    ODL_C_IDLE = 0,
    ODL_C_WAIT_COMMIT,       /* START sent, key generated */
    ODL_C_WAIT_REVEAL,       /* NONCE sent */
    ODL_C_SAS_READY,         /* both derived the SAS: waiting for the local MATCH */
    ODL_C_WAIT_ACCEPT,       /* CONFIRM sent: waiting for the device's user + tag */
    ODL_C_PAIRED,            /* credential verified and persisted */
    ODL_C_FAILED,            /* see fail */
    ODL_C_CANCELLED,
} odl_c_state_t;

typedef enum {
    ODL_F_NONE = 0,
    ODL_F_NOT_PAIRING,       /* nobody answered START: device not in pair mode */
    ODL_F_REJECTED,          /* the device's user rejected */
    ODL_F_MISMATCH,          /* commitment or confirmation tag did not verify */
    ODL_F_TIMEOUT,
    ODL_F_STORAGE,           /* persisting the credential failed */
    ODL_F_CRYPTO,
    ODL_F_REMOTE,            /* the device aborted (busy / failed) */
} odl_fail_t;

typedef struct {
    odl_c_state_t st;
    odl_fail_t fail;
    odl_pair_ops_t ops;
    odl_transcript_t t;
    olc_x25519_t eph;
    odl_pair_keys_t keys;
    uint8_t th[ODL_HASH_LEN];
    uint8_t commit[ODL_HASH_LEN];
    uint32_t started_ms, last_tx_ms;
    uint8_t last[ODL_MAX_FRAME];     /* frame being retransmitted */
    size_t last_len;
    bool debug_mismatch;             /* DEV: corrupt our transcript copy (forced SAS mismatch) */
} odl_pairc_t;

void odl_pairc_init(odl_pairc_t *c, const odl_pair_ops_t *ops);
/* Generates the ephemeral key (~0.1 s): call from a worker task. */
bool odl_pairc_start(odl_pairc_t *c, uint64_t ctrl_id, const uint8_t ctrl_mac[6], uint64_t dev_id,
                     const uint8_t dev_mac[6], uint32_t now);
/* A decoded pairing frame from radio source mac (agreement ~0.1 s). */
void odl_pairc_rx(odl_pairc_t *c, const odl_msg_t *m, const uint8_t src_mac[6], uint32_t now);
void odl_pairc_tick(odl_pairc_t *c, uint32_t now);
void odl_pairc_confirm(odl_pairc_t *c, uint32_t now);   /* the user saw the same SAS: MATCH */
void odl_pairc_cancel(odl_pairc_t *c);
bool odl_pairc_active(const odl_pairc_t *c);
/* Terminal state: back to IDLE, wipe everything. */
void odl_pairc_reset(odl_pairc_t *c);
const char *odl_c_state_name(odl_c_state_t s);
const char *odl_fail_name(odl_fail_t f);

/* ---------------------------------------------------------------------- */
/* Device (responder)                                                     */
/* ---------------------------------------------------------------------- */

typedef enum {
    ODL_D_LOCKED = 0,        /* not in pair mode: bootstrap frames are ignored */
    ODL_D_PAIR_MODE,         /* waiting for a START */
    ODL_D_EXCHANGING,        /* COMMIT sent: waiting for the NONCE */
    ODL_D_SAS_READY,         /* REVEAL sent: waiting for CONFIRM and the local accept */
    ODL_D_ACCEPTED,          /* pending credential stored, ACCEPT sent (resent on retry) */
    ODL_D_FAILED,
} odl_d_state_t;

typedef struct {
    odl_d_state_t st;
    odl_fail_t fail;
    odl_pair_ops_t ops;
    uint64_t self_id;
    uint8_t self_mac[6];
    uint32_t mode_until_ms;
    uint32_t tx_started_ms;
    odl_transcript_t t;
    olc_x25519_t eph;
    odl_pair_keys_t keys;
    uint8_t th[ODL_HASH_LEN];
    bool remote_confirmed, local_accepted;
    uint8_t accept_frame[ODL_MAX_FRAME];
    size_t accept_len;
    bool debug_mismatch;
} odl_paird_t;

void odl_paird_init(odl_paird_t *d, const odl_pair_ops_t *ops, uint64_t self_id, const uint8_t self_mac[6]);
/* Open (duration_ms > 0) or close pair mode. Opening never touches the
 * existing authorization; closing abandons any unfinished transaction. */
void odl_paird_mode(odl_paird_t *d, uint32_t duration_ms, uint32_t now);
void odl_paird_rx(odl_paird_t *d, const odl_msg_t *m, const uint8_t src_mac[6], uint32_t now);
void odl_paird_tick(odl_paird_t *d, uint32_t now);
void odl_paird_accept(odl_paird_t *d, uint32_t now);   /* local user: the codes match */
void odl_paird_reject(odl_paird_t *d);
bool odl_paird_in_mode(const odl_paird_t *d);
const char *odl_d_state_name(odl_d_state_t s);

#ifdef __cplusplus
}
#endif
