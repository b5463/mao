/*
 * Link frame codec (non-DATA types). Every type has one exact length, so a
 * truncated, oversized or padded frame is malformed by construction.
 */
#include "odd_link.h"

#include <string.h>

typedef struct {
    uint8_t type;
    uint8_t has_txid, has_pub, has_commit, nonces, tag_len, has_reason;
} layout_t;

static const layout_t kLayouts[] = {
    { ODL_PAIR_START,   1, 1, 0, 0, 0,  0 },
    { ODL_PAIR_COMMIT,  1, 1, 1, 0, 0,  0 },
    { ODL_PAIR_NONCE,   1, 0, 0, 1, 0,  0 },
    { ODL_PAIR_REVEAL,  1, 0, 0, 1, 0,  0 },
    { ODL_PAIR_CONFIRM, 1, 0, 0, 0, 32, 0 },
    { ODL_PAIR_ACCEPT,  1, 0, 0, 0, 32, 0 },
    { ODL_PAIR_ABORT,   1, 0, 0, 0, 0,  1 },
    { ODL_HELLO,        0, 0, 0, 1, 16, 0 },
    { ODL_HELLO_ACK,    0, 0, 0, 2, 16, 0 },
};

static const layout_t *layout(uint8_t type)
{
    for (size_t i = 0; i < sizeof(kLayouts) / sizeof(kLayouts[0]); i++) {
        if (kLayouts[i].type == type) {
            return &kLayouts[i];
        }
    }
    return NULL;
}

static size_t body_len(const layout_t *l)
{
    return (l->has_txid ? ODL_TXID_LEN : 0) + 16 + (l->has_pub ? ODL_PUB_LEN : 0) +
           (l->has_commit ? ODL_HASH_LEN : 0) + (size_t)l->nonces * ODL_NONCE_LEN + l->tag_len +
           (l->has_reason ? 1 : 0);
}

bool odl_is_link(const uint8_t *f, size_t len)
{
    return len >= ODL_HEAD_LEN && f[0] == ODL_MAGIC0 && f[1] == ODL_MAGIC1;
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (56 - 8 * i));
    }
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

size_t odl_encode(const odl_msg_t *m, uint8_t out[ODL_MAX_FRAME])
{
    const layout_t *l = layout(m->type);
    if (!l) {
        return 0;
    }
    uint8_t *p = out;
    *p++ = ODL_MAGIC0;
    *p++ = ODL_MAGIC1;
    *p++ = ODL_VERSION;
    *p++ = m->type;
    if (l->has_txid) {
        memcpy(p, m->txid, ODL_TXID_LEN);
        p += ODL_TXID_LEN;
    }
    put64(p, m->ctrl_id);
    put64(p + 8, m->dev_id);
    p += 16;
    if (l->has_pub) {
        memcpy(p, m->pub, ODL_PUB_LEN);
        p += ODL_PUB_LEN;
    }
    if (l->has_commit) {
        memcpy(p, m->commit, ODL_HASH_LEN);
        p += ODL_HASH_LEN;
    }
    if (l->nonces >= 1) {
        memcpy(p, m->nonce, ODL_NONCE_LEN);
        p += ODL_NONCE_LEN;
    }
    if (l->nonces >= 2) {
        memcpy(p, m->nonce2, ODL_NONCE_LEN);
        p += ODL_NONCE_LEN;
    }
    if (l->tag_len) {
        memcpy(p, m->tag, l->tag_len);
        p += l->tag_len;
    }
    if (l->has_reason) {
        *p++ = m->reason;
    }
    return (size_t)(p - out);
}

bool odl_decode(const uint8_t *f, size_t len, odl_msg_t *out)
{
    if (!odl_is_link(f, len) || f[2] != ODL_VERSION) {
        return false;
    }
    const layout_t *l = layout(f[3]);
    if (!l || len != ODL_HEAD_LEN + body_len(l)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->type = f[3];
    const uint8_t *p = f + ODL_HEAD_LEN;
    if (l->has_txid) {
        memcpy(out->txid, p, ODL_TXID_LEN);
        p += ODL_TXID_LEN;
    }
    out->ctrl_id = get64(p);
    out->dev_id = get64(p + 8);
    p += 16;
    if (l->has_pub) {
        memcpy(out->pub, p, ODL_PUB_LEN);
        p += ODL_PUB_LEN;
    }
    if (l->has_commit) {
        memcpy(out->commit, p, ODL_HASH_LEN);
        p += ODL_HASH_LEN;
    }
    if (l->nonces >= 1) {
        memcpy(out->nonce, p, ODL_NONCE_LEN);
        p += ODL_NONCE_LEN;
    }
    if (l->nonces >= 2) {
        memcpy(out->nonce2, p, ODL_NONCE_LEN);
        p += ODL_NONCE_LEN;
    }
    if (l->tag_len) {
        memcpy(out->tag, p, l->tag_len);
        p += l->tag_len;
    }
    if (l->has_reason) {
        out->reason = *p;
    }
    return true;
}
