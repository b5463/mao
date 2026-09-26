/*
 * Pairing ceremony state machines (see odd_link_pair.h).
 *
 * Message order (numeric comparison with responder nonce commitment):
 *   C: START(pub_c)          D: COMMIT(pub_d, H(.. pub_c pub_d nonce_d))
 *   C: NONCE(nonce_c)        D: REVEAL(nonce_d)      -> both: Z, TH, SAS
 *   C: CONFIRM(tag_c)        D: (local accept)  persist pending, ACCEPT(tag_d)
 *   C: verify tag_d, persist -> PAIRED
 * The controller retransmits its last frame until the answer arrives; the
 * device answers duplicates idempotently and keeps no timers but timeouts.
 */
#include "odd_link_pair.h"

#include <string.h>

static bool expired(uint32_t now, uint32_t since, uint32_t ms)
{
    return (int32_t)(now - since) >= (int32_t)ms;
}

static void emit(const odl_pair_ops_t *ops, const odl_msg_t *m, uint8_t *keep, size_t *keep_len)
{
    uint8_t f[ODL_MAX_FRAME];
    const size_t n = odl_encode(m, f);
    if (!n) {
        return;
    }
    if (keep) {
        memcpy(keep, f, n);
        *keep_len = n;
    }
    ops->send(ops->ctx, f, n);
}

static void notify(const odl_pair_ops_t *ops)
{
    if (ops->changed) {
        ops->changed(ops->ctx);
    }
}

static odl_msg_t head(uint8_t type, const odl_transcript_t *t)
{
    odl_msg_t m = { .type = type, .ctrl_id = t->ctrl_id, .dev_id = t->dev_id };
    memcpy(m.txid, t->txid, ODL_TXID_LEN);
    return m;
}

/* Z -> TH -> keys. debug_mismatch corrupts only our own transcript copy. */
static int derive(odl_transcript_t *t, olc_x25519_t *eph, const uint8_t peer_pub[32], bool debug_mismatch,
                  uint8_t th[ODL_HASH_LEN], odl_pair_keys_t *keys)
{
    uint8_t z[32];
    int e = olc_x25519_agree(eph, peer_pub, z);
    olc_x25519_destroy(eph);                 /* the ephemeral key is single use */
    if (e == 0) {
        odl_transcript_t tt = *t;
        if (debug_mismatch) {
            tt.nonce_c[0] ^= 0x01;
        }
        e = odl_transcript_hash(&tt, th);
        e |= odl_pair_keys(z, th, keys);
        olc_wipe(&tt, sizeof(tt));
    }
    olc_wipe(z, sizeof(z));
    return e;
}

/* ---------------------------------------------------------------------- */
/* Controller                                                             */
/* ---------------------------------------------------------------------- */

static void c_wipe(odl_pairc_t *c)
{
    olc_x25519_destroy(&c->eph);
    olc_wipe(&c->keys, sizeof(c->keys));
    olc_wipe(c->th, sizeof(c->th));
    olc_wipe(c->t.nonce_c, sizeof(c->t.nonce_c));
    olc_wipe(c->t.nonce_d, sizeof(c->t.nonce_d));
    c->last_len = 0;
}

static void c_end(odl_pairc_t *c, odl_c_state_t st, odl_fail_t fail)
{
    c_wipe(c);
    c->st = st;
    c->fail = fail;
    notify(&c->ops);
}

static void c_abort(odl_pairc_t *c, odl_abort_t reason, odl_fail_t fail)
{
    odl_msg_t m = head(ODL_PAIR_ABORT, &c->t);
    m.reason = (uint8_t)reason;
    emit(&c->ops, &m, NULL, NULL);
    c_end(c, fail == ODL_F_NONE ? ODL_C_CANCELLED : ODL_C_FAILED, fail);
}

void odl_pairc_init(odl_pairc_t *c, const odl_pair_ops_t *ops)
{
    memset(c, 0, sizeof(*c));
    c->ops = *ops;
}

void odl_pairc_reset(odl_pairc_t *c)
{
    c_wipe(c);
    const odl_pair_ops_t ops = c->ops;
    const bool dbg = c->debug_mismatch;
    memset(c, 0, sizeof(*c));
    c->ops = ops;
    c->debug_mismatch = dbg;
}

bool odl_pairc_active(const odl_pairc_t *c)
{
    return c->st >= ODL_C_WAIT_COMMIT && c->st <= ODL_C_WAIT_ACCEPT;
}

bool odl_pairc_start(odl_pairc_t *c, uint64_t ctrl_id, const uint8_t ctrl_mac[6], uint64_t dev_id,
                     const uint8_t dev_mac[6], uint32_t now)
{
    odl_pairc_reset(c);
    c->t.ctrl_id = ctrl_id;
    c->t.dev_id = dev_id;
    memcpy(c->t.mac_c, ctrl_mac, 6);
    memcpy(c->t.mac_d, dev_mac, 6);
    if (olc_random(c->t.txid, ODL_TXID_LEN) || olc_random(c->t.nonce_c, ODL_NONCE_LEN) ||
        olc_x25519_generate(&c->eph)) {
        c_end(c, ODL_C_FAILED, ODL_F_CRYPTO);
        return false;
    }
    memcpy(c->t.pub_c, c->eph.pub, ODL_PUB_LEN);
    c->started_ms = c->last_tx_ms = now;
    c->st = ODL_C_WAIT_COMMIT;
    odl_msg_t m = head(ODL_PAIR_START, &c->t);
    memcpy(m.pub, c->t.pub_c, ODL_PUB_LEN);
    emit(&c->ops, &m, c->last, &c->last_len);
    notify(&c->ops);
    return true;
}

void odl_pairc_rx(odl_pairc_t *c, const odl_msg_t *m, const uint8_t src_mac[6], uint32_t now)
{
    if (!odl_pairc_active(c) || memcmp(m->txid, c->t.txid, ODL_TXID_LEN) != 0 || m->ctrl_id != c->t.ctrl_id ||
        m->dev_id != c->t.dev_id || memcmp(src_mac, c->t.mac_d, 6) != 0) {
        return;                              /* another attempt, another peer, or a replay */
    }
    switch (m->type) {
    case ODL_PAIR_ABORT:
        c_end(c, ODL_C_FAILED, m->reason == ODL_ABORT_REJECTED ? ODL_F_REJECTED
                               : m->reason == ODL_ABORT_MISMATCH ? ODL_F_MISMATCH : ODL_F_REMOTE);
        break;
    case ODL_PAIR_COMMIT:
        if (c->st == ODL_C_WAIT_COMMIT) {
            memcpy(c->t.pub_d, m->pub, ODL_PUB_LEN);
            memcpy(c->commit, m->commit, ODL_HASH_LEN);
            c->st = ODL_C_WAIT_REVEAL;
            c->last_tx_ms = now;
            odl_msg_t n = head(ODL_PAIR_NONCE, &c->t);
            memcpy(n.nonce, c->t.nonce_c, ODL_NONCE_LEN);
            emit(&c->ops, &n, c->last, &c->last_len);
            notify(&c->ops);
        }
        break;
    case ODL_PAIR_REVEAL:
        if (c->st == ODL_C_WAIT_REVEAL) {
            memcpy(c->t.nonce_d, m->nonce, ODL_NONCE_LEN);
            uint8_t expect[ODL_HASH_LEN];
            if (odl_commit(&c->t, expect) != 0 || !olc_equal(expect, c->commit, ODL_HASH_LEN)) {
                c_abort(c, ODL_ABORT_MISMATCH, ODL_F_MISMATCH);   /* the nonce was not the committed one */
                return;
            }
            if (derive(&c->t, &c->eph, c->t.pub_d, c->debug_mismatch, c->th, &c->keys) != 0) {
                c_abort(c, ODL_ABORT_FAILED, ODL_F_CRYPTO);
                return;
            }
            c->st = ODL_C_SAS_READY;
            c->last_len = 0;                 /* nothing to retransmit while the user decides */
            notify(&c->ops);
        }
        break;
    case ODL_PAIR_ACCEPT:
        if (c->st == ODL_C_WAIT_ACCEPT) {
            uint8_t expect[ODL_HASH_LEN];
            if (odl_confirm_tag(c->keys.k_confirm, 'D', c->th, expect) != 0 ||
                !olc_equal(expect, m->tag, ODL_HASH_LEN)) {
                c_abort(c, ODL_ABORT_MISMATCH, ODL_F_MISMATCH);
                return;
            }
            odl_credential_t cred = { .peer_id = c->t.dev_id };
            memcpy(cred.peer_mac, c->t.mac_d, 6);
            memcpy(cred.k_link, c->keys.k_link, ODL_KEY_LEN);
            const bool ok = c->ops.persist && c->ops.persist(c->ops.ctx, &cred);
            olc_wipe(&cred, sizeof(cred));
            c_end(c, ok ? ODL_C_PAIRED : ODL_C_FAILED, ok ? ODL_F_NONE : ODL_F_STORAGE);
        }
        break;
    default:
        break;
    }
}

void odl_pairc_confirm(odl_pairc_t *c, uint32_t now)
{
    if (c->st != ODL_C_SAS_READY) {
        return;
    }
    odl_msg_t m = head(ODL_PAIR_CONFIRM, &c->t);
    if (odl_confirm_tag(c->keys.k_confirm, 'C', c->th, m.tag) != 0) {
        c_abort(c, ODL_ABORT_FAILED, ODL_F_CRYPTO);
        return;
    }
    c->st = ODL_C_WAIT_ACCEPT;
    c->last_tx_ms = now;
    emit(&c->ops, &m, c->last, &c->last_len);
    notify(&c->ops);
}

void odl_pairc_cancel(odl_pairc_t *c)
{
    if (odl_pairc_active(c)) {
        c_abort(c, ODL_ABORT_CANCELLED, ODL_F_NONE);
    }
}

void odl_pairc_tick(odl_pairc_t *c, uint32_t now)
{
    if (!odl_pairc_active(c)) {
        return;
    }
    if (expired(now, c->started_ms, ODL_PAIR_TX_TIMEOUT_MS)) {
        c_abort(c, ODL_ABORT_TIMEOUT, ODL_F_TIMEOUT);
        return;
    }
    if (c->st == ODL_C_WAIT_COMMIT && expired(now, c->started_ms, ODL_PAIR_NO_REPLY_MS)) {
        c_end(c, ODL_C_FAILED, ODL_F_NOT_PAIRING);
        return;
    }
    if (c->last_len && expired(now, c->last_tx_ms, ODL_PAIR_RETRY_MS)) {
        c->last_tx_ms = now;
        c->ops.send(c->ops.ctx, c->last, c->last_len);
    }
}

const char *odl_c_state_name(odl_c_state_t s)
{
    static const char *const k[] = { "IDLE", "WAIT_COMMIT", "WAIT_REVEAL", "SAS_READY", "WAIT_ACCEPT",
                                     "PAIRED", "FAILED", "CANCELLED" };
    return (unsigned)s < sizeof(k) / sizeof(k[0]) ? k[s] : "?";
}

const char *odl_fail_name(odl_fail_t f)
{
    static const char *const k[] = { "none", "not pairing", "rejected", "mismatch", "timeout", "storage",
                                     "crypto", "remote abort" };
    return (unsigned)f < sizeof(k) / sizeof(k[0]) ? k[f] : "?";
}

/* ---------------------------------------------------------------------- */
/* Device                                                                 */
/* ---------------------------------------------------------------------- */

static void d_wipe(odl_paird_t *d)
{
    olc_x25519_destroy(&d->eph);
    olc_wipe(&d->keys, sizeof(d->keys));
    olc_wipe(d->th, sizeof(d->th));
    olc_wipe(&d->t, sizeof(d->t));
    d->remote_confirmed = d->local_accepted = false;
    d->accept_len = 0;
}

static void d_fail(odl_paird_t *d, odl_abort_t reason, odl_fail_t fail)
{
    if (reason) {
        odl_msg_t m = head(ODL_PAIR_ABORT, &d->t);
        m.reason = (uint8_t)reason;
        emit(&d->ops, &m, NULL, NULL);
    }
    d_wipe(d);
    d->st = ODL_D_FAILED;
    d->fail = fail;
    notify(&d->ops);
}

void odl_paird_init(odl_paird_t *d, const odl_pair_ops_t *ops, uint64_t self_id, const uint8_t self_mac[6])
{
    memset(d, 0, sizeof(*d));
    d->ops = *ops;
    d->self_id = self_id;
    memcpy(d->self_mac, self_mac, 6);
}

bool odl_paird_in_mode(const odl_paird_t *d)
{
    return d->st != ODL_D_LOCKED;
}

void odl_paird_mode(odl_paird_t *d, uint32_t duration_ms, uint32_t now)
{
    if (duration_ms) {
        d->mode_until_ms = now + duration_ms;
        if (d->mode_until_ms == 0) {
            d->mode_until_ms = 1;
        }
        if (d->st == ODL_D_LOCKED || d->st == ODL_D_FAILED) {
            d_wipe(d);
            d->st = ODL_D_PAIR_MODE;
            d->fail = ODL_F_NONE;
        }
    } else {
        d->mode_until_ms = 0;
        d_wipe(d);
        d->st = ODL_D_LOCKED;
    }
    notify(&d->ops);
}

static void d_try_commit(odl_paird_t *d)
{
    if (d->st != ODL_D_SAS_READY || !d->remote_confirmed || !d->local_accepted) {
        return;
    }
    odl_credential_t cred = { .peer_id = d->t.ctrl_id };
    memcpy(cred.peer_mac, d->t.mac_c, 6);
    memcpy(cred.k_link, d->keys.k_link, ODL_KEY_LEN);
    const bool ok = d->ops.persist && d->ops.persist(d->ops.ctx, &cred);   /* stored as PENDING */
    olc_wipe(&cred, sizeof(cred));
    if (!ok) {
        d_fail(d, ODL_ABORT_FAILED, ODL_F_STORAGE);
        return;
    }
    odl_msg_t m = head(ODL_PAIR_ACCEPT, &d->t);
    odl_confirm_tag(d->keys.k_confirm, 'D', d->th, m.tag);
    olc_wipe(&d->keys, sizeof(d->keys));     /* k_link is persisted; the ACCEPT frame is kept for retries */
    d->st = ODL_D_ACCEPTED;
    emit(&d->ops, &m, d->accept_frame, &d->accept_len);
    notify(&d->ops);
}

void odl_paird_rx(odl_paird_t *d, const odl_msg_t *m, const uint8_t src_mac[6], uint32_t now)
{
    if (m->dev_id != d->self_id || d->st == ODL_D_LOCKED) {
        return;                              /* not for us, or not in pair mode: silence */
    }
    const bool same_tx = memcmp(m->txid, d->t.txid, ODL_TXID_LEN) == 0 && m->ctrl_id == d->t.ctrl_id &&
                         memcmp(src_mac, d->t.mac_c, 6) == 0;
    if (m->type == ODL_PAIR_START) {
        if (d->st == ODL_D_ACCEPTED && same_tx) {
            return;                          /* a repeat of the finished attempt: never re-run it */
        }
        const bool busy = d->st == ODL_D_EXCHANGING || d->st == ODL_D_SAS_READY;
        if (busy && same_tx) {
            /* our COMMIT was lost: answer again, identically */
            odl_msg_t r = head(ODL_PAIR_COMMIT, &d->t);
            memcpy(r.pub, d->t.pub_d, ODL_PUB_LEN);
            odl_commit(&d->t, r.commit);
            emit(&d->ops, &r, NULL, NULL);
            return;
        }
        if (busy) {
            odl_msg_t r = { .type = ODL_PAIR_ABORT, .ctrl_id = m->ctrl_id, .dev_id = d->self_id,
                            .reason = ODL_ABORT_BUSY };
            memcpy(r.txid, m->txid, ODL_TXID_LEN);
            emit(&d->ops, &r, NULL, NULL);   /* one transaction at a time */
            return;
        }
        d_wipe(d);
        memcpy(d->t.txid, m->txid, ODL_TXID_LEN);
        d->t.ctrl_id = m->ctrl_id;
        d->t.dev_id = d->self_id;
        memcpy(d->t.mac_c, src_mac, 6);      /* the radio source, never a claimed address */
        memcpy(d->t.mac_d, d->self_mac, 6);
        memcpy(d->t.pub_c, m->pub, ODL_PUB_LEN);
        if (olc_random(d->t.nonce_d, ODL_NONCE_LEN) || olc_x25519_generate(&d->eph)) {
            d_fail(d, ODL_ABORT_FAILED, ODL_F_CRYPTO);
            return;
        }
        memcpy(d->t.pub_d, d->eph.pub, ODL_PUB_LEN);
        d->tx_started_ms = now;
        d->st = ODL_D_EXCHANGING;
        odl_msg_t r = head(ODL_PAIR_COMMIT, &d->t);
        memcpy(r.pub, d->t.pub_d, ODL_PUB_LEN);
        odl_commit(&d->t, r.commit);
        emit(&d->ops, &r, NULL, NULL);
        notify(&d->ops);
        return;
    }
    if (!same_tx) {
        return;                              /* stale txid, other controller, or replay */
    }
    switch (m->type) {
    case ODL_PAIR_NONCE:
        if (d->st == ODL_D_EXCHANGING) {
            memcpy(d->t.nonce_c, m->nonce, ODL_NONCE_LEN);
            if (derive(&d->t, &d->eph, d->t.pub_c, d->debug_mismatch, d->th, &d->keys) != 0) {
                d_fail(d, ODL_ABORT_FAILED, ODL_F_CRYPTO);
                return;
            }
            d->st = ODL_D_SAS_READY;
            notify(&d->ops);                 /* the caller shows the SAS */
        }
        if (d->st == ODL_D_SAS_READY) {
            odl_msg_t r = head(ODL_PAIR_REVEAL, &d->t);
            memcpy(r.nonce, d->t.nonce_d, ODL_NONCE_LEN);
            emit(&d->ops, &r, NULL, NULL);   /* first answer, or a lost REVEAL */
        }
        break;
    case ODL_PAIR_CONFIRM:
        if (d->st == ODL_D_SAS_READY) {
            uint8_t expect[ODL_HASH_LEN];
            if (odl_confirm_tag(d->keys.k_confirm, 'C', d->th, expect) != 0 ||
                !olc_equal(expect, m->tag, ODL_HASH_LEN)) {
                d_fail(d, ODL_ABORT_MISMATCH, ODL_F_MISMATCH);
                return;
            }
            if (!d->remote_confirmed) {
                d->remote_confirmed = true;
                notify(&d->ops);
            }
            d_try_commit(d);
        } else if (d->st == ODL_D_ACCEPTED && d->accept_len) {
            d->ops.send(d->ops.ctx, d->accept_frame, d->accept_len);   /* our ACCEPT was lost */
        }
        break;
    case ODL_PAIR_ABORT:
        if (d->st == ODL_D_EXCHANGING || d->st == ODL_D_SAS_READY) {
            d_fail(d, 0, ODL_F_REMOTE);      /* the controller cancelled or gave up */
        }
        break;
    default:
        break;
    }
}

void odl_paird_accept(odl_paird_t *d, uint32_t now)
{
    (void)now;
    if (d->st == ODL_D_SAS_READY && !d->local_accepted) {
        d->local_accepted = true;
        notify(&d->ops);
        d_try_commit(d);
    }
}

void odl_paird_reject(odl_paird_t *d)
{
    if (d->st == ODL_D_EXCHANGING || d->st == ODL_D_SAS_READY) {
        d_fail(d, ODL_ABORT_REJECTED, ODL_F_REJECTED);
    }
}

void odl_paird_tick(odl_paird_t *d, uint32_t now)
{
    if (d->st == ODL_D_LOCKED) {
        return;
    }
    if (d->mode_until_ms && (int32_t)(now - d->mode_until_ms) >= 0) {
        d->mode_until_ms = 0;
        d_wipe(d);
        d->st = ODL_D_LOCKED;                /* the window closed; any authorization is untouched */
        notify(&d->ops);
        return;
    }
    if ((d->st == ODL_D_EXCHANGING || d->st == ODL_D_SAS_READY) &&
        expired(now, d->tx_started_ms, ODL_PAIR_TX_TIMEOUT_MS)) {
        d_fail(d, ODL_ABORT_TIMEOUT, ODL_F_TIMEOUT);
    }
}

const char *odl_d_state_name(odl_d_state_t s)
{
    static const char *const k[] = { "LOCKED", "PAIR_MODE", "EXCHANGING", "SAS_READY", "ACCEPTED", "FAILED" };
    return (unsigned)s < sizeof(k) / sizeof(k[0]) ? k[s] : "?";
}
