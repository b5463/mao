/*
 * Key schedule (docs/link_security.md). Every value is SHA-256, HMAC-SHA-256
 * or HKDF-SHA-256 over fixed, domain-separated labels; ids are 8-byte
 * big-endian.
 */
#include "odd_link.h"
#include "odd_link_crypto.h"

#include <stdio.h>
#include <string.h>

static void be64(uint64_t v, uint8_t out[8])
{
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)(v >> (56 - 8 * i));
    }
}

#define LBL(s) { (s), sizeof(s) - 1 }

int odl_commit(const odl_transcript_t *t, uint8_t out[ODL_HASH_LEN])
{
    uint8_t ic[8], id[8];
    be64(t->ctrl_id, ic);
    be64(t->dev_id, id);
    return olc_sha256(OLC_PARTS(LBL("ODD-PAIR-COMMIT-v1"), { t->txid, ODL_TXID_LEN }, { ic, 8 }, { id, 8 },
                                { t->pub_c, ODL_PUB_LEN }, { t->pub_d, ODL_PUB_LEN }, { t->nonce_d, ODL_NONCE_LEN }),
                      out);
}

int odl_transcript_hash(const odl_transcript_t *t, uint8_t out[ODL_HASH_LEN])
{
    const uint8_t ver = ODL_VERSION;
    uint8_t ic[8], id[8];
    be64(t->ctrl_id, ic);
    be64(t->dev_id, id);
    return olc_sha256(OLC_PARTS(LBL("ODD-PAIR-TRANSCRIPT-v1"), { &ver, 1 }, { ic, 8 }, { id, 8 }, { t->mac_c, 6 },
                                { t->mac_d, 6 }, { t->txid, ODL_TXID_LEN }, { t->pub_c, ODL_PUB_LEN },
                                { t->pub_d, ODL_PUB_LEN }, { t->nonce_c, ODL_NONCE_LEN },
                                { t->nonce_d, ODL_NONCE_LEN }),
                      out);
}

int odl_pair_keys(const uint8_t z[32], const uint8_t th[ODL_HASH_LEN], odl_pair_keys_t *out)
{
    static const char kConfirm[] = "ODD-PAIR-CONFIRM-v1";
    static const char kLink[] = "ODD-LINK-KEY-v1";
    static const char kSas[] = "ODD-PAIR-SAS-v1";
    uint8_t sas[8];
    int e = olc_hkdf(th, ODL_HASH_LEN, z, 32, (const uint8_t *)kConfirm, sizeof(kConfirm) - 1, out->k_confirm,
                     ODL_KEY_LEN);
    e |= olc_hkdf(th, ODL_HASH_LEN, z, 32, (const uint8_t *)kLink, sizeof(kLink) - 1, out->k_link, ODL_KEY_LEN);
    e |= olc_hkdf(th, ODL_HASH_LEN, z, 32, (const uint8_t *)kSas, sizeof(kSas) - 1, sas, sizeof(sas));
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (v << 8) | sas[i];
    }
    out->sas = (uint32_t)(v % 1000000u);
    olc_wipe(sas, sizeof(sas));
    return e ? -1 : 0;
}

int odl_confirm_tag(const uint8_t k_confirm[ODL_KEY_LEN], char role, const uint8_t th[ODL_HASH_LEN],
                    uint8_t out[ODL_HASH_LEN])
{
    const char *label = role == 'C' ? "ODD-PAIR-CONFIRM-C" : "ODD-PAIR-CONFIRM-D";
    return olc_hmac(k_confirm, ODL_KEY_LEN, OLC_PARTS({ label, 18 }, { th, ODL_HASH_LEN }), out);
}

int odl_hello_key(const uint8_t k_link[ODL_KEY_LEN], uint64_t ctrl_id, uint64_t dev_id, uint8_t out[ODL_KEY_LEN])
{
    uint8_t info[17 + 16];
    memcpy(info, "ODD-LINK-HELLO-v1", 17);
    be64(ctrl_id, &info[17]);
    be64(dev_id, &info[25]);
    return olc_hkdf(NULL, 0, k_link, ODL_KEY_LEN, info, sizeof(info), out, ODL_KEY_LEN);
}

int odl_hello_tag(const uint8_t k_hello[ODL_KEY_LEN], uint64_t ctrl_id, uint64_t dev_id, const uint8_t mac_c[6],
                  const uint8_t mac_d[6], const uint8_t nonce_c[ODL_NONCE_LEN], const uint8_t *nonce_d,
                  uint8_t out[ODL_TAG_LEN])
{
    uint8_t ic[8], id[8], full[32];
    be64(ctrl_id, ic);
    be64(dev_id, id);
    const int e = olc_hmac(k_hello, ODL_KEY_LEN,
                           OLC_PARTS({ nonce_d ? "H2" : "H1", 2 }, { ic, 8 }, { id, 8 }, { mac_c, 6 }, { mac_d, 6 },
                                     { nonce_c, ODL_NONCE_LEN }, { nonce_d, nonce_d ? ODL_NONCE_LEN : 0 }),
                           full);
    memcpy(out, full, ODL_TAG_LEN);
    return e;
}

int odl_session_keys(const uint8_t k_link[ODL_KEY_LEN], uint64_t ctrl_id, uint64_t dev_id, const uint8_t mac_c[6],
                     const uint8_t mac_d[6], const uint8_t nonce_c[ODL_NONCE_LEN],
                     const uint8_t nonce_d[ODL_NONCE_LEN], odl_session_keys_t *out)
{
    uint8_t salt[2 * ODL_NONCE_LEN];
    memcpy(salt, nonce_c, ODL_NONCE_LEN);
    memcpy(&salt[ODL_NONCE_LEN], nonce_d, ODL_NONCE_LEN);
    uint8_t info[19 + 16 + 12];
    memcpy(info, "ODD-LINK-SESSION-v1", 19);
    be64(ctrl_id, &info[19]);
    be64(dev_id, &info[27]);
    memcpy(&info[35], mac_c, 6);
    memcpy(&info[41], mac_d, 6);
    uint8_t okm[ODL_LMK_LEN + 2 * ODL_KEY_LEN + ODL_SID_LEN];
    const int e = olc_hkdf(salt, sizeof(salt), k_link, ODL_KEY_LEN, info, sizeof(info), okm, sizeof(okm));
    memcpy(out->lmk, okm, ODL_LMK_LEN);
    memcpy(out->k_c2d, &okm[ODL_LMK_LEN], ODL_KEY_LEN);
    memcpy(out->k_d2c, &okm[ODL_LMK_LEN + ODL_KEY_LEN], ODL_KEY_LEN);
    memcpy(out->sid, &okm[ODL_LMK_LEN + 2 * ODL_KEY_LEN], ODL_SID_LEN);
    olc_wipe(okm, sizeof(okm));
    return e;
}

uint32_t odl_fingerprint(const uint8_t key[ODL_KEY_LEN])
{
    uint8_t h[32];
    if (olc_sha256(OLC_PARTS(LBL("ODD-KEY-FP-v1"), { key, ODL_KEY_LEN }), h) != 0) {
        return 0;
    }
    return ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
}

void odl_sas_text(uint32_t sas, char out[8])
{
    snprintf(out, 8, "%03u %03u", (unsigned)(sas / 1000u) % 1000u, (unsigned)(sas % 1000u));
}
