/*
 * ODD BUS codec self-test (runs on target, no radio needed). Exercises every
 * message type's encode/decode round trip and each validation rule.
 */
#include <string.h>
#include "esp_log.h"
#include "odd_bus.h"

static const char *TAG = "ODD_BUS";

static int s_fail;
static int s_checks;

#define CHECK(cond, what)                                   \
    do {                                                    \
        s_checks++;                                         \
        if (!(cond)) {                                      \
            s_fail++;                                       \
            ESP_LOGE(TAG, "selftest FAIL: %s", what);       \
        }                                                   \
    } while (0)

static size_t make(uint8_t type, const odd_message_t *body, uint8_t *f)
{
    const odd_header_t h = { .type = type, .seq = 0xFFFE, .src_id = 0x0DD0112233445566ULL,
                             .dst_id = 0x0DD0AABBCCDDEEFFULL };
    return odd_encode(&h, body, f, ODD_MAX_FRAME);
}

static odd_decode_result_t redecode(uint8_t *f, size_t n, odd_message_t *out)
{
    /* Re-seal the CRC after deliberate edits so only the intended rule fails. */
    const uint16_t crc = odd_crc16(f, n - ODD_CRC_LEN);
    f[n - 2] = (uint8_t)crc;
    f[n - 1] = (uint8_t)(crc >> 8);
    return odd_decode(f, n, out);
}

int odd_bus_selftest(void)
{
    s_fail = 0;
    s_checks = 0;
    uint8_t f[ODD_MAX_FRAME];
    odd_message_t b, d;
    size_t n;

    /* CRC-16/CCITT-FALSE reference vector. */
    CHECK(odd_crc16((const uint8_t *)"123456789", 9) == 0x29B1, "crc16 check value");

    /* ANNOUNCE round trip. */
    memset(&b, 0, sizeof(b));
    strcpy(b.u.info.name, "LAMP 01");
    b.u.info.device_type = ODD_DEVICE_LIGHT;
    b.u.info.cap_count = 2;
    n = make(ODD_MSG_ANNOUNCE, &b, f);
    CHECK(n == ODD_HEADER_LEN + 4 + 7 + ODD_CRC_LEN, "announce length");
    CHECK(odd_decode(f, n, &d) == ODD_DECODE_OK, "announce decodes");
    CHECK(!strcmp(d.u.info.name, "LAMP 01") && d.u.info.device_type == ODD_DEVICE_LIGHT &&
          d.u.info.cap_count == 2 && d.hdr.seq == 0xFFFE && d.hdr.src_id == 0x0DD0112233445566ULL &&
          d.hdr.dst_id == 0x0DD0AABBCCDDEEFFULL && d.u.info.id == d.hdr.src_id, "announce fields");

    /* CAPABILITIES with negative ranges round trip. */
    memset(&b, 0, sizeof(b));
    b.u.caps.count = 2;
    b.u.caps.cap[0] = (odd_capability_t) { 1, ODD_CAP_POWER, ODD_CAP_F_WRITE, 0, 1, 1 };
    b.u.caps.cap[1] = (odd_capability_t) { 7, ODD_CAP_LEVEL, 3, -50, 50, 5 };
    n = make(ODD_MSG_CAPABILITIES, &b, f);
    CHECK(odd_decode(f, n, &d) == ODD_DECODE_OK, "caps decode");
    CHECK(d.u.caps.count == 2 && d.u.caps.cap[1].id == 7 && d.u.caps.cap[1].min == -50 &&
          d.u.caps.cap[1].max == 50 && d.u.caps.cap[1].step == 5 && d.u.caps.cap[1].flags == 3, "caps fields");

    /* Maximum frame: 8 capabilities fits well under ESP-NOW's limit. */
    b.u.caps.count = ODD_MAX_CAPS;
    for (int i = 0; i < ODD_MAX_CAPS; i++) {
        b.u.caps.cap[i] = (odd_capability_t) { (uint8_t)(i + 1), ODD_CAP_LEVEL, 1, 0, 100, 1 };
    }
    n = make(ODD_MSG_CAPABILITIES, &b, f);
    CHECK(n == 147 && odd_decode(f, n, &d) == ODD_DECODE_OK, "8-cap frame is 147 B and valid");

    /* STATE, SET_VALUE, ACK, empty requests. */
    memset(&b, 0, sizeof(b));
    b.u.state.in_reply_to = 1234;
    b.u.state.count = 2;
    b.u.state.value[0] = (odd_value_t) { 1, 1 };
    b.u.state.value[1] = (odd_value_t) { 2, -7 };
    n = make(ODD_MSG_STATE, &b, f);
    CHECK(odd_decode(f, n, &d) == ODD_DECODE_OK && d.u.state.in_reply_to == 1234 &&
          d.u.state.value[1].value == -7, "state round trip");

    memset(&b, 0, sizeof(b));
    b.u.set = (odd_value_t) { 2, 42 };
    n = make(ODD_MSG_SET_VALUE, &b, f);
    CHECK(odd_decode(f, n, &d) == ODD_DECODE_OK && d.u.set.cap_id == 2 && d.u.set.value == 42, "set round trip");

    memset(&b, 0, sizeof(b));
    b.u.ack.acked_seq = 65535;
    b.u.ack.status = ODD_ACK_CLAMPED;
    b.u.ack.applied = (odd_value_t) { 2, 100 };
    n = make(ODD_MSG_ACK, &b, f);
    CHECK(odd_decode(f, n, &d) == ODD_DECODE_OK && d.u.ack.acked_seq == 65535 &&
          d.u.ack.status == ODD_ACK_CLAMPED && d.u.ack.applied.value == 100, "ack round trip");

    n = make(ODD_MSG_GET_STATE, NULL, f);
    CHECK(n == ODD_HEADER_LEN + ODD_CRC_LEN && odd_decode(f, n, &d) == ODD_DECODE_OK, "empty get_state");

    /* --- Rejections --------------------------------------------------- */
    memset(&b, 0, sizeof(b));
    b.u.set = (odd_value_t) { 2, 42 };
    n = make(ODD_MSG_SET_VALUE, &b, f);
    uint8_t g[ODD_MAX_FRAME];

    CHECK(odd_decode(f, 10, &d) == ODD_DECODE_NOT_ODD, "too short rejected");
    memcpy(g, f, n); g[0] = 'X';
    CHECK(odd_decode(g, n, &d) == ODD_DECODE_NOT_ODD, "bad magic rejected");
    memcpy(g, f, n); g[2] = 2;
    CHECK(odd_decode(g, n, &d) == ODD_DECODE_BAD_VERSION && d.hdr.version == 2, "v2 rejected as version");
    memcpy(g, f, n); g[ODD_HEADER_LEN + 1] ^= 0x01;
    CHECK(odd_decode(g, n, &d) == ODD_DECODE_BAD_CRC, "bit flip rejected by CRC");
    memcpy(g, f, n);
    CHECK(odd_decode(g, n - 1, &d) == ODD_DECODE_MALFORMED, "truncated frame rejected");
    memcpy(g, f, n); g[3] = 0;
    CHECK(redecode(g, n, &d) == ODD_DECODE_MALFORMED, "type 0 rejected");
    memcpy(g, f, n); g[3] = ODD_MSG_TYPE_MAX + 1;
    CHECK(redecode(g, n, &d) == ODD_DECODE_MALFORMED, "unknown type rejected");
    memcpy(g, f, n); memset(g + 8, 0, 8);
    CHECK(redecode(g, n, &d) == ODD_DECODE_MALFORMED, "zero source id rejected");
    memcpy(g, f, n); g[ODD_HEADER_LEN] = 0;
    CHECK(redecode(g, n, &d) == ODD_DECODE_MALFORMED, "SET for cap 0 rejected");
    memcpy(g, f, n); g[3] = ODD_MSG_ACK;
    CHECK(redecode(g, n, &d) == ODD_DECODE_MALFORMED, "payload size wrong for type rejected");

    memset(&b, 0, sizeof(b));
    strcpy(b.u.info.name, "OK");
    n = make(ODD_MSG_ANNOUNCE, &b, f);
    memcpy(g, f, n); g[ODD_HEADER_LEN + 4] = 0x07;
    CHECK(redecode(g, n, &d) == ODD_DECODE_MALFORMED, "non-printable name rejected");

    memset(&b, 0, sizeof(b));
    b.u.caps.count = 1;
    b.u.caps.cap[0] = (odd_capability_t) { 1, ODD_CAP_LEVEL, 1, 10, 0, 1 };
    n = make(ODD_MSG_CAPABILITIES, &b, f);
    CHECK(odd_decode(f, n, &d) == ODD_DECODE_MALFORMED, "cap min > max rejected");
    b.u.caps.cap[0] = (odd_capability_t) { 1, ODD_CAP_LEVEL, 1, 0, 10, 0 };
    n = make(ODD_MSG_CAPABILITIES, &b, f);
    CHECK(odd_decode(f, n, &d) == ODD_DECODE_MALFORMED, "cap step 0 rejected");
    b.u.caps.count = ODD_MAX_CAPS + 1;
    CHECK(make(ODD_MSG_CAPABILITIES, &b, f) == 0, "encoder refuses 9 caps");

    /* Sequence arithmetic across wrap-around. */
    CHECK(odd_seq_newer(1, 0) && odd_seq_newer(0, 65535) && odd_seq_newer(10, 65530), "seq newer across wrap");
    CHECK(!odd_seq_newer(65530, 10) && !odd_seq_newer(5, 5), "seq older/equal");

    if (s_fail == 0) {
        ESP_LOGI(TAG, "selftest PASS (%d checks)", s_checks);
    } else {
        ESP_LOGE(TAG, "selftest: %d of %d checks FAILED", s_fail, s_checks);
    }
    return s_fail;
}
