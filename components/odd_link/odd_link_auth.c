/*
 * HELLO helpers and the device authorization store (see odd_link_auth.h).
 *
 * Commit-last replacement: a completed ceremony only stores PENDING. The
 * first HELLO that verifies under PENDING proves the controller adopted the
 * new key: PENDING becomes ACTIVE (the old credential is gone). A HELLO that
 * verifies under ACTIVE proves it did not: PENDING is discarded. Nothing an
 * attacker without a key sends can move either slot.
 */
#include "odd_link_auth.h"
#include "odd_link_crypto.h"

#include <string.h>

size_t odl_hello_build(const odl_credential_t *cred, uint64_t self_id, const uint8_t self_mac[6],
                       uint8_t nonce_c[ODL_NONCE_LEN], uint8_t out[ODL_MAX_FRAME])
{
    uint8_t kh[ODL_KEY_LEN];
    odl_msg_t m = { .type = ODL_HELLO, .ctrl_id = self_id, .dev_id = cred->peer_id };
    if (olc_random(nonce_c, ODL_NONCE_LEN) || odl_hello_key(cred->k_link, self_id, cred->peer_id, kh) ||
        odl_hello_tag(kh, self_id, cred->peer_id, self_mac, cred->peer_mac, nonce_c, NULL, m.tag)) {
        olc_wipe(kh, sizeof(kh));
        return 0;
    }
    olc_wipe(kh, sizeof(kh));
    memcpy(m.nonce, nonce_c, ODL_NONCE_LEN);
    return odl_encode(&m, out);
}

bool odl_hello_ack_verify(const odl_credential_t *cred, uint64_t self_id, const uint8_t self_mac[6],
                          const uint8_t nonce_c[ODL_NONCE_LEN], const odl_msg_t *ack, const uint8_t src_mac[6],
                          odl_session_keys_t *keys)
{
    if (ack->type != ODL_HELLO_ACK || ack->ctrl_id != self_id || ack->dev_id != cred->peer_id ||
        memcmp(src_mac, cred->peer_mac, 6) != 0 || memcmp(ack->nonce, nonce_c, ODL_NONCE_LEN) != 0) {
        return false;
    }
    uint8_t kh[ODL_KEY_LEN], tag[ODL_TAG_LEN];
    bool ok = odl_hello_key(cred->k_link, self_id, cred->peer_id, kh) == 0 &&
              odl_hello_tag(kh, self_id, cred->peer_id, self_mac, cred->peer_mac, nonce_c, ack->nonce2, tag) == 0 &&
              olc_equal(tag, ack->tag, ODL_TAG_LEN);
    olc_wipe(kh, sizeof(kh));
    if (ok) {
        ok = odl_session_keys(cred->k_link, self_id, cred->peer_id, self_mac, cred->peer_mac, nonce_c, ack->nonce2,
                              keys) == 0;
    }
    return ok;
}

/* ---------------------------------------------------------------------- */

void odl_devauth_init(odl_devauth_t *d, const odl_devauth_ops_t *ops, uint64_t self_id, const uint8_t self_mac[6],
                      const odl_credential_t *active, const odl_credential_t *pending)
{
    memset(d, 0, sizeof(*d));
    d->ops = *ops;
    d->self_id = self_id;
    memcpy(d->self_mac, self_mac, 6);
    if (active) {
        d->active = *active;
        d->has_active = true;
    }
    if (pending) {
        d->pending = *pending;
        d->has_pending = true;
    }
}

bool odl_devauth_set_pending(odl_devauth_t *d, const odl_credential_t *c)
{
    if (!d->ops.store(d->ops.ctx, ODL_SLOT_PENDING, c)) {
        return false;
    }
    d->pending = *c;
    d->has_pending = true;
    return true;
}

bool odl_devauth_locked(const odl_devauth_t *d)
{
    return d->has_active || d->has_pending;
}

bool odl_devauth_has_session(const odl_devauth_t *d)
{
    return d->sess.valid;
}

bool odl_devauth_is_session_peer(const odl_devauth_t *d, const uint8_t mac[6])
{
    return d->sess.valid && memcmp(mac, d->sess_mac, 6) == 0;
}

static bool hello_valid(const odl_devauth_t *d, const odl_credential_t *c, const odl_msg_t *m,
                        const uint8_t src_mac[6])
{
    if (m->ctrl_id != c->peer_id || memcmp(src_mac, c->peer_mac, 6) != 0) {
        return false;                        /* bound controller identity and radio, or nothing */
    }
    uint8_t kh[ODL_KEY_LEN], tag[ODL_TAG_LEN];
    const bool ok = odl_hello_key(c->k_link, c->peer_id, d->self_id, kh) == 0 &&
                    odl_hello_tag(kh, c->peer_id, d->self_id, src_mac, d->self_mac, m->nonce, NULL, tag) == 0 &&
                    olc_equal(tag, m->tag, ODL_TAG_LEN);
    olc_wipe(kh, sizeof(kh));
    return ok;
}

static bool seen_before(const odl_devauth_t *d, const uint8_t nc[ODL_NONCE_LEN])
{
    for (int i = 0; i < ODL_HELLO_SEEN; i++) {
        if (memcmp(d->seen[i], nc, ODL_NONCE_LEN) == 0) {
            return true;
        }
    }
    return false;
}

odl_hello_result_t odl_devauth_hello(odl_devauth_t *d, const odl_msg_t *m, const uint8_t src_mac[6])
{
    if (m->type != ODL_HELLO || m->dev_id != d->self_id) {
        return ODL_HELLO_IGNORED;
    }
    if (d->sess.valid && memcmp(m->nonce, d->last_nc, ODL_NONCE_LEN) == 0 &&
        memcmp(src_mac, d->sess_mac, 6) == 0 && d->last_ack_len) {
        d->ops.send(d->ops.ctx, d->last_ack, d->last_ack_len);   /* our ACK was lost */
        return ODL_HELLO_REPEATED;
    }
    const bool under_pending = d->has_pending && hello_valid(d, &d->pending, m, src_mac);
    const bool under_active = !under_pending && d->has_active && hello_valid(d, &d->active, m, src_mac);
    if (!under_pending && !under_active) {
        d->hello_bad++;
        return ODL_HELLO_REFUSED;
    }
    if (seen_before(d, m->nonce)) {
        return ODL_HELLO_REPLAYED;           /* a recorded HELLO: no new session for it */
    }
    memcpy(d->seen[d->seen_i], m->nonce, ODL_NONCE_LEN);
    d->seen_i = (uint8_t)((d->seen_i + 1) % ODL_HELLO_SEEN);

    odl_hello_result_t res = ODL_HELLO_NEW_SESSION;
    if (under_pending) {
        /* The controller uses the new key: it becomes the authorization. */
        if (d->ops.store(d->ops.ctx, ODL_SLOT_ACTIVE, &d->pending)) {
            d->ops.erase(d->ops.ctx, ODL_SLOT_PENDING);
            d->active = d->pending;
            d->has_active = true;
            olc_wipe(&d->pending, sizeof(d->pending));
            d->has_pending = false;
            d->promoted++;
            res = ODL_HELLO_PROMOTED;
        } else {
            return ODL_HELLO_REFUSED;        /* could not commit: stay as we are, it will retry */
        }
    } else if (d->has_pending && d->pending.peer_id == d->active.peer_id) {
        /* The same controller kept its old key (its commit failed): drop the
         * new one. A pending credential of a DIFFERENT controller (a
         * replacement in progress) is left alone: only that controller's
         * own HELLO can decide it. */
        d->ops.erase(d->ops.ctx, ODL_SLOT_PENDING);
        olc_wipe(&d->pending, sizeof(d->pending));
        d->has_pending = false;
        d->discarded++;
    }

    odl_msg_t ack = { .type = ODL_HELLO_ACK, .ctrl_id = d->active.peer_id, .dev_id = d->self_id };
    memcpy(ack.nonce, m->nonce, ODL_NONCE_LEN);
    odl_session_keys_t keys;
    uint8_t kh[ODL_KEY_LEN];
    if (olc_random(ack.nonce2, ODL_NONCE_LEN) ||
        odl_hello_key(d->active.k_link, d->active.peer_id, d->self_id, kh) ||
        odl_hello_tag(kh, d->active.peer_id, d->self_id, src_mac, d->self_mac, m->nonce, ack.nonce2, ack.tag) ||
        odl_session_keys(d->active.k_link, d->active.peer_id, d->self_id, src_mac, d->self_mac, m->nonce,
                         ack.nonce2, &keys)) {
        olc_wipe(kh, sizeof(kh));
        return ODL_HELLO_REFUSED;
    }
    olc_wipe(kh, sizeof(kh));
    if (d->sess.valid && memcmp(d->sess_mac, src_mac, 6) != 0) {
        d->ops.peer(d->ops.ctx, d->sess_mac, NULL);   /* the previous controller's encrypted peer */
    }
    odl_session_end(&d->sess);               /* the previous session is over */
    odl_session_start(&d->sess, &keys, 'D');
    memcpy(d->sess_mac, src_mac, 6);
    memcpy(d->last_nc, m->nonce, ODL_NONCE_LEN);
    d->ops.peer(d->ops.ctx, src_mac, keys.lmk);   /* a fresh LMK for this session */
    olc_wipe(&keys, sizeof(keys));
    d->last_ack_len = odl_encode(&ack, d->last_ack);
    d->ops.send(d->ops.ctx, d->last_ack, d->last_ack_len);
    d->hello_ok++;
    return res;
}

odl_rx_t odl_devauth_unwrap(odl_devauth_t *d, const uint8_t src_mac[6], const uint8_t *f, size_t len,
                            const uint8_t **inner, size_t *inner_len)
{
    if (!d->sess.valid) {
        return ODL_RX_NO_SESSION;
    }
    if (memcmp(src_mac, d->sess_mac, 6) != 0) {
        return ODL_RX_WRONG_SESSION;         /* right bytes from the wrong radio are still wrong */
    }
    return odl_unwrap(&d->sess, f, len, inner, inner_len);
}

size_t odl_devauth_wrap(odl_devauth_t *d, const uint8_t *inner, size_t len, uint8_t out[ODL_MAX_FRAME])
{
    return odl_wrap(&d->sess, inner, len, out);
}

bool odl_devauth_revoke(odl_devauth_t *d)
{
    bool ok = true;
    if (d->has_active) {
        ok &= d->ops.erase(d->ops.ctx, ODL_SLOT_ACTIVE);
    }
    if (d->has_pending) {
        ok &= d->ops.erase(d->ops.ctx, ODL_SLOT_PENDING);
    }
    if (d->sess.valid) {
        d->ops.peer(d->ops.ctx, d->sess_mac, NULL);
    }
    odl_session_end(&d->sess);
    olc_wipe(&d->active, sizeof(d->active));
    olc_wipe(&d->pending, sizeof(d->pending));
    d->has_active = d->has_pending = false;
    d->last_ack_len = 0;
    return ok;
}

const char *odl_hello_result_name(odl_hello_result_t r)
{
    static const char *const k[] = { "ignored", "refused", "replayed", "repeated", "new session", "promoted" };
    return (unsigned)r < sizeof(k) / sizeof(k[0]) ? k[r] : "?";
}
