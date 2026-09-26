/*
 * TEST DOUBLE for odd_link_crypto.h - NOT CRYPTOGRAPHY.
 *
 * Deterministic stand-ins with the algebraic properties the protocol logic
 * relies on (DH symmetry, keyed outputs that change with any input bit), so
 * the state machines, codec and replay logic can be tested on the host. The
 * real primitives are verified on the target against published vectors and
 * an independent implementation (odl_selftest, tools/odd_link_vectors.py).
 */
#include "odd_link_crypto.h"

#include <string.h>

static uint64_t s_rng = 0x9E3779B97F4A7C15ull;

void fake_crypto_seed(uint64_t seed)
{
    s_rng = seed ? seed : 1;
}

static void mix(const olc_part_t *parts, int count, const char *domain, uint8_t out[32])
{
    uint64_t h[4] = { 0xcbf29ce484222325ull, 0x84222325cbf29ce4ull, 0x100000001b3ull, 0x9E3779B97F4A7C15ull };
    for (int lane = 0; lane < 4; lane++) {
        for (const char *d = domain; *d; d++) {
            h[lane] = (h[lane] ^ (uint8_t)*d) * 0x100000001b3ull;
        }
        for (int i = 0; i < count; i++) {
            const uint8_t *p = parts[i].p;
            for (size_t j = 0; j < parts[i].n; j++) {
                h[lane] = (h[lane] ^ p[j]) * 0x100000001b3ull;
                h[lane] ^= h[lane] >> 29;
            }
            h[lane] = (h[lane] ^ parts[i].n) * 0x100000001b3ull;   /* length-framed */
        }
        h[lane] ^= h[lane] >> 31;
        h[lane] *= 0xff51afd7ed558ccdull + (uint64_t)lane * 2;
        h[lane] ^= h[lane] >> 33;
    }
    memcpy(out, h, 32);
}

int olc_random(void *out, size_t n)
{
    uint8_t *p = out;
    for (size_t i = 0; i < n; i++) {
        s_rng ^= s_rng << 13;
        s_rng ^= s_rng >> 7;
        s_rng ^= s_rng << 17;
        p[i] = (uint8_t)s_rng;
    }
    return 0;
}

int olc_sha256(const olc_part_t *parts, int count, uint8_t out[32])
{
    mix(parts, count, "sha", out);
    return 0;
}

int olc_hmac(const uint8_t *key, size_t key_len, const olc_part_t *parts, int count, uint8_t out[32])
{
    olc_part_t all[16];
    all[0] = (olc_part_t){ key, key_len };
    for (int i = 0; i < count && i < 15; i++) {
        all[i + 1] = parts[i];
    }
    mix(all, count + 1, "hmac", out);
    return 0;
}

int olc_hkdf(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, const uint8_t *info,
             size_t info_len, uint8_t *out, size_t out_len)
{
    for (size_t off = 0, ctr = 0; off < out_len; ctr++) {
        uint8_t block[32];
        const uint8_t c = (uint8_t)ctr;
        mix(OLC_PARTS({ salt, salt ? salt_len : 0 }, { ikm, ikm_len }, { info, info_len }, { &c, 1 }), "hkdf", block);
        const size_t n = out_len - off < 32 ? out_len - off : 32;
        memcpy(out + off, block, n);
        off += n;
    }
    return 0;
}

static uint32_t s_next_id = 1;

int olc_x25519_generate(olc_x25519_t *k)
{
    uint8_t priv[32];
    olc_random(priv, sizeof(priv));
    return olc_x25519_import(k, priv);
}

int olc_x25519_import(olc_x25519_t *k, const uint8_t priv[32])
{
    mix(OLC_PARTS({ priv, 32 }), "pub", k->pub);
    k->id = s_next_id++;
    return 0;
}

int olc_x25519_agree(const olc_x25519_t *k, const uint8_t peer_pub[32], uint8_t out[32])
{
    static const uint8_t zero[32];
    if (!k->id || memcmp(peer_pub, zero, 32) == 0) {
        return -1;
    }
    const bool mine_first = memcmp(k->pub, peer_pub, 32) < 0;   /* symmetric, like DH */
    mix(OLC_PARTS({ mine_first ? k->pub : peer_pub, 32 }, { mine_first ? peer_pub : k->pub, 32 }), "dh", out);
    return 0;
}

void olc_x25519_destroy(olc_x25519_t *k)
{
    k->id = 0;
}

void olc_wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) {
        *v++ = 0;
    }
}

bool olc_equal(const void *a, const void *b, size_t n)
{
    return memcmp(a, b, n) == 0;
}
