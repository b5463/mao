/*
 * ODD link crypto: the only primitives the link layer uses, as thin wrappers
 * around PSA Crypto (TF-PSA-Crypto in ESP-IDF 6.0). Nothing here is a new
 * primitive; every function maps onto one standard PSA operation.
 *
 * All functions return 0 on success, negative on failure. Multi-part inputs
 * are passed as a list of (pointer, length) parts, hashed/MACed in order.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const void *p;
    size_t n;
} olc_part_t;

#define OLC_PARTS(...) ((const olc_part_t[]){ __VA_ARGS__ }), \
                       (int)(sizeof((const olc_part_t[]){ __VA_ARGS__ }) / sizeof(olc_part_t))

/* Cryptographically secure random bytes (hardware RNG on the ESP32-C3). */
int olc_random(void *out, size_t n);
/* SHA-256 over the parts. */
int olc_sha256(const olc_part_t *parts, int count, uint8_t out[32]);
/* HMAC-SHA-256(key) over the parts; full 32-byte tag. */
int olc_hmac(const uint8_t *key, size_t key_len, const olc_part_t *parts, int count, uint8_t out[32]);
/* HKDF-SHA-256 (RFC 5869). salt may be NULL / 0 (HKDF then uses zeros). */
int olc_hkdf(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
             const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len);

/* X25519 (RFC 7748) ephemeral key pair. The private key lives inside PSA
 * (volatile key slot) and never leaves it. */
typedef struct {
    uint32_t id;            /* psa_key_id_t, 0 = none */
    uint8_t pub[32];
} olc_x25519_t;

int olc_x25519_generate(olc_x25519_t *k);
/* Development / test vectors only: import a fixed private scalar. */
int olc_x25519_import(olc_x25519_t *k, const uint8_t priv[32]);
/* Shared secret with peer_pub. Fails on the all-zero result (low-order
 * input), as RFC 7748 §6.1 recommends. */
int olc_x25519_agree(const olc_x25519_t *k, const uint8_t peer_pub[32], uint8_t out[32]);
void olc_x25519_destroy(olc_x25519_t *k);

/* Wipe secrets (not optimised away). */
void olc_wipe(void *p, size_t n);
/* Constant-time equality. */
bool olc_equal(const void *a, const void *b, size_t n);

#ifdef __cplusplus
}
#endif
