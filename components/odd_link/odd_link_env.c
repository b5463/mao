/*
 * DATA envelope:  'L' 'K' 01 20 | sid(8) | counter(4) | inner | tag(16)
 *                 tag = HMAC-SHA-256(K_dir, header | inner)[0:16]
 * Replay: per-direction counter from 1; the receiver keeps the highest
 * accepted value and a 32-frame bitmap (RFC 4303-style anti-replay window).
 */
#include "odd_link.h"
#include "odd_link_crypto.h"

#include <string.h>

void odl_session_start(odl_session_t *s, const odl_session_keys_t *k, char role)
{
    memset(s, 0, sizeof(*s));
    memcpy(s->sid, k->sid, ODL_SID_LEN);
    memcpy(s->k_tx, role == 'C' ? k->k_c2d : k->k_d2c, ODL_KEY_LEN);
    memcpy(s->k_rx, role == 'C' ? k->k_d2c : k->k_c2d, ODL_KEY_LEN);
    s->valid = true;
}

void odl_session_end(odl_session_t *s)
{
    olc_wipe(s, sizeof(*s));
}

static int tag_of(const uint8_t key[ODL_KEY_LEN], const uint8_t *hdr, const uint8_t *inner, size_t inner_len,
                  uint8_t out[ODL_TAG_LEN])
{
    uint8_t full[32];
    const int e = olc_hmac(key, ODL_KEY_LEN, OLC_PARTS({ hdr, ODL_ENV_HDR_LEN }, { inner, inner_len }), full);
    memcpy(out, full, ODL_TAG_LEN);
    return e;
}

size_t odl_wrap(odl_session_t *s, const uint8_t *inner, size_t inner_len, uint8_t out[ODL_MAX_FRAME])
{
    if (!s->valid || inner_len == 0 || inner_len > ODL_MAX_INNER || s->tx_ctr == UINT32_MAX) {
        return 0;
    }
    const uint32_t ctr = ++s->tx_ctr;
    out[0] = ODL_MAGIC0;
    out[1] = ODL_MAGIC1;
    out[2] = ODL_VERSION;
    out[3] = ODL_DATA;
    memcpy(&out[4], s->sid, ODL_SID_LEN);
    out[12] = (uint8_t)(ctr >> 24);
    out[13] = (uint8_t)(ctr >> 16);
    out[14] = (uint8_t)(ctr >> 8);
    out[15] = (uint8_t)ctr;
    memcpy(&out[ODL_ENV_HDR_LEN], inner, inner_len);
    if (tag_of(s->k_tx, out, inner, inner_len, &out[ODL_ENV_HDR_LEN + inner_len]) != 0) {
        return 0;
    }
    return ODL_ENV_HDR_LEN + inner_len + ODL_TAG_LEN;
}

odl_rx_t odl_unwrap(odl_session_t *s, const uint8_t *f, size_t len, const uint8_t **inner, size_t *inner_len)
{
    if (len < ODL_ENV_OVERHEAD + 1 || len > ODL_MAX_FRAME || !odl_is_link(f, len) || f[2] != ODL_VERSION ||
        f[3] != ODL_DATA) {
        return ODL_RX_MALFORMED;
    }
    if (!s->valid) {
        return ODL_RX_NO_SESSION;
    }
    if (!olc_equal(&f[4], s->sid, ODL_SID_LEN)) {
        return ODL_RX_WRONG_SESSION;
    }
    const uint32_t ctr = ((uint32_t)f[12] << 24) | ((uint32_t)f[13] << 16) | ((uint32_t)f[14] << 8) | f[15];
    const size_t n = len - ODL_ENV_OVERHEAD;
    uint8_t tag[ODL_TAG_LEN];
    if (tag_of(s->k_rx, f, &f[ODL_ENV_HDR_LEN], n, tag) != 0 ||
        !olc_equal(tag, &f[ODL_ENV_HDR_LEN + n], ODL_TAG_LEN)) {
        return ODL_RX_BAD_TAG;
    }
    /* Authentic: now the replay window (never moved by forged frames). */
    if (ctr == 0) {
        return ODL_RX_REPLAY;
    }
    if (ctr > s->rx_hi) {
        const uint32_t shift = ctr - s->rx_hi;
        s->rx_bits = shift >= ODL_REPLAY_WINDOW ? 0 : s->rx_bits << shift;
        s->rx_bits |= 1u;
        s->rx_hi = ctr;
    } else {
        const uint32_t back = s->rx_hi - ctr;
        if (back >= ODL_REPLAY_WINDOW || (s->rx_bits & (1u << back))) {
            return ODL_RX_REPLAY;
        }
        s->rx_bits |= 1u << back;
    }
    *inner = &f[ODL_ENV_HDR_LEN];
    *inner_len = n;
    return ODL_RX_OK;
}

const char *odl_rx_name(odl_rx_t r)
{
    static const char *const k[] = { "ok", "malformed", "no session", "wrong session", "bad tag", "replay" };
    return (unsigned)r < sizeof(k) / sizeof(k[0]) ? k[r] : "?";
}
