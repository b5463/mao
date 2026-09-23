/*
 * ODD BUS v1 codec: explicit little-endian field-by-field encoding (never raw
 * C structs), strict length checks per message type, CRC-16/CCITT-FALSE.
 */
#include <string.h>
#include "odd_bus.h"
#include "odd_message.h"

#define CAP_WIRE_LEN    15   /* id, type, flags, min(4), max(4), step(4) */
#define VALUE_WIRE_LEN  5    /* cap_id, value(4) */
#define INFO_FIXED_LEN  4    /* device_type(2), cap_count, name_len */

/* ---------------------------------------------------------------------- */

uint16_t odd_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

typedef struct {
    uint8_t *p;
    size_t left;
    bool ok;
} wr_t;

static void w8(wr_t *w, uint8_t v)
{
    if (w->left < 1) { w->ok = false; return; }
    *w->p++ = v; w->left--;
}
static void w16(wr_t *w, uint16_t v) { w8(w, (uint8_t)v); w8(w, (uint8_t)(v >> 8)); }
static void w32(wr_t *w, uint32_t v) { w16(w, (uint16_t)v); w16(w, (uint16_t)(v >> 16)); }
static void w64(wr_t *w, uint64_t v) { w32(w, (uint32_t)v); w32(w, (uint32_t)(v >> 32)); }

typedef struct {
    const uint8_t *p;
    size_t left;
    bool ok;
} rd_t;

static uint8_t r8(rd_t *r)
{
    if (r->left < 1) { r->ok = false; return 0; }
    r->left--;
    return *r->p++;
}
static uint16_t r16(rd_t *r) { uint16_t lo = r8(r); return (uint16_t)(lo | ((uint16_t)r8(r) << 8)); }
static uint32_t r32(rd_t *r) { uint32_t lo = r16(r); return lo | ((uint32_t)r16(r) << 16); }
static uint64_t r64(rd_t *r) { uint64_t lo = r32(r); return lo | ((uint64_t)r32(r) << 32); }

/* ---------------------------------------------------------------------- */

static void encode_payload(wr_t *w, uint8_t type, const odd_message_t *b)
{
    switch (type) {
    case ODD_MSG_DISCOVER:
    case ODD_MSG_ANNOUNCE: {
        const size_t n = strnlen(b->u.info.name, ODD_NAME_MAX);
        w16(w, b->u.info.device_type);
        w8(w, b->u.info.cap_count);
        w8(w, (uint8_t)n);
        for (size_t i = 0; i < n; i++) {
            w8(w, (uint8_t)b->u.info.name[i]);
        }
        break;
    }
    case ODD_MSG_CAPABILITIES:
        if (b->u.caps.count > ODD_MAX_CAPS) { w->ok = false; return; }
        w8(w, b->u.caps.count);
        for (int i = 0; i < b->u.caps.count; i++) {
            const odd_capability_t *c = &b->u.caps.cap[i];
            w8(w, c->id); w8(w, c->type); w8(w, c->flags);
            w32(w, (uint32_t)c->min); w32(w, (uint32_t)c->max); w32(w, (uint32_t)c->step);
        }
        break;
    case ODD_MSG_STATE:
        if (b->u.state.count > ODD_MAX_CAPS) { w->ok = false; return; }
        w16(w, b->u.state.in_reply_to);
        w8(w, b->u.state.count);
        for (int i = 0; i < b->u.state.count; i++) {
            w8(w, b->u.state.value[i].cap_id);
            w32(w, (uint32_t)b->u.state.value[i].value);
        }
        break;
    case ODD_MSG_SET_VALUE:
        w8(w, b->u.set.cap_id);
        w32(w, (uint32_t)b->u.set.value);
        break;
    case ODD_MSG_ACK:
        w16(w, b->u.ack.acked_seq);
        w8(w, b->u.ack.status);
        w8(w, b->u.ack.applied.cap_id);
        w32(w, (uint32_t)b->u.ack.applied.value);
        break;
    case ODD_MSG_GET_CAPS:
    case ODD_MSG_GET_STATE:
    default:
        break;
    }
}

size_t odd_encode(const odd_header_t *hdr, const odd_message_t *body, uint8_t *out, size_t out_size)
{
    if (!hdr || !out || out_size < ODD_HEADER_LEN + ODD_CRC_LEN ||
        hdr->type == 0 || hdr->type > ODD_MSG_TYPE_MAX) {
        return 0;
    }
    const bool needs_body = hdr->type != ODD_MSG_GET_CAPS && hdr->type != ODD_MSG_GET_STATE;
    if (needs_body && !body) {
        return 0;
    }

    /* Payload first, after the header, so its length is known. */
    wr_t w = { .p = out + ODD_HEADER_LEN, .left = out_size - ODD_HEADER_LEN - ODD_CRC_LEN, .ok = true };
    if (needs_body) {
        encode_payload(&w, hdr->type, body);
    }
    const size_t payload_len = (size_t)(w.p - (out + ODD_HEADER_LEN));
    if (!w.ok || payload_len > ODD_MAX_PAYLOAD) {
        return 0;
    }

    wr_t h = { .p = out, .left = ODD_HEADER_LEN, .ok = true };
    w8(&h, ODD_MAGIC0); w8(&h, ODD_MAGIC1);
    w8(&h, ODD_BUS_PROTOCOL_VERSION);
    w8(&h, hdr->type);
    w16(&h, hdr->seq);
    w8(&h, 0);
    w8(&h, (uint8_t)payload_len);
    w64(&h, hdr->src_id);
    w64(&h, hdr->dst_id);

    const size_t body_end = ODD_HEADER_LEN + payload_len;
    const uint16_t crc = odd_crc16(out, body_end);
    out[body_end] = (uint8_t)crc;
    out[body_end + 1] = (uint8_t)(crc >> 8);
    return body_end + ODD_CRC_LEN;
}

/* ---------------------------------------------------------------------- */

static bool decode_payload(rd_t *r, uint8_t type, odd_message_t *m)
{
    switch (type) {
    case ODD_MSG_DISCOVER:
    case ODD_MSG_ANNOUNCE: {
        m->u.info.id = m->hdr.src_id;
        m->u.info.protocol_version = m->hdr.version;
        m->u.info.device_type = r16(r);
        m->u.info.cap_count = r8(r);
        const uint8_t n = r8(r);
        if (!r->ok || n == 0 || n > ODD_NAME_MAX || r->left != n) {
            return false;
        }
        for (uint8_t i = 0; i < n; i++) {
            const uint8_t c = r8(r);
            if (c < 0x20 || c > 0x7E) {
                return false;            /* printable ASCII names only */
            }
            m->u.info.name[i] = (char)c;
        }
        m->u.info.name[n] = '\0';
        return m->u.info.cap_count <= ODD_MAX_CAPS;
    }
    case ODD_MSG_CAPABILITIES: {
        const uint8_t count = r8(r);
        if (!r->ok || count > ODD_MAX_CAPS || r->left != (size_t)count * CAP_WIRE_LEN) {
            return false;
        }
        m->u.caps.count = count;
        for (uint8_t i = 0; i < count; i++) {
            odd_capability_t *c = &m->u.caps.cap[i];
            c->id = r8(r); c->type = r8(r); c->flags = r8(r);
            c->min = (int32_t)r32(r); c->max = (int32_t)r32(r); c->step = (int32_t)r32(r);
            if (c->id == 0 || c->min > c->max || c->step <= 0) {
                return false;
            }
        }
        return r->ok;
    }
    case ODD_MSG_STATE: {
        m->u.state.in_reply_to = r16(r);
        const uint8_t count = r8(r);
        if (!r->ok || count > ODD_MAX_CAPS || r->left != (size_t)count * VALUE_WIRE_LEN) {
            return false;
        }
        m->u.state.count = count;
        for (uint8_t i = 0; i < count; i++) {
            m->u.state.value[i].cap_id = r8(r);
            m->u.state.value[i].value = (int32_t)r32(r);
        }
        return r->ok;
    }
    case ODD_MSG_SET_VALUE:
        m->u.set.cap_id = r8(r);
        m->u.set.value = (int32_t)r32(r);
        return r->ok && r->left == 0 && m->u.set.cap_id != 0;
    case ODD_MSG_ACK:
        m->u.ack.acked_seq = r16(r);
        m->u.ack.status = r8(r);
        m->u.ack.applied.cap_id = r8(r);
        m->u.ack.applied.value = (int32_t)r32(r);
        return r->ok && r->left == 0;
    case ODD_MSG_GET_CAPS:
    case ODD_MSG_GET_STATE:
        return r->left == 0;
    default:
        return false;
    }
}

odd_decode_result_t odd_decode(const uint8_t *frame, size_t len, odd_message_t *out)
{
    if (!frame || !out || len < ODD_HEADER_LEN + ODD_CRC_LEN || len > ODD_MAX_FRAME ||
        frame[0] != ODD_MAGIC0 || frame[1] != ODD_MAGIC1) {
        return ODD_DECODE_NOT_ODD;
    }
    if (frame[2] != ODD_BUS_PROTOCOL_VERSION) {
        out->hdr.version = frame[2];
        return ODD_DECODE_BAD_VERSION;
    }
    const uint8_t payload_len = frame[7];
    if ((size_t)ODD_HEADER_LEN + payload_len + ODD_CRC_LEN != len) {
        return ODD_DECODE_MALFORMED;
    }
    const size_t body_end = ODD_HEADER_LEN + payload_len;
    const uint16_t crc = (uint16_t)(frame[body_end] | (frame[body_end + 1] << 8));
    if (crc != odd_crc16(frame, body_end)) {
        return ODD_DECODE_BAD_CRC;
    }

    memset(out, 0, sizeof(*out));
    rd_t r = { .p = frame + 2, .left = ODD_HEADER_LEN - 2, .ok = true };
    out->hdr.version = r8(&r);
    out->hdr.type = r8(&r);
    out->hdr.seq = r16(&r);
    out->hdr.flags = r8(&r);
    out->hdr.payload_len = r8(&r);
    out->hdr.src_id = r64(&r);
    out->hdr.dst_id = r64(&r);
    if (out->hdr.type == 0 || out->hdr.type > ODD_MSG_TYPE_MAX || out->hdr.src_id == 0) {
        return ODD_DECODE_MALFORMED;
    }

    rd_t p = { .p = frame + ODD_HEADER_LEN, .left = payload_len, .ok = true };
    return decode_payload(&p, out->hdr.type, out) ? ODD_DECODE_OK : ODD_DECODE_MALFORMED;
}

const char *odd_msg_type_name(uint8_t type)
{
    static const char *const names[] = {
        "?", "DISCOVER", "ANNOUNCE", "GET_CAPS", "CAPABILITIES", "GET_STATE", "STATE", "SET_VALUE", "ACK",
    };
    return type <= ODD_MSG_TYPE_MAX ? names[type] : "?";
}
