/*
 * odd_link_crypto.h on PSA Crypto. psa_crypto_init() is run by ESP-IDF at
 * start-up (components/mbedtls/port/esp_psa_crypto_init.c).
 */
#include "odd_link_crypto.h"

#include <string.h>
#include "psa/crypto.h"
#include "mbedtls/platform_util.h"

int olc_random(void *out, size_t n)
{
    return psa_generate_random(out, n) == PSA_SUCCESS ? 0 : -1;
}

int olc_sha256(const olc_part_t *parts, int count, uint8_t out[32])
{
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    size_t len = 0;
    psa_status_t st = psa_hash_setup(&op, PSA_ALG_SHA_256);
    for (int i = 0; st == PSA_SUCCESS && i < count; i++) {
        st = psa_hash_update(&op, parts[i].p, parts[i].n);
    }
    if (st == PSA_SUCCESS) {
        st = psa_hash_finish(&op, out, 32, &len);
    }
    if (st != PSA_SUCCESS) {
        psa_hash_abort(&op);
    }
    return st == PSA_SUCCESS && len == 32 ? 0 : -1;
}

int olc_hmac(const uint8_t *key, size_t key_len, const olc_part_t *parts, int count, uint8_t out[32])
{
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&a, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&a, (size_t)key_len * 8);
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&a, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_key_id_t id = 0;
    psa_status_t st = psa_import_key(&a, key, key_len, &id);
    psa_mac_operation_t op = PSA_MAC_OPERATION_INIT;
    size_t len = 0;
    if (st == PSA_SUCCESS) {
        st = psa_mac_sign_setup(&op, id, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    }
    for (int i = 0; st == PSA_SUCCESS && i < count; i++) {
        st = psa_mac_update(&op, parts[i].p, parts[i].n);
    }
    if (st == PSA_SUCCESS) {
        st = psa_mac_sign_finish(&op, out, 32, &len);
    } else {
        psa_mac_abort(&op);
    }
    if (id) {
        psa_destroy_key(id);
    }
    return st == PSA_SUCCESS && len == 32 ? 0 : -1;
}

int olc_hkdf(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
             const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len)
{
    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t st = psa_key_derivation_setup(&op, PSA_ALG_HKDF(PSA_ALG_SHA_256));
    if (st == PSA_SUCCESS && salt && salt_len) {
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT, salt, salt_len);
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SECRET, ikm, ikm_len);
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_INFO, info, info_len);
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_output_bytes(&op, out, out_len);
    }
    psa_key_derivation_abort(&op);
    return st == PSA_SUCCESS ? 0 : -1;
}

static void x25519_attrs(psa_key_attributes_t *a)
{
    psa_set_key_type(a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(a, 255);
    psa_set_key_usage_flags(a, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(a, PSA_ALG_ECDH);
}

static int x25519_finish(olc_x25519_t *k, psa_status_t st, psa_key_id_t id)
{
    size_t len = 0;
    if (st == PSA_SUCCESS) {
        st = psa_export_public_key(id, k->pub, sizeof(k->pub), &len);
    }
    if (st != PSA_SUCCESS || len != 32) {
        if (id) {
            psa_destroy_key(id);
        }
        k->id = 0;
        return -1;
    }
    k->id = id;
    return 0;
}

int olc_x25519_generate(olc_x25519_t *k)
{
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    x25519_attrs(&a);
    psa_key_id_t id = 0;
    return x25519_finish(k, psa_generate_key(&a, &id), id);
}

int olc_x25519_import(olc_x25519_t *k, const uint8_t priv[32])
{
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    x25519_attrs(&a);
    psa_key_id_t id = 0;
    return x25519_finish(k, psa_import_key(&a, priv, 32, &id), id);
}

int olc_x25519_agree(const olc_x25519_t *k, const uint8_t peer_pub[32], uint8_t out[32])
{
    size_t len = 0;
    if (!k->id || psa_raw_key_agreement(PSA_ALG_ECDH, k->id, peer_pub, 32, out, 32, &len) != PSA_SUCCESS ||
        len != 32) {
        return -1;
    }
    uint8_t acc = 0;
    for (int i = 0; i < 32; i++) {
        acc |= out[i];
    }
    if (acc == 0) {
        mbedtls_platform_zeroize(out, 32);
        return -1;                  /* low-order peer key: RFC 7748 §6.1 */
    }
    return 0;
}

void olc_x25519_destroy(olc_x25519_t *k)
{
    if (k->id) {
        psa_destroy_key(k->id);     /* PSA wipes the key material */
    }
    k->id = 0;
}

void olc_wipe(void *p, size_t n)
{
    mbedtls_platform_zeroize(p, n);
}

bool olc_equal(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) {
        d |= x[i] ^ y[i];
    }
    return d == 0;
}
