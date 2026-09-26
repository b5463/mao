/*
 * On-target known-answer self-test of the link crypto: the PSA primitives
 * against published vectors (FIPS 180, RFC 4231, RFC 5869, RFC 7748) and
 * the whole ODD key schedule against an independent implementation
 * (odd_link_vectors.h, generated with pyca/cryptography by
 * tools/odd_link_vectors.py). Only test data is involved; it logs check
 * names, never live key material.
 */
#include "odd_link_selftest.h"
#include "odd_link.h"
#include "odd_link_crypto.h"
#include "odd_link_vectors.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ODD_LINK";

static int s_pass, s_total;

static void check(const char *name, bool ok)
{
    s_total++;
    if (ok) {
        s_pass++;
    } else {
        ESP_LOGE(TAG, "selftest FAIL: %s", name);
    }
}

#define EQ(name, got, exp) check(name, memcmp((got), (exp), sizeof(exp)) == 0)

static void standard_vectors(void)
{
    uint8_t h[32], okm[42];
    check("sha256 run", olc_sha256(OLC_PARTS({ "abc", 3 }), h) == 0);
    EQ("sha256 FIPS abc", h, kV_std_sha256_abc);
    static const uint8_t k1[20] = { 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b };
    check("hmac run 1", olc_hmac(k1, sizeof(k1), OLC_PARTS({ "Hi There", 8 }), h) == 0);
    EQ("hmac RFC4231 #1", h, kV_std_hmac_1);
    check("hmac run 2", olc_hmac((const uint8_t *)"Jefe", 4,
                                 OLC_PARTS({ "what do ya ", 11 }, { "want for nothing?", 17 }), h) == 0);
    EQ("hmac RFC4231 #2 (multi-part)", h, kV_std_hmac_2);
    uint8_t ikm[22], salt[13], info[10];
    memset(ikm, 0x0b, sizeof(ikm));
    for (int i = 0; i < 13; i++) {
        salt[i] = (uint8_t)i;
    }
    for (int i = 0; i < 10; i++) {
        info[i] = (uint8_t)(0xf0 + i);
    }
    check("hkdf run", olc_hkdf(salt, sizeof(salt), ikm, sizeof(ikm), info, sizeof(info), okm, sizeof(okm)) == 0);
    EQ("hkdf RFC5869 #1", okm, kV_std_hkdf_1);
}

static void x25519_vectors(void)
{
    olc_x25519_t a = { 0 }, b = { 0 };
    uint8_t za[32], zb[32];
    check("x25519 import a", olc_x25519_import(&a, kV_x_a_priv) == 0);
    check("x25519 import b", olc_x25519_import(&b, kV_x_b_priv) == 0);
    EQ("x25519 RFC7748 pub a", a.pub, kV_x_a_pub);
    EQ("x25519 RFC7748 pub b", b.pub, kV_x_b_pub);
    check("x25519 agree a", olc_x25519_agree(&a, b.pub, za) == 0);
    check("x25519 agree b", olc_x25519_agree(&b, a.pub, zb) == 0);
    EQ("x25519 RFC7748 shared (a)", za, kV_x_shared);
    EQ("x25519 RFC7748 shared (b)", zb, kV_x_shared);
    static const uint8_t zero_point[32] = { 0 };   /* low order: must be refused */
    check("x25519 rejects low-order peer", olc_x25519_agree(&a, zero_point, za) != 0);
    olc_x25519_destroy(&a);
    olc_x25519_destroy(&b);
    olc_x25519_t e1 = { 0 }, e2 = { 0 };
    check("x25519 generate", olc_x25519_generate(&e1) == 0 && olc_x25519_generate(&e2) == 0);
    check("x25519 fresh keys differ", memcmp(e1.pub, e2.pub, 32) != 0);
    olc_x25519_destroy(&e1);
    olc_x25519_destroy(&e2);
}

static void schedule_vectors(void)
{
    odl_transcript_t t = { .ctrl_id = kV_ctrl_id, .dev_id = kV_dev_id };
    memcpy(t.mac_c, kV_mac_c, 6);
    memcpy(t.mac_d, kV_mac_d, 6);
    memcpy(t.txid, kV_txid, 8);
    memcpy(t.pub_c, kV_x_a_pub, 32);
    memcpy(t.pub_d, kV_x_b_pub, 32);
    memcpy(t.nonce_c, kV_nonce_c, 16);
    memcpy(t.nonce_d, kV_nonce_d, 16);
    uint8_t v[32], th[32];
    check("commit run", odl_commit(&t, v) == 0);
    EQ("commit", v, kV_commit);
    check("transcript run", odl_transcript_hash(&t, th) == 0);
    EQ("transcript hash", th, kV_th);
    odl_pair_keys_t k;
    check("pair keys run", odl_pair_keys(kV_x_shared, th, &k) == 0);
    EQ("K_confirm", k.k_confirm, kV_k_confirm);
    EQ("K_link", k.k_link, kV_k_link);
    check("SAS", k.sas == kV_sas);
    check("fingerprint", odl_fingerprint(k.k_link) == kV_fp);
    odl_confirm_tag(k.k_confirm, 'C', th, v);
    EQ("confirm tag C", v, kV_tag_c);
    odl_confirm_tag(k.k_confirm, 'D', th, v);
    EQ("confirm tag D", v, kV_tag_d);
    /* one changed transcript byte: different SAS / tags (MITM detection) */
    odl_transcript_t t2 = t;
    t2.nonce_c[0] ^= 1;
    uint8_t th2[32];
    odl_pair_keys_t k2;
    odl_transcript_hash(&t2, th2);
    odl_pair_keys(kV_x_shared, th2, &k2);
    check("modified transcript changes K_link", memcmp(k2.k_link, k.k_link, 32) != 0);
    odl_confirm_tag(k2.k_confirm, 'C', th2, v);
    check("modified transcript changes confirm tag", memcmp(v, kV_tag_c, 32) != 0);
    uint8_t kh[32], tag[16];
    odl_hello_key(k.k_link, kV_ctrl_id, kV_dev_id, kh);
    EQ("K_hello", kh, kV_k_hello);
    odl_hello_tag(kh, kV_ctrl_id, kV_dev_id, kV_mac_c, kV_mac_d, kV_nonce_c, NULL, tag);
    EQ("HELLO tag", tag, kV_hello_tag);
    odl_hello_tag(kh, kV_ctrl_id, kV_dev_id, kV_mac_c, kV_mac_d, kV_nonce_c, kV_nonce_d, tag);
    EQ("HELLO_ACK tag", tag, kV_hello_ack_tag);
    odl_session_keys_t sk;
    check("session run", odl_session_keys(k.k_link, kV_ctrl_id, kV_dev_id, kV_mac_c, kV_mac_d, kV_nonce_c,
                                          kV_nonce_d, &sk) == 0);
    EQ("session LMK", sk.lmk, kV_lmk);
    EQ("session K_c2d", sk.k_c2d, kV_k_c2d);
    EQ("session K_d2c", sk.k_d2c, kV_k_d2c);
    EQ("session id", sk.sid, kV_sid);

    /* envelope: exact bytes, then the receiver's checks */
    odl_session_t c, d;
    odl_session_start(&c, &sk, 'C');
    odl_session_start(&d, &sk, 'D');
    uint8_t f[ODL_MAX_FRAME];
    const size_t n = odl_wrap(&c, kV_env_inner, sizeof(kV_env_inner), f);
    check("envelope length", n == sizeof(kV_env_frame));
    check("envelope bytes", n == sizeof(kV_env_frame) && memcmp(f, kV_env_frame, n) == 0);
    const uint8_t *in;
    size_t il;
    check("unwrap ok", odl_unwrap(&d, f, n, &in, &il) == ODL_RX_OK && il == sizeof(kV_env_inner) &&
                           memcmp(in, kV_env_inner, il) == 0);
    check("replay refused", odl_unwrap(&d, f, n, &in, &il) == ODL_RX_REPLAY);
    check("reflection refused (own direction)", odl_unwrap(&c, f, n, &in, &il) == ODL_RX_BAD_TAG);
    f[ODL_ENV_HDR_LEN] ^= 0x01;
    check("tampered inner refused", odl_unwrap(&d, f, n, &in, &il) == ODL_RX_BAD_TAG);
    f[ODL_ENV_HDR_LEN] ^= 0x01;
    f[4] ^= 0x01;
    check("other session refused", odl_unwrap(&d, f, n, &in, &il) == ODL_RX_WRONG_SESSION);
    odl_session_end(&c);
    odl_session_end(&d);
    olc_wipe(&k, sizeof(k));
    olc_wipe(&sk, sizeof(sk));
}

int odl_selftest(int *total)
{
    s_pass = s_total = 0;
    standard_vectors();
    x25519_vectors();
    schedule_vectors();
    if (total) {
        *total = s_total;
    }
    ESP_LOGI(TAG, "link crypto selftest: %d/%d passed", s_pass, s_total);
    return s_pass;
}

void odl_bench(void)
{
    olc_x25519_t a = { 0 }, b = { 0 };
    uint8_t z[32], okm[88], tag[32];
    int64_t t0 = esp_timer_get_time();
    olc_x25519_generate(&a);
    const int64_t t_gen = esp_timer_get_time() - t0;
    olc_x25519_generate(&b);
    t0 = esp_timer_get_time();
    olc_x25519_agree(&a, b.pub, z);
    const int64_t t_agree = esp_timer_get_time() - t0;
    t0 = esp_timer_get_time();
    olc_hkdf(z, 32, z, 32, (const uint8_t *)"bench", 5, okm, sizeof(okm));
    const int64_t t_hkdf = esp_timer_get_time() - t0;
    uint8_t frame[64] = { 0 };
    t0 = esp_timer_get_time();
    for (int i = 0; i < 20; i++) {
        olc_hmac(z, 32, OLC_PARTS({ frame, sizeof(frame) }), tag);
    }
    const int64_t t_hmac = (esp_timer_get_time() - t0) / 20;
    olc_x25519_destroy(&a);
    olc_x25519_destroy(&b);
    olc_wipe(z, sizeof(z));
    olc_wipe(okm, sizeof(okm));
    ESP_LOGI(TAG, "bench: x25519 generate %lld us, agree %lld us, hkdf(88 B) %lld us, hmac(64 B) %lld us",
             (long long)t_gen, (long long)t_agree, (long long)t_hkdf, (long long)t_hmac);
}
