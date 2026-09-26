/*
 * ODD link security layer (below ODD BUS). Shared by MAO and every ODD
 * device firmware, like odd_bus; it must not depend on any MAO component.
 * The design is in docs/link_security.md.
 *
 * This header holds the wire constants, the frame codec, the key schedule
 * and the DATA envelope. Pairing state machines are in odd_link_pair.h.
 * The code is plain C and host-testable; crypto goes through odd_link_crypto.h.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODL_MAGIC0          'L'
#define ODL_MAGIC1          'K'
#define ODL_VERSION         1
#define ODL_HEAD_LEN        4        /* 'L' 'K' version type */

#define ODL_TXID_LEN        8
#define ODL_PUB_LEN         32
#define ODL_NONCE_LEN       16
#define ODL_HASH_LEN        32
#define ODL_KEY_LEN         32       /* K_link, K_confirm, envelope keys */
#define ODL_LMK_LEN         16       /* ESP-NOW local master key */
#define ODL_SID_LEN         8
#define ODL_TAG_LEN         16       /* truncated HMAC-SHA-256 (HELLO, envelope) */
#define ODL_MAX_FRAME       250      /* ESP-NOW v1 payload */
#define ODL_ENV_HDR_LEN     (ODL_HEAD_LEN + ODL_SID_LEN + 4)
#define ODL_ENV_OVERHEAD    (ODL_ENV_HDR_LEN + ODL_TAG_LEN)
#define ODL_MAX_INNER       (ODL_MAX_FRAME - ODL_ENV_OVERHEAD)
#define ODL_REPLAY_WINDOW   32

/* The ESP-NOW PMK shared by every ODD device. It feeds CCMP's effective key
 * together with the per-peer LMK, so both sides must agree on it (audit
 * §7); it is NOT a secret and NOT the root of any security: that is the
 * fresh per-session LMK and the envelope keys. Derivation (documented, not
 * the undocumented IDF default): SHA-256("ODD-ESPNOW-PMK-v1")[0:16]. */
#define ODL_ESPNOW_PMK { 0xbf, 0xfc, 0x5d, 0x6d, 0xa6, 0xd5, 0x10, 0x7e, \
                         0x0d, 0xa6, 0xc0, 0xa9, 0xa8, 0x3e, 0xff, 0x89 }

typedef enum {
    ODL_PAIR_START   = 0x01,
    ODL_PAIR_COMMIT  = 0x02,
    ODL_PAIR_NONCE   = 0x03,
    ODL_PAIR_REVEAL  = 0x04,
    ODL_PAIR_CONFIRM = 0x05,
    ODL_PAIR_ACCEPT  = 0x06,
    ODL_PAIR_ABORT   = 0x07,
    ODL_HELLO        = 0x10,
    ODL_HELLO_ACK    = 0x11,
    ODL_DATA         = 0x20,
} odl_type_t;

typedef enum {
    ODL_ABORT_CANCELLED = 1,    /* the user cancelled */
    ODL_ABORT_REJECTED  = 2,    /* the device's user rejected */
    ODL_ABORT_MISMATCH  = 3,    /* commitment or confirmation did not verify */
    ODL_ABORT_BUSY      = 4,    /* another transaction is running */
    ODL_ABORT_FAILED    = 5,    /* crypto or storage failure */
    ODL_ABORT_TIMEOUT   = 6,
} odl_abort_t;

/* Link control carried INSIDE a DATA envelope (security management). */
#define ODL_CTL_MAGIC0      'L'
#define ODL_CTL_MAGIC1      'C'
typedef enum { ODL_CTL_REVOKE = 1, ODL_CTL_REVOKE_ACK = 2 } odl_ctl_t;

/* A decoded bootstrap / HELLO frame. */
typedef struct {
    uint8_t type;
    uint8_t txid[ODL_TXID_LEN];
    uint64_t ctrl_id, dev_id;
    uint8_t pub[ODL_PUB_LEN];
    uint8_t commit[ODL_HASH_LEN];
    uint8_t nonce[ODL_NONCE_LEN];      /* PAIR_NONCE / REVEAL; HELLO: nonce_c */
    uint8_t nonce2[ODL_NONCE_LEN];     /* HELLO_ACK: nonce_d */
    uint8_t tag[ODL_HASH_LEN];         /* CONFIRM / ACCEPT: 32 B; HELLO(_ACK): first 16 */
    uint8_t reason;
} odl_msg_t;

/* True if the frame belongs to the link namespace (never an ODD frame). */
bool odl_is_link(const uint8_t *f, size_t len);
/* Encode a non-DATA message; returns the length, 0 on error. */
size_t odl_encode(const odl_msg_t *m, uint8_t out[ODL_MAX_FRAME]);
/* Decode and length-check a non-DATA message. False for anything malformed:
 * wrong magic / version, unknown type, wrong length. */
bool odl_decode(const uint8_t *f, size_t len, odl_msg_t *out);

/* ---------------------------------------------------------------------- */
/* Key schedule                                                           */
/* ---------------------------------------------------------------------- */

typedef struct {
    uint64_t ctrl_id, dev_id;
    uint8_t mac_c[6], mac_d[6];        /* radio source addresses */
    uint8_t txid[ODL_TXID_LEN];
    uint8_t pub_c[ODL_PUB_LEN], pub_d[ODL_PUB_LEN];
    uint8_t nonce_c[ODL_NONCE_LEN], nonce_d[ODL_NONCE_LEN];
} odl_transcript_t;

typedef struct {
    uint8_t k_confirm[ODL_KEY_LEN];
    uint8_t k_link[ODL_KEY_LEN];
    uint32_t sas;                      /* 0 .. 999999 */
} odl_pair_keys_t;

typedef struct {
    uint8_t lmk[ODL_LMK_LEN];
    uint8_t k_c2d[ODL_KEY_LEN];
    uint8_t k_d2c[ODL_KEY_LEN];
    uint8_t sid[ODL_SID_LEN];
} odl_session_keys_t;

int odl_commit(const odl_transcript_t *t, uint8_t out[ODL_HASH_LEN]);
int odl_transcript_hash(const odl_transcript_t *t, uint8_t out[ODL_HASH_LEN]);
int odl_pair_keys(const uint8_t z[32], const uint8_t th[ODL_HASH_LEN], odl_pair_keys_t *out);
/* role 'C' (controller) or 'D' (device). */
int odl_confirm_tag(const uint8_t k_confirm[ODL_KEY_LEN], char role, const uint8_t th[ODL_HASH_LEN],
                    uint8_t out[ODL_HASH_LEN]);
int odl_hello_key(const uint8_t k_link[ODL_KEY_LEN], uint64_t ctrl_id, uint64_t dev_id, uint8_t out[ODL_KEY_LEN]);
/* nonce_d NULL: HELLO ("H1"); else HELLO_ACK ("H2"). */
int odl_hello_tag(const uint8_t k_hello[ODL_KEY_LEN], uint64_t ctrl_id, uint64_t dev_id, const uint8_t mac_c[6],
                  const uint8_t mac_d[6], const uint8_t nonce_c[ODL_NONCE_LEN], const uint8_t *nonce_d,
                  uint8_t out[ODL_TAG_LEN]);
int odl_session_keys(const uint8_t k_link[ODL_KEY_LEN], uint64_t ctrl_id, uint64_t dev_id, const uint8_t mac_c[6],
                     const uint8_t mac_d[6], const uint8_t nonce_c[ODL_NONCE_LEN],
                     const uint8_t nonce_d[ODL_NONCE_LEN], odl_session_keys_t *out);
/* A short non-secret identifier of a key for logs (first 4 bytes of
 * SHA-256("ODD-KEY-FP-v1" | key)). Never the key itself. */
uint32_t odl_fingerprint(const uint8_t key[ODL_KEY_LEN]);
/* "482 193" */
void odl_sas_text(uint32_t sas, char out[8]);

/* ---------------------------------------------------------------------- */
/* DATA envelope                                                          */
/* ---------------------------------------------------------------------- */

typedef struct {
    bool valid;
    uint8_t sid[ODL_SID_LEN];
    uint8_t k_tx[ODL_KEY_LEN], k_rx[ODL_KEY_LEN];
    uint32_t tx_ctr;                   /* last counter sent */
    uint32_t rx_hi;                    /* highest counter accepted */
    uint32_t rx_bits;                  /* bit i: rx_hi - i already seen */
} odl_session_t;

typedef enum {
    ODL_RX_OK = 0,
    ODL_RX_MALFORMED,
    ODL_RX_NO_SESSION,
    ODL_RX_WRONG_SESSION,
    ODL_RX_BAD_TAG,
    ODL_RX_REPLAY,
} odl_rx_t;

/* Install session keys for one side ('C' or 'D'). Wipes nothing in keys. */
void odl_session_start(odl_session_t *s, const odl_session_keys_t *k, char role);
void odl_session_end(odl_session_t *s);
/* Wrap inner (<= ODL_MAX_INNER) into a DATA frame; returns the length, 0 on error. */
size_t odl_wrap(odl_session_t *s, const uint8_t *inner, size_t inner_len, uint8_t out[ODL_MAX_FRAME]);
/* Verify a DATA frame; on ODL_RX_OK *inner points into f. The replay state
 * advances only for authentic frames. */
odl_rx_t odl_unwrap(odl_session_t *s, const uint8_t *f, size_t len, const uint8_t **inner, size_t *inner_len);
const char *odl_rx_name(odl_rx_t r);

#ifdef __cplusplus
}
#endif
