/*
 * ODD BUS v1 messages.
 *
 * Wire format (all integers little-endian, no struct padding on the wire):
 *
 *   off size  field
 *     0    2  magic        'O' 'D'
 *     2    1  version      1
 *     3    1  type         odd_msg_type_t
 *     4    2  seq          sender's sequence number (wraps)
 *     6    1  flags        reserved, 0
 *     7    1  payload_len  bytes of payload
 *     8    8  src_id       sender's device id
 *    16    8  dst_id       target device id, 0 = any
 *    24    n  payload
 *  24+n    2  crc16        CRC-16/CCITT-FALSE over bytes [0, 24+n)
 *
 * Header 24 B + CRC 2 B; largest v1 message (CAPABILITIES with 8 caps) is
 * 147 B, comfortably under ESP-NOW's 250 B.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "odd_capability.h"
#include "odd_device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ODD_MAGIC0            'O'
#define ODD_MAGIC1            'D'
#define ODD_HEADER_LEN        24
#define ODD_CRC_LEN           2
#define ODD_MAX_FRAME         250
#define ODD_MAX_PAYLOAD       (ODD_MAX_FRAME - ODD_HEADER_LEN - ODD_CRC_LEN)
#define ODD_ID_ANY            0ULL

typedef enum {
    ODD_MSG_DISCOVER     = 0x01,  /* payload: sender info. "Who is there?" */
    ODD_MSG_ANNOUNCE     = 0x02,  /* payload: sender info. "I am ..." */
    ODD_MSG_GET_CAPS     = 0x03,  /* empty */
    ODD_MSG_CAPABILITIES = 0x04,  /* capability list */
    ODD_MSG_GET_STATE    = 0x05,  /* empty */
    ODD_MSG_STATE        = 0x06,  /* current values (reply or change notification) */
    ODD_MSG_SET_VALUE    = 0x07,  /* one value */
    ODD_MSG_ACK          = 0x08,  /* result of a SET_VALUE */
    ODD_MSG_TYPE_MAX     = ODD_MSG_ACK,
} odd_msg_type_t;

typedef enum {
    ODD_ACK_OK          = 0,
    ODD_ACK_CLAMPED     = 1,   /* applied, value adjusted to limits/step */
    ODD_ACK_STALE       = 2,   /* older than an already applied command: ignored */
    ODD_ACK_UNKNOWN_CAP = 3,
    ODD_ACK_READ_ONLY   = 4,
} odd_ack_status_t;

typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t seq;
    uint8_t flags;
    uint8_t payload_len;
    uint64_t src_id;
    uint64_t dst_id;
} odd_header_t;

typedef struct {
    uint8_t count;
    odd_capability_t cap[ODD_MAX_CAPS];
} odd_caps_msg_t;

typedef struct {
    uint16_t in_reply_to;          /* seq of the GET_STATE answered, 0 = notification */
    uint8_t count;
    odd_value_t value[ODD_MAX_CAPS];
} odd_state_msg_t;

typedef struct {
    uint16_t acked_seq;
    uint8_t status;                /* odd_ack_status_t */
    odd_value_t applied;           /* value now in effect */
} odd_ack_msg_t;

/* A decoded, validated message. */
typedef struct {
    odd_header_t hdr;
    uint8_t src_mac[6];
    int8_t rssi;
    union {
        odd_device_info_t info;    /* DISCOVER, ANNOUNCE */
        odd_caps_msg_t caps;       /* CAPABILITIES */
        odd_state_msg_t state;     /* STATE */
        odd_value_t set;           /* SET_VALUE */
        odd_ack_msg_t ack;         /* ACK */
    } u;
} odd_message_t;

typedef enum {
    ODD_DECODE_OK = 0,
    ODD_DECODE_NOT_ODD,            /* too short / wrong magic */
    ODD_DECODE_BAD_VERSION,
    ODD_DECODE_BAD_CRC,
    ODD_DECODE_MALFORMED,          /* type, length or content invalid */
} odd_decode_result_t;

/* Encode header + payload + CRC into out (>= ODD_MAX_FRAME). Returns the
 * frame length, or 0 if the message cannot be encoded. */
size_t odd_encode(const odd_header_t *hdr, const odd_message_t *body, uint8_t *out, size_t out_size);

/* Validate and decode one frame. */
odd_decode_result_t odd_decode(const uint8_t *frame, size_t len, odd_message_t *out);

uint16_t odd_crc16(const uint8_t *data, size_t len);
const char *odd_msg_type_name(uint8_t type);

/* True if sequence a is newer than b (serial-number arithmetic, 16 bit). */
static inline bool odd_seq_newer(uint16_t a, uint16_t b)
{
    return (int16_t)(a - b) > 0;
}

#ifdef __cplusplus
}
#endif
