/*
 * Host tests: HELLO / link sessions and the device authorization store
 * (components/odd_link/odd_link_auth.c). Crypto = labelled TEST DOUBLE.
 */
#include <stdio.h>
#include <string.h>
#include "odd_link_auth.h"

void fake_crypto_seed(uint64_t seed);

static int s_pass, s_fail;
#define CHECK(c) do { if (c) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static const uint8_t MAC_A[6] = { 0x98, 0x88, 0xe0, 0xd4, 0xd0, 0x90 };   /* MAO */
static const uint8_t MAC_B[6] = { 0x02, 0xb0, 0xb0, 0xb0, 0xb0, 0xb0 };   /* another controller */
static const uint8_t MAC_D[6] = { 0x60, 0x55, 0xf9, 0x23, 0x53, 0x24 };
#define ID_A 0x0DD09888E0D4D090ull
#define ID_B 0x0DD002B0B0B0B0B0ull
#define ID_D 0x0DD06055F9235324ull

/* fake NVS for the two slots */
static odl_credential_t s_store[2];
static bool s_have[2], s_fail_store;
static int s_stores, s_erases;
static uint8_t s_lmk[16];
static uint8_t s_peer_mac[6];
static bool s_peer_on;
static uint8_t s_sent[ODL_MAX_FRAME];
static size_t s_sent_len;

static bool st(void *c, odl_slot_t sl, const odl_credential_t *cr)
{
    (void)c;
    if (s_fail_store) {
        return false;
    }
    s_store[sl] = *cr;
    s_have[sl] = true;
    s_stores++;
    return true;
}
static bool er(void *c, odl_slot_t sl) { (void)c; s_have[sl] = false; s_erases++; return true; }
static void pe(void *c, const uint8_t mac[6], const uint8_t *lmk)
{
    (void)c;
    memcpy(s_peer_mac, mac, 6);
    s_peer_on = lmk != NULL;
    if (lmk) {
        memcpy(s_lmk, lmk, 16);
    }
}
static void se(void *c, const uint8_t *f, size_t n) { (void)c; memcpy(s_sent, f, n); s_sent_len = n; }
static const odl_devauth_ops_t OPS = { st, er, pe, se, NULL };

static odl_credential_t cred(uint64_t peer, const uint8_t mac[6], uint8_t key_byte)
{
    odl_credential_t c = { .peer_id = peer };
    memcpy(c.peer_mac, mac, 6);
    memset(c.k_link, key_byte, sizeof(c.k_link));
    return c;
}

/* controller side: one HELLO -> device -> verify ACK. Returns the device's result. */
static odl_hello_result_t hello(odl_devauth_t *d, const odl_credential_t *ctrl_view, uint64_t ctrl_id,
                                const uint8_t ctrl_mac[6], const uint8_t radio_mac[6], bool *ack_ok,
                                odl_session_keys_t *keys, uint8_t frame_out[ODL_MAX_FRAME], size_t *frame_len)
{
    uint8_t nc[16], f[ODL_MAX_FRAME];
    const size_t n = odl_hello_build(ctrl_view, ctrl_id, ctrl_mac, nc, f);
    if (frame_out) {
        memcpy(frame_out, f, n);
        *frame_len = n;
    }
    odl_msg_t m;
    odl_decode(f, n, &m);
    s_sent_len = 0;
    const odl_hello_result_t r = odl_devauth_hello(d, &m, radio_mac);
    *ack_ok = false;
    if (s_sent_len) {
        odl_msg_t a;
        if (odl_decode(s_sent, s_sent_len, &a)) {
            *ack_ok = odl_hello_ack_verify(ctrl_view, ctrl_id, ctrl_mac, nc, &a, MAC_D, keys);
        }
    }
    return r;
}

int main(void)
{
    fake_crypto_seed(77);
    odl_devauth_t d;
    odl_session_keys_t kc;
    bool ok;
    const odl_credential_t a1_dev = cred(ID_A, MAC_A, 0x11), a1_mao = cred(ID_D, MAC_D, 0x11);
    const odl_credential_t a2_dev = cred(ID_A, MAC_A, 0x22), a2_mao = cred(ID_D, MAC_D, 0x22);
    const odl_credential_t b_dev = cred(ID_B, MAC_B, 0x33), b_ctrl = cred(ID_D, MAC_D, 0x33);

    /* factory device: nothing authorizes anything */
    odl_devauth_init(&d, &OPS, ID_D, MAC_D, NULL, NULL);
    CHECK(!odl_devauth_locked(&d));
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);

    /* first pairing: PENDING, then the first HELLO promotes it */
    CHECK(odl_devauth_set_pending(&d, &a1_dev) && odl_devauth_locked(&d) && !odl_devauth_has_session(&d));
    uint8_t first_hello[ODL_MAX_FRAME];
    size_t first_len;
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_A, &ok, &kc, first_hello, &first_len) == ODL_HELLO_PROMOTED && ok);
    CHECK(d.has_active && !d.has_pending && s_have[ODL_SLOT_ACTIVE] && !s_have[ODL_SLOT_PENDING]);
    CHECK(s_peer_on && memcmp(s_peer_mac, MAC_A, 6) == 0 && memcmp(s_lmk, kc.lmk, 16) == 0);   /* same LMK */
    /* both sessions carry traffic */
    odl_session_t c;
    odl_session_start(&c, &kc, 'C');
    uint8_t inner[20] = { 'O', 'D', 9 }, f[ODL_MAX_FRAME];
    const uint8_t *in;
    size_t il, n = odl_wrap(&c, inner, sizeof(inner), f);
    CHECK(odl_devauth_unwrap(&d, MAC_A, f, n, &in, &il) == ODL_RX_OK);
    n = odl_wrap(&c, inner, sizeof(inner), f);
    CHECK(odl_devauth_unwrap(&d, MAC_B, f, n, &in, &il) == ODL_RX_WRONG_SESSION);   /* other radio */
    n = odl_devauth_wrap(&d, inner, sizeof(inner), f);
    CHECK(n && odl_unwrap(&c, f, n, &in, &il) == ODL_RX_OK);
    CHECK(odl_devauth_is_session_peer(&d, MAC_A) && !odl_devauth_is_session_peer(&d, MAC_B));

    /* the same HELLO again (lost ACK): identical ACK, same session */
    uint8_t sid[8];
    memcpy(sid, d.sess.sid, 8);
    odl_msg_t hm;
    odl_decode(first_hello, first_len, &hm);
    s_sent_len = 0;
    CHECK(odl_devauth_hello(&d, &hm, MAC_A) == ODL_HELLO_REPEATED && s_sent_len > 0 && memcmp(sid, d.sess.sid, 8) == 0);
    /* a new session: fresh keys, fresh LMK */
    uint8_t lmk1[16];
    memcpy(lmk1, s_lmk, 16);
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_NEW_SESSION && ok);
    CHECK(memcmp(lmk1, s_lmk, 16) != 0 && memcmp(sid, d.sess.sid, 8) != 0);
    /* now the first HELLO is a replay */
    CHECK(odl_devauth_hello(&d, &hm, MAC_A) == ODL_HELLO_REPLAYED);
    /* a valid HELLO sent from another radio (spoofed / relayed) */
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_B, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);
    /* the right radio and ids, the wrong key */
    const odl_credential_t wrong = cred(ID_D, MAC_D, 0x99);
    CHECK(hello(&d, &wrong, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);
    /* a copied device_id claimed by another controller */
    CHECK(hello(&d, &a1_mao, ID_B, MAC_B, MAC_B, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);

    /* re-pair, same controller: K2 pending; the HELLO under K2 promotes it; K1 is dead */
    CHECK(odl_devauth_set_pending(&d, &a2_dev));
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_NEW_SESSION && ok);
    CHECK(!d.has_pending);                                   /* MAO kept K1: K2 discarded */
    CHECK(odl_devauth_set_pending(&d, &a2_dev));
    CHECK(hello(&d, &a2_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_PROMOTED && ok);
    CHECK(memcmp(d.active.k_link, a2_dev.k_link, 32) == 0);
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);   /* old key */

    /* replacement by controller B: A keeps working until B's first HELLO */
    CHECK(odl_devauth_set_pending(&d, &b_dev));
    CHECK(hello(&d, &a2_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_NEW_SESSION && ok);
    CHECK(d.has_pending);                                    /* A cannot cancel B's replacement */
    CHECK(hello(&d, &b_ctrl, ID_B, MAC_B, MAC_B, &ok, &kc, NULL, NULL) == ODL_HELLO_PROMOTED && ok);
    CHECK(d.active.peer_id == ID_B && s_peer_on && memcmp(s_peer_mac, MAC_B, 6) == 0);
    CHECK(hello(&d, &a2_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);   /* A replaced */

    /* storage failure while promoting: nothing moves, the next HELLO retries */
    odl_devauth_init(&d, &OPS, ID_D, MAC_D, NULL, &a1_dev);
    s_fail_store = true;
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);
    CHECK(d.has_pending && !d.has_active && !odl_devauth_has_session(&d));
    s_fail_store = false;
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_PROMOTED && ok);

    /* revocation: every slot, the session and the peer entry */
    CHECK(odl_devauth_revoke(&d) && !odl_devauth_locked(&d) && !odl_devauth_has_session(&d) && !s_peer_on);
    CHECK(!s_have[ODL_SLOT_ACTIVE] && !s_have[ODL_SLOT_PENDING]);
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);
    n = odl_wrap(&c, inner, sizeof(inner), f);
    CHECK(odl_devauth_unwrap(&d, MAC_A, f, n, &in, &il) == ODL_RX_NO_SESSION);

    /* reboot: credentials reload, but no session until a new HELLO */
    odl_devauth_init(&d, &OPS, ID_D, MAC_D, &a1_dev, NULL);
    CHECK(odl_devauth_locked(&d) && !odl_devauth_has_session(&d));
    n = odl_wrap(&c, inner, sizeof(inner), f);
    CHECK(odl_devauth_unwrap(&d, MAC_A, f, n, &in, &il) == ODL_RX_NO_SESSION);   /* the old session is dead */
    CHECK(hello(&d, &a1_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_NEW_SESSION && ok);

    /* ---- M3.2: one controller, two paired devices (D and E), nothing shared ---- */
    static const uint8_t MAC_E[6] = { 0xa0, 0x76, 0x4e, 0x1d, 0x86, 0xd4 };
    const uint64_t ID_E = 0x0DD0A0764E1D86D4ull;
    odl_devauth_t e;
    const odl_credential_t d_dev = cred(ID_A, MAC_A, 0x11), d_mao = cred(ID_D, MAC_D, 0x11);
    const odl_credential_t e_dev = cred(ID_A, MAC_A, 0x55), e_mao = cred(ID_E, MAC_E, 0x55);
    odl_devauth_init(&d, &OPS, ID_D, MAC_D, &d_dev, NULL);
    odl_devauth_init(&e, &OPS, ID_E, MAC_E, &e_dev, NULL);
    odl_session_keys_t kd, ke;
    CHECK(hello(&d, &d_mao, ID_A, MAC_A, MAC_A, &ok, &kd, NULL, NULL) == ODL_HELLO_NEW_SESSION && ok);
    uint8_t lmk_d[16];
    memcpy(lmk_d, s_lmk, 16);
    /* E's ACK is verified against E's radio */
    {
        uint8_t nc[16], hf[ODL_MAX_FRAME];
        const size_t hn = odl_hello_build(&e_mao, ID_A, MAC_A, nc, hf);
        odl_msg_t m, a;
        odl_decode(hf, hn, &m);
        s_sent_len = 0;
        CHECK(odl_devauth_hello(&e, &m, MAC_A) == ODL_HELLO_NEW_SESSION && s_sent_len > 0);
        CHECK(odl_decode(s_sent, s_sent_len, &a));
        CHECK(odl_hello_ack_verify(&e_mao, ID_A, MAC_A, nc, &a, MAC_E, &ke));
        CHECK(!odl_hello_ack_verify(&e_mao, ID_A, MAC_A, nc, &a, MAC_D, &kc));   /* from the other radio */
        CHECK(!odl_hello_ack_verify(&d_mao, ID_A, MAC_A, nc, &a, MAC_E, &kc));   /* under D's credential */
    }
    CHECK(memcmp(lmk_d, s_lmk, 16) != 0 && memcmp(kd.lmk, ke.lmk, 16) != 0);   /* no shared LMK */
    CHECK(memcmp(kd.sid, ke.sid, 8) != 0 && memcmp(kd.k_c2d, ke.k_c2d, 32) != 0 && memcmp(kd.k_d2c, ke.k_d2c, 32) != 0);
    CHECK(memcmp(d.sess.sid, kd.sid, 8) == 0 && memcmp(e.sess.sid, ke.sid, 8) == 0);
    /* wrong-peer credential: MAO's HELLO for D presented to E (and vice versa) */
    CHECK(hello(&e, &d_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_IGNORED && !ok);   /* not E's id */
    CHECK(hello(&d, &e_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_IGNORED && !ok);
    const odl_credential_t e_with_d_key = cred(ID_E, MAC_E, 0x11), d_with_e_key = cred(ID_D, MAC_D, 0x55);
    CHECK(hello(&e, &e_with_d_key, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);
    CHECK(hello(&d, &d_with_e_key, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_REFUSED && !ok);
    CHECK(memcmp(d.sess.sid, kd.sid, 8) == 0 && memcmp(e.sess.sid, ke.sid, 8) == 0);   /* both sessions intact */
    /* cross-peer envelopes fail; each window is its own */
    odl_session_t cd, ce;
    odl_session_start(&cd, &kd, 'C');
    odl_session_start(&ce, &ke, 'C');
    uint8_t fd[ODL_MAX_FRAME], fe[ODL_MAX_FRAME];
    size_t nd = odl_wrap(&cd, inner, sizeof(inner), fd), ne = odl_wrap(&ce, inner, sizeof(inner), fe);
    CHECK(cd.tx_ctr == ce.tx_ctr);                           /* same counter value, different sessions */
    CHECK(odl_devauth_unwrap(&e, MAC_A, fd, nd, &in, &il) == ODL_RX_WRONG_SESSION);
    CHECK(odl_devauth_unwrap(&d, MAC_A, fe, ne, &in, &il) == ODL_RX_WRONG_SESSION);
    CHECK(odl_devauth_unwrap(&d, MAC_A, fd, nd, &in, &il) == ODL_RX_OK);
    CHECK(odl_devauth_unwrap(&e, MAC_A, fe, ne, &in, &il) == ODL_RX_OK);   /* D's accept never burnt E's counter */
    CHECK(odl_devauth_unwrap(&d, MAC_A, fd, nd, &in, &il) == ODL_RX_REPLAY);
    CHECK(odl_devauth_unwrap(&e, MAC_A, fe, ne, &in, &il) == ODL_RX_REPLAY);
    /* the same session id forced onto E's frame: E's key still refuses D's frame */
    memcpy(fe, fd, nd);
    memcpy(&fe[4], e.sess.sid, 8);
    CHECK(odl_devauth_unwrap(&e, MAC_A, fe, nd, &in, &il) == ODL_RX_BAD_TAG);
    /* D advances far; E's window does not move */
    for (int i = 0; i < 50; i++) {
        nd = odl_wrap(&cd, inner, sizeof(inner), fd);
        odl_devauth_unwrap(&d, MAC_A, fd, nd, &in, &il);
    }
    CHECK(d.sess.rx_hi == 51 && e.sess.rx_hi == 1 && ce.tx_ctr == 1);
    ne = odl_wrap(&ce, inner, sizeof(inner), fe);
    CHECK(odl_devauth_unwrap(&e, MAC_A, fe, ne, &in, &il) == ODL_RX_OK);
    /* device -> controller: D's reply never verifies in E's controller session */
    size_t rd = odl_devauth_wrap(&d, inner, sizeof(inner), fd);
    CHECK(rd && odl_unwrap(&ce, fd, rd, &in, &il) == ODL_RX_WRONG_SESSION && odl_unwrap(&cd, fd, rd, &in, &il) == ODL_RX_OK);
    /* one-peer teardown: D revoked; E keeps its credential and session */
    CHECK(odl_devauth_revoke(&d) && !odl_devauth_has_session(&d));
    CHECK(odl_devauth_locked(&e) && odl_devauth_has_session(&e) && memcmp(e.sess.sid, ke.sid, 8) == 0);
    ne = odl_wrap(&ce, inner, sizeof(inner), fe);
    CHECK(odl_devauth_unwrap(&e, MAC_A, fe, ne, &in, &il) == ODL_RX_OK);
    nd = odl_wrap(&cd, inner, sizeof(inner), fd);
    CHECK(odl_devauth_unwrap(&d, MAC_A, fd, nd, &in, &il) == ODL_RX_NO_SESSION);
    /* E re-keys: D (now factory) is unaffected, E's old session is dead */
    uint8_t old_e[ODL_MAX_FRAME];
    const size_t old_en = odl_wrap(&ce, inner, sizeof(inner), old_e);
    CHECK(hello(&e, &e_mao, ID_A, MAC_A, MAC_A, &ok, &kc, NULL, NULL) == ODL_HELLO_NEW_SESSION);
    CHECK(odl_devauth_has_session(&e) && memcmp(e.sess.sid, ke.sid, 8) != 0);
    CHECK(odl_devauth_unwrap(&e, MAC_A, old_e, old_en, &in, &il) == ODL_RX_WRONG_SESSION);
    CHECK(!odl_devauth_locked(&d));

    printf("link security (sessions / authorization): %d checks passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
