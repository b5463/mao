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
 *     6    1  flags        odd_frame_flags_t (bits outside it: reject)
 *     7    1  payload_len  bytes of payload
 *     8    8  src_id       sender's device id
 *    16    8  dst_id       target device id, 0 = any
 *    24    n  payload
 *  24+n    2  crc16        CRC-16/CCITT-FALSE over bytes [0, 24+n)
 *
 * Header 24 B + CRC 2 B; largest v1 message (CAPABILITIES with 8 caps) is
 * 147 B, comfortably under ESP-NOW's 250 B.
 *
 * IDENTITY MODEL (see docs/odd_bus_identity.md). Four separate concepts:
 *   device_id       stable hardware identity (0x0DD0 + MAC), never changes
 *   incarnation_id  random 64-bit value a CONTROLLER generates once per
 *                   boot: "this particular running instance". Never stored.
 *   seq             command ordering within one incarnation (wraps)
 *   transfer_id     one visual connect interaction (MAO-internal, not wire)
 *
 * ODD_FRAME_F_INCARNATION marks a frame whose payload begins with the
 * sender's 64-bit incarnation (little-endian), before the message's normal
 * payload. SESSION_OPEN, SET_VALUE, ACK, ACTION and ACTION_RESULT may carry
 * it; for SESSION_OPEN, ACTION and ACTION_RESULT it is mandatory - there is
 * no legacy unflagged ACTION form, by design. A device
 * tracks per controller the CURRENT and PREVIOUS incarnation: SESSION_OPEN
 * establishes a new current (previous is refused - a delayed old open can
 * never reclaim the session), and state-changing commands execute only when
 * their incarnation IS the current one. Safety invariants:
 *
 *   A state-changing command is uniquely identified by (source device_id,
 *   incarnation_id, command semantic, seq).
 *   A command from a non-current incarnation must never execute.
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
    ODD_MSG_ACK          = 0x08,  /* result of a SET_VALUE / SESSION_OPEN */
    ODD_MSG_SESSION_OPEN = 0x09,  /* controller establishes its incarnation (flag required) */
    ODD_MSG_ACTION       = 0x0A,  /* invoke a discrete operation (flag required) */
    ODD_MSG_ACTION_RESULT = 0x0B, /* asynchronous completion of an ACTION (flag required) */
    ODD_MSG_TYPE_MAX     = ODD_MSG_ACTION_RESULT,
} odd_msg_type_t;

/* Frame flags (header byte 6). Unknown bits are rejected as malformed. */
typedef enum {
    ODD_FRAME_F_INCARNATION = 0x01,   /* payload starts with u64 incarnation */
} odd_frame_flags_t;
#define ODD_FRAME_FLAGS_KNOWN  ODD_FRAME_F_INCARNATION

typedef enum {
    ODD_ACK_OK            = 0,
    ODD_ACK_CLAMPED       = 1,   /* applied, value adjusted to limits/step */
    ODD_ACK_STALE         = 2,   /* older than an already applied command: ignored */
    ODD_ACK_UNKNOWN_CAP   = 3,
    ODD_ACK_READ_ONLY     = 4,
    ODD_ACK_NO_SESSION    = 5,   /* not executed: open a session first (device rebooted?) */
    ODD_ACK_STALE_SESSION = 6,   /* not executed: incarnation is a previous one */
    ODD_ACK_ACCEPTED      = 7,   /* ACTION taken for execution (completion comes separately) */
    ODD_ACK_BUSY          = 8,   /* ACTION refused: the device cannot start it now */
} odd_ack_status_t;

/* Final outcome of an ACTION (ODD_MSG_ACTION_RESULT). 0 is invalid; any other
 * value a newer device may send decodes, and a controller treats values it
 * does not know as FAILED. */
typedef enum {
    ODD_ACTION_R_DONE   = 1,
    ODD_ACTION_R_FAILED = 2,
} odd_action_result_t;

typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t seq;
    uint8_t flags;
    uint8_t payload_len;
    uint64_t src_id;
    uint64_t dst_id;
} odd_header_t;

/* Device contract version (docs/odd_device_contract.md). Carried in an
 * optional descriptor at the front of CAPABILITIES:
 *   [ODD_CAPS_DESCRIPTOR][len >= 2][major][minor][len-2 bytes, skipped][count][records]
 * A payload that starts with count (<= ODD_MAX_CAPS) has no descriptor and
 * means contract 1.0. MAJOR = breaking semantics, MINOR = additive. The wire
 * version (header byte 2) stays 1: it only changes if the framing does. */
#define ODD_CONTRACT_MAJOR      1
#define ODD_CONTRACT_MINOR      1      /* what this build implements */
#define ODD_CAPS_DESCRIPTOR     0xD0
#define ODD_CAPS_DESC_MAX       16     /* descriptor bytes after the length */

typedef struct {
    uint8_t major, minor;          /* contract; 1.0 when no descriptor was sent */
    bool has_descriptor;           /* encode: send one; decode: one was present */
    uint8_t count;                 /* 0 (records not parsed) when major != ODD_CONTRACT_MAJOR */
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

/* ACTION: invoke capability `cap_id` once. The command's identity is
 * (source device_id, incarnation, cap_id, header seq). */
typedef struct {
    uint8_t cap_id;
} odd_action_msg_t;

/* ACTION_RESULT: completion of the invocation identified by
 * (the controller incarnation in the prefix, cap_id, action_seq). */
typedef struct {
    uint8_t cap_id;
    uint16_t action_seq;           /* the ORIGINAL action's sequence number */
    uint8_t result;                /* odd_action_result_t */
} odd_action_result_msg_t;

/* A decoded, validated message. */
typedef struct {
    odd_header_t hdr;
    uint8_t src_mac[6];
    int8_t rssi;
    uint64_t incarnation;          /* != 0 iff ODD_FRAME_F_INCARNATION was set */
    union {
        odd_device_info_t info;    /* DISCOVER, ANNOUNCE */
        odd_caps_msg_t caps;       /* CAPABILITIES */
        odd_state_msg_t state;     /* STATE */
        odd_value_t set;           /* SET_VALUE */
        odd_ack_msg_t ack;         /* ACK */
        odd_action_msg_t action;   /* ACTION */
        odd_action_result_msg_t action_result;   /* ACTION_RESULT */
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
