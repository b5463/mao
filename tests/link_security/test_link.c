/*
 * Host tests for the ODD link protocol logic (components/odd_link): codec,
 * DATA envelope + replay window, and the pairing ceremony state machines,
 * wired through an in-memory "air" that can drop, record and replay frames.
 *
 * Crypto is the deterministic TEST DOUBLE (fake_crypto.c): these tests prove
 * protocol behaviour, not primitive correctness (that is odl_selftest on the
 * target, against published vectors and an independent implementation).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "odd_link.h"
#include "odd_link_pair.h"

void fake_crypto_seed(uint64_t seed);

static int s_pass, s_fail;
#define CHECK(c) do { if (c) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---------------------------------------------------------------------- */
/* The air                                                                */
/* ---------------------------------------------------------------------- */

typedef struct { uint8_t f[ODL_MAX_FRAME]; size_t n; char from; } frame_t;

static const uint8_t MAC_C[6] = { 0x98, 0x88, 0xe0, 0xd4, 0xd0, 0x90 };
static const uint8_t MAC_D[6] = { 0x60, 0x55, 0xf9, 0x23, 0x53, 0x24 };
static const uint8_t MAC_X[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
#define CTRL_ID 0x0DD09888E0D4D090ull
#define DEV_ID  0x0DD06055F9235324ull

static frame_t s_q[64];
static int s_qn;
static frame_t s_rec[128];          /* everything ever sent (for replay tests) */
static int s_recn;
static int s_drop_type = -1, s_drop_count;   /* drop the next N frames of a type */

static void air_send(char from, const uint8_t *f, size_t n)
{
    if (s_recn < 128) {
        memcpy(s_rec[s_recn].f, f, n);
        s_rec[s_recn].n = n;
        s_rec[s_recn++].from = from;
    }
    if (s_drop_count > 0 && n >= 4 && f[3] == s_drop_type) {
        s_drop_count--;
        return;
    }
    if (s_qn < 64) {
        memcpy(s_q[s_qn].f, f, n);
        s_q[s_qn].n = n;
        s_q[s_qn++].from = from;
    }
}

typedef struct {
    int persists;
    odl_credential_t cred;
    bool fail_persist;
    int changes;
} side_t;

static side_t SC, SD;

static void c_send(void *ctx, const uint8_t *f, size_t n) { (void)ctx; air_send('C', f, n); }
static void d_send(void *ctx, const uint8_t *f, size_t n) { (void)ctx; air_send('D', f, n); }
static bool persist(void *ctx, const odl_credential_t *c)
{
    side_t *s = ctx;
    if (s->fail_persist) {
        return false;
    }
    s->persists++;
    s->cred = *c;
    return true;
}
static void changed(void *ctx) { ((side_t *)ctx)->changes++; }

static odl_pairc_t C;
static odl_paird_t D;
static uint32_t T;

static void rig(bool pair_mode)
{
    memset(&SC, 0, sizeof(SC));
    memset(&SD, 0, sizeof(SD));
    s_qn = s_recn = 0;
    s_drop_count = 0;
    T = 1000;
    const odl_pair_ops_t oc = { c_send, persist, changed, &SC }, od = { d_send, persist, changed, &SD };
    odl_pairc_init(&C, &oc);
    odl_paird_init(&D, &od, DEV_ID, MAC_D);
    if (pair_mode) {
        odl_paird_mode(&D, ODL_PAIR_MODE_MS, T);
    }
}

/* Deliver queued frames (and keep time moving) until quiet. */
static void pump(int rounds)
{
    for (int r = 0; r < rounds; r++) {
        while (s_qn > 0) {
            frame_t fr = s_q[0];
            memmove(s_q, s_q + 1, (size_t)(--s_qn) * sizeof(frame_t));
            odl_msg_t m;
            if (!odl_decode(fr.f, fr.n, &m)) {
                continue;
            }
            if (fr.from == 'C') {
                odl_paird_rx(&D, &m, MAC_C, T);
            } else {
                odl_pairc_rx(&C, &m, MAC_D, T);
            }
        }
        T += 100;
        odl_pairc_tick(&C, T);
        odl_paird_tick(&D, T);
    }
}

/* Deliver exactly n queued frames, in order. */
static void deliver(int n)
{
    for (int i = 0; i < n && s_qn > 0; i++) {
        frame_t fr = s_q[0];
        memmove(s_q, s_q + 1, (size_t)(--s_qn) * sizeof(frame_t));
        odl_msg_t m;
        if (odl_decode(fr.f, fr.n, &m)) {
            if (fr.from == 'C') {
                odl_paird_rx(&D, &m, MAC_C, T);
            } else {
                odl_pairc_rx(&C, &m, MAC_D, T);
            }
        }
    }
}

static void inject(const frame_t *fr, const uint8_t mac[6], char to)
{
    odl_msg_t m;
    if (odl_decode(fr->f, fr->n, &m)) {
        if (to == 'D') {
            odl_paird_rx(&D, &m, mac, T);
        } else {
            odl_pairc_rx(&C, &m, mac, T);
        }
    }
}

static uint32_t sas_c(void) { return C.keys.sas; }
static uint32_t sas_d(void) { return D.keys.sas; }

/* ---------------------------------------------------------------------- */

static void test_codec(void)
{
    const uint8_t types[] = { ODL_PAIR_START, ODL_PAIR_COMMIT, ODL_PAIR_NONCE, ODL_PAIR_REVEAL,
                              ODL_PAIR_CONFIRM, ODL_PAIR_ACCEPT, ODL_PAIR_ABORT, ODL_HELLO, ODL_HELLO_ACK };
    for (size_t i = 0; i < sizeof(types); i++) {
        odl_msg_t m = { .type = types[i], .ctrl_id = CTRL_ID, .dev_id = DEV_ID, .reason = 3 };
        for (int j = 0; j < 32; j++) {
            m.pub[j] = (uint8_t)j;
            m.commit[j] = (uint8_t)(j * 3);
            m.tag[j] = (uint8_t)(j * 5);
        }
        memset(m.txid, 0x42, 8);
        memset(m.nonce, 0x17, 16);
        memset(m.nonce2, 0x71, 16);
        if (types[i] == ODL_HELLO || types[i] == ODL_HELLO_ACK) {
            memset(m.tag + 16, 0, 16);
            memset(m.txid, 0, 8);
        }
        uint8_t f[ODL_MAX_FRAME];
        const size_t n = odl_encode(&m, f);
        odl_msg_t back;
        CHECK(n > 0 && odl_decode(f, n, &back));
        CHECK(back.type == m.type && back.ctrl_id == CTRL_ID && back.dev_id == DEV_ID);
        CHECK(!odl_decode(f, n - 1, &back));                 /* truncated */
        f[n] = 0;
        CHECK(!odl_decode(f, n + 1, &back));                 /* oversized */
        f[2] = 2;
        CHECK(!odl_decode(f, n, &back));                     /* wrong version */
        f[2] = ODL_VERSION;
        f[0] = 'O';
        CHECK(!odl_decode(f, n, &back));                     /* ODD namespace, not link */
    }
    uint8_t f[8] = { 'L', 'K', 1, 0x55, 0, 0, 0, 0 };
    odl_msg_t m;
    CHECK(!odl_decode(f, sizeof(f), &m));                    /* unknown type */
    CHECK(!odl_decode(f, 0, &m) && !odl_decode(f, 3, &m));
    /* fuzz: random frames never decode into something with the wrong length */
    srand(7);
    int decoded = 0;
    for (int i = 0; i < 20000; i++) {
        uint8_t r[ODL_MAX_FRAME];
        const size_t n = (size_t)(rand() % ODL_MAX_FRAME);
        for (size_t j = 0; j < n; j++) {
            r[j] = (uint8_t)rand();
        }
        if (n >= 4) {
            r[0] = 'L';
            r[1] = 'K';
            r[2] = 1;
            r[3] = (uint8_t)(1 + rand() % 0x12);
        }
        decoded += odl_decode(r, n, &m);
    }
    CHECK(decoded < 400);                                    /* only exact lengths can decode */
}

static void test_envelope(void)
{
    odl_session_keys_t k;
    memset(&k, 0x33, sizeof(k));
    for (int i = 0; i < 32; i++) {
        k.k_c2d[i] = (uint8_t)i;
        k.k_d2c[i] = (uint8_t)(0x80 + i);
    }
    odl_session_t c, d;
    odl_session_start(&c, &k, 'C');
    odl_session_start(&d, &k, 'D');
    uint8_t inner[40] = { 'O', 'D', 1, 2, 3 }, f[ODL_MAX_FRAME];
    const uint8_t *in;
    size_t il;
    size_t n = odl_wrap(&c, inner, sizeof(inner), f);
    CHECK(n == sizeof(inner) + ODL_ENV_OVERHEAD);
    CHECK(odl_unwrap(&d, f, n, &in, &il) == ODL_RX_OK && il == sizeof(inner) && memcmp(in, inner, il) == 0);
    CHECK(odl_unwrap(&d, f, n, &in, &il) == ODL_RX_REPLAY);
    CHECK(odl_unwrap(&c, f, n, &in, &il) == ODL_RX_BAD_TAG);             /* reflected to the sender */
    uint8_t g[ODL_MAX_FRAME];
    memcpy(g, f, n);
    g[n - 1] ^= 1;
    CHECK(odl_unwrap(&d, g, n, &in, &il) == ODL_RX_BAD_TAG);
    memcpy(g, f, n);
    g[5] ^= 1;
    CHECK(odl_unwrap(&d, g, n, &in, &il) == ODL_RX_WRONG_SESSION);
    CHECK(odl_unwrap(&d, f, ODL_ENV_OVERHEAD, &in, &il) == ODL_RX_MALFORMED);   /* empty inner */
    /* out of order inside the window: each accepted exactly once */
    uint8_t fr[40][ODL_MAX_FRAME];
    size_t fl[40];
    for (int i = 0; i < 40; i++) {
        fl[i] = odl_wrap(&c, inner, sizeof(inner), fr[i]);
    }
    CHECK(odl_unwrap(&d, fr[39], fl[39], &in, &il) == ODL_RX_OK);
    CHECK(odl_unwrap(&d, fr[20], fl[20], &in, &il) == ODL_RX_OK);        /* 19 back: inside */
    CHECK(odl_unwrap(&d, fr[20], fl[20], &in, &il) == ODL_RX_REPLAY);
    CHECK(odl_unwrap(&d, fr[2], fl[2], &in, &il) == ODL_RX_REPLAY);      /* 37 back: outside */
    CHECK(odl_unwrap(&d, fr[38], fl[38], &in, &il) == ODL_RX_OK);
    /* forged frames never move the window */
    uint8_t forged[ODL_MAX_FRAME];
    memcpy(forged, fr[0], fl[0]);
    forged[12] = forged[13] = forged[14] = 0x7F;                        /* huge counter, bad tag */
    CHECK(odl_unwrap(&d, forged, fl[0], &in, &il) == ODL_RX_BAD_TAG);
    size_t n2 = odl_wrap(&c, inner, sizeof(inner), f);
    CHECK(odl_unwrap(&d, f, n2, &in, &il) == ODL_RX_OK);
    uint8_t big[ODL_MAX_INNER + 1] = { 'O', 'D' };
    CHECK(odl_wrap(&c, big, sizeof(big), f) == 0);                       /* too long to wrap */
    CHECK(odl_wrap(&c, big, ODL_MAX_INNER, f) == ODL_MAX_FRAME);
    odl_session_end(&d);
    CHECK(odl_unwrap(&d, f, n2, &in, &il) == ODL_RX_NO_SESSION);
    odl_session_t other;
    odl_session_keys_t k2 = k;
    k2.k_c2d[0] ^= 1;                                                    /* a different key */
    odl_session_start(&other, &k2, 'D');
    n2 = odl_wrap(&c, inner, sizeof(inner), f);
    CHECK(odl_unwrap(&other, f, n2, &in, &il) == ODL_RX_BAD_TAG);
}

static void run_to_sas(void)
{
    CHECK(odl_pairc_start(&C, CTRL_ID, MAC_C, DEV_ID, MAC_D, T));
    pump(5);
}

static void test_happy(void)
{
    for (int order = 0; order < 2; order++) {
        rig(true);
        fake_crypto_seed(100 + order);
        run_to_sas();
        CHECK(C.st == ODL_C_SAS_READY && D.st == ODL_D_SAS_READY);
        CHECK(sas_c() == sas_d() && sas_c() < 1000000);
        if (order == 0) {                     /* MAO first, then the device's user */
            odl_pairc_confirm(&C, T);
            pump(3);
            CHECK(C.st == ODL_C_WAIT_ACCEPT && SD.persists == 0);
            odl_paird_accept(&D, T);
        } else {                              /* device's user first */
            odl_paird_accept(&D, T);
            pump(2);
            CHECK(SD.persists == 0);          /* never without the controller's proof */
            odl_pairc_confirm(&C, T);
        }
        pump(5);
        CHECK(C.st == ODL_C_PAIRED && D.st == ODL_D_ACCEPTED);
        CHECK(SC.persists == 1 && SD.persists == 1);
        CHECK(memcmp(SC.cred.k_link, SD.cred.k_link, ODL_KEY_LEN) == 0);
        CHECK(SC.cred.peer_id == DEV_ID && SD.cred.peer_id == CTRL_ID);
        CHECK(memcmp(SC.cred.peer_mac, MAC_D, 6) == 0 && memcmp(SD.cred.peer_mac, MAC_C, 6) == 0);
        uint8_t zero[ODL_KEY_LEN] = { 0 };
        CHECK(memcmp(C.keys.k_link, zero, ODL_KEY_LEN) == 0 && memcmp(D.keys.k_link, zero, ODL_KEY_LEN) == 0);
        CHECK(C.eph.id == 0 && D.eph.id == 0);  /* ephemeral keys destroyed */
    }
    /* fresh material per attempt */
    rig(true);
    fake_crypto_seed(5);
    run_to_sas();
    const uint32_t s1 = sas_c();
    uint8_t tx1[8];
    memcpy(tx1, C.t.txid, 8);
    odl_pairc_cancel(&C);
    pump(2);
    run_to_sas();
    CHECK(memcmp(tx1, C.t.txid, 8) != 0 && C.st == ODL_C_SAS_READY && sas_c() == sas_d());
    (void)s1;
}

static void test_cancel_reject(void)
{
    const odl_c_state_t at[] = { ODL_C_WAIT_COMMIT, ODL_C_WAIT_REVEAL, ODL_C_SAS_READY, ODL_C_WAIT_ACCEPT };
    for (int i = 0; i < 4; i++) {
        rig(true);
        fake_crypto_seed(200 + i);
        CHECK(odl_pairc_start(&C, CTRL_ID, MAC_C, DEV_ID, MAC_D, T));
        for (int g = 0; g < 20 && C.st != at[i]; g++) {
            if (C.st == ODL_C_SAS_READY) {
                odl_pairc_confirm(&C, T);
            }
            /* deliver one frame at a time */
            if (s_qn) {
                frame_t fr = s_q[0];
                memmove(s_q, s_q + 1, (size_t)(--s_qn) * sizeof(frame_t));
                inject(&fr, fr.from == 'C' ? MAC_C : MAC_D, fr.from == 'C' ? 'D' : 'C');
            }
        }
        CHECK(C.st == at[i]);
        odl_pairc_cancel(&C);
        pump(5);
        CHECK(C.st == ODL_C_CANCELLED && SC.persists == 0 && SD.persists == 0);
        odl_paird_accept(&D, T);             /* too late: nothing to accept */
        pump(3);
        CHECK(SD.persists == 0);
    }
    /* the device's user rejects */
    rig(true);
    run_to_sas();
    odl_pairc_confirm(&C, T);
    odl_paird_reject(&D);
    pump(5);
    CHECK(C.st == ODL_C_FAILED && C.fail == ODL_F_REJECTED && SC.persists == 0 && SD.persists == 0);
}

static void test_mismatch(void)
{
    for (int side = 0; side < 2; side++) {
        rig(true);
        fake_crypto_seed(300 + side);
        if (side == 0) {
            C.debug_mismatch = true;
        } else {
            D.debug_mismatch = true;
        }
        run_to_sas();
        CHECK(C.st == ODL_C_SAS_READY && sas_c() != sas_d());   /* the human would see different codes */
        odl_pairc_confirm(&C, T);                                /* ... and if MATCH is pressed anyway */
        odl_paird_accept(&D, T);
        pump(6);
        CHECK(C.st == ODL_C_FAILED && C.fail == ODL_F_MISMATCH);
        CHECK(SC.persists == 0 && SD.persists == 0);
    }
    /* a device that reveals a nonce other than the committed one */
    rig(true);
    CHECK(odl_pairc_start(&C, CTRL_ID, MAC_C, DEV_ID, MAC_D, T));
    deliver(2);                                                  /* START -> COMMIT -> NONCE */
    CHECK(C.st == ODL_C_WAIT_REVEAL);
    s_qn = 0;
    odl_msg_t fake = { .type = ODL_PAIR_REVEAL, .ctrl_id = CTRL_ID, .dev_id = DEV_ID };
    memcpy(fake.txid, C.t.txid, 8);
    memset(fake.nonce, 0xEE, 16);
    odl_pairc_rx(&C, &fake, MAC_D, T);
    CHECK(C.st == ODL_C_FAILED && C.fail == ODL_F_MISMATCH && SC.persists == 0);
    /* low-order public key from the "device" */
    rig(true);
    CHECK(odl_pairc_start(&C, CTRL_ID, MAC_C, DEV_ID, MAC_D, T));
    s_qn = 0;
    odl_msg_t cm = { .type = ODL_PAIR_COMMIT, .ctrl_id = CTRL_ID, .dev_id = DEV_ID };
    memcpy(cm.txid, C.t.txid, 8);
    odl_transcript_t tt = C.t;                                   /* pub_d = all zero */
    memset(tt.nonce_d, 0x11, 16);
    odl_commit(&tt, cm.commit);
    odl_pairc_rx(&C, &cm, MAC_D, T);
    odl_msg_t rv = { .type = ODL_PAIR_REVEAL, .ctrl_id = CTRL_ID, .dev_id = DEV_ID };
    memcpy(rv.txid, C.t.txid, 8);
    memset(rv.nonce, 0x11, 16);
    odl_pairc_rx(&C, &rv, MAC_D, T);
    CHECK(C.st == ODL_C_FAILED && C.fail == ODL_F_CRYPTO && SC.persists == 0);
}

static void test_not_pairing_and_timeouts(void)
{
    rig(false);                                                  /* device NOT in pair mode */
    CHECK(odl_pairc_start(&C, CTRL_ID, MAC_C, DEV_ID, MAC_D, T));
    pump(40);                                                    /* 4 s */
    CHECK(C.st == ODL_C_FAILED && C.fail == ODL_F_NOT_PAIRING && D.st == ODL_D_LOCKED);
    CHECK(SC.persists == 0 && SD.persists == 0);
    /* pair mode expires */
    rig(true);
    for (int i = 0; i < 610; i++) {
        T += 100;
        odl_paird_tick(&D, T);
    }
    CHECK(D.st == ODL_D_LOCKED);
    /* a user who never decides: the attempt times out on both sides */
    rig(true);
    run_to_sas();
    pump(460);                                                   /* 46 s */
    CHECK(C.st == ODL_C_FAILED && C.fail == ODL_F_TIMEOUT && D.st != ODL_D_SAS_READY);
    CHECK(SC.persists == 0 && SD.persists == 0);
}

static void test_lost_frames(void)
{
    const uint8_t types[] = { ODL_PAIR_START, ODL_PAIR_COMMIT, ODL_PAIR_NONCE, ODL_PAIR_REVEAL, ODL_PAIR_CONFIRM,
                              ODL_PAIR_ACCEPT };
    for (size_t i = 0; i < sizeof(types); i++) {
        rig(true);
        fake_crypto_seed(400 + i);
        s_drop_type = types[i];
        s_drop_count = 2;                                        /* lose it twice */
        CHECK(odl_pairc_start(&C, CTRL_ID, MAC_C, DEV_ID, MAC_D, T));
        pump(20);
        if (C.st == ODL_C_SAS_READY) {
            odl_pairc_confirm(&C, T);
            odl_paird_accept(&D, T);
        }
        pump(30);
        CHECK(C.st == ODL_C_PAIRED && SC.persists == 1 && SD.persists == 1);   /* converged, once each */
        CHECK(memcmp(SC.cred.k_link, SD.cred.k_link, ODL_KEY_LEN) == 0);
    }
}

static void test_replay_and_strangers(void)
{
    rig(true);
    fake_crypto_seed(500);
    run_to_sas();
    odl_pairc_confirm(&C, T);
    odl_paird_accept(&D, T);
    pump(5);
    CHECK(C.st == ODL_C_PAIRED);
    frame_t old[128];
    const int oldn = s_recn;
    memcpy(old, s_rec, sizeof(s_rec));
    odl_credential_t first = SD.cred;
    /* replay every recorded frame at both sides, now idle */
    for (int i = 0; i < oldn; i++) {
        inject(&old[i], old[i].from == 'C' ? MAC_C : MAC_D, old[i].from == 'C' ? 'D' : 'C');
    }
    pump(5);
    CHECK(SD.persists == 1 && SC.persists == 1);                 /* nothing re-committed */
    /* a new attempt, then the old attempt's frames injected into it */
    SC.persists = SD.persists = 0;
    odl_paird_mode(&D, ODL_PAIR_MODE_MS, T);
    run_to_sas();
    for (int i = 0; i < oldn; i++) {
        if (old[i].from == 'C' && old[i].f[3] != ODL_PAIR_START) {
            inject(&old[i], MAC_C, 'D');                         /* old txid: ignored */
        }
        if (old[i].from == 'D') {
            inject(&old[i], MAC_D, 'C');
        }
    }
    CHECK(C.st == ODL_C_SAS_READY && D.st == ODL_D_SAS_READY && sas_c() == sas_d());
    odl_pairc_confirm(&C, T);
    odl_paird_accept(&D, T);
    pump(5);
    CHECK(C.st == ODL_C_PAIRED && SD.persists == 1);
    CHECK(memcmp(first.k_link, SD.cred.k_link, ODL_KEY_LEN) != 0);   /* a new key, not the old one */
    /* frames from a different radio are never part of the transaction */
    rig(true);
    CHECK(odl_pairc_start(&C, CTRL_ID, MAC_C, DEV_ID, MAC_D, T));
    deliver(2);
    s_qn = 0;
    odl_msg_t rv = { .type = ODL_PAIR_ABORT, .ctrl_id = CTRL_ID, .dev_id = DEV_ID, .reason = ODL_ABORT_REJECTED };
    memcpy(rv.txid, C.t.txid, 8);
    odl_pairc_rx(&C, &rv, MAC_X, T);                             /* a stranger's abort */
    CHECK(C.st == ODL_C_WAIT_REVEAL);
    /* a second controller while the device is busy */
    odl_msg_t st2 = { .type = ODL_PAIR_START, .ctrl_id = 0x0DD0021122334455ull, .dev_id = DEV_ID };
    memset(st2.txid, 0x99, 8);
    memset(st2.pub, 0x44, 32);
    odl_paird_rx(&D, &st2, MAC_X, T);
    CHECK(s_qn == 1 && s_q[0].f[3] == ODL_PAIR_ABORT && D.st == ODL_D_EXCHANGING);   /* BUSY, own attempt intact */
    s_qn = 0;
    /* a controller's confirmation with a wrong tag */
    rig(true);
    run_to_sas();
    CHECK(D.st == ODL_D_SAS_READY);
    odl_msg_t bad = { .type = ODL_PAIR_CONFIRM, .ctrl_id = CTRL_ID, .dev_id = DEV_ID };
    memcpy(bad.txid, D.t.txid, 8);
    odl_paird_rx(&D, &bad, MAC_C, T);
    CHECK(D.st == ODL_D_FAILED && D.fail == ODL_F_MISMATCH && SD.persists == 0);
}

static void test_reboot_mid(void)
{
    /* the device reboots with the SAS on screen */
    rig(true);
    run_to_sas();
    const odl_pair_ops_t od = { d_send, persist, changed, &SD };
    odl_paird_init(&D, &od, DEV_ID, MAC_D);                      /* power cycle: LOCKED, no state */
    odl_pairc_confirm(&C, T);
    pump(470);
    CHECK(C.st == ODL_C_FAILED && SC.persists == 0 && SD.persists == 0);
    /* MAO reboots with the SAS on screen */
    rig(true);
    run_to_sas();
    const odl_pair_ops_t oc = { c_send, persist, changed, &SC };
    odl_pairc_init(&C, &oc);
    odl_paird_accept(&D, T);
    pump(470);
    CHECK(SC.persists == 0 && SD.persists == 0 && D.st != ODL_D_ACCEPTED);
    /* and the next attempt works */
    odl_paird_mode(&D, ODL_PAIR_MODE_MS, T);
    run_to_sas();
    odl_pairc_confirm(&C, T);
    odl_paird_accept(&D, T);
    pump(5);
    CHECK(C.st == ODL_C_PAIRED && SC.persists == 1 && SD.persists == 1);
}

static void test_storage_failure(void)
{
    rig(true);
    SD.fail_persist = true;                                      /* the device cannot store it */
    run_to_sas();
    odl_pairc_confirm(&C, T);
    odl_paird_accept(&D, T);
    pump(5);
    CHECK(C.st == ODL_C_FAILED && SC.persists == 0 && D.fail == ODL_F_STORAGE);
    rig(true);
    SC.fail_persist = true;                                      /* MAO cannot store it */
    run_to_sas();
    odl_pairc_confirm(&C, T);
    odl_paird_accept(&D, T);
    pump(5);
    CHECK(C.st == ODL_C_FAILED && C.fail == ODL_F_STORAGE && SC.persists == 0);
    CHECK(SD.persists == 1);                                     /* only PENDING there: see the credential store */
}

int main(void)
{
    test_codec();
    test_envelope();
    test_happy();
    test_cancel_reject();
    test_mismatch();
    test_not_pairing_and_timeouts();
    test_lost_frames();
    test_replay_and_strangers();
    test_reboot_mid();
    test_storage_failure();
    printf("link security (protocol logic): %d checks passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
