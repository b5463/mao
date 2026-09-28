/*
 * ODD device contract: what a controller may do with a decoded capability
 * set (docs/odd_device_contract.md). Pure C, no ESP-IDF dependency.
 *
 * The codec (odd_codec.c) checks structure: lengths, ids, canonical known
 * types. This module decides meaning: which records the controller
 * understands, which ones are ambiguous, and how compatible the device is.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "odd_message.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compatibility, per device. A separate axis from relationship,
 * reachability and authentication. */
typedef enum {
    ODD_COMPAT_UNKNOWN = 0,      /* never described (this boot) */
    ODD_COMPAT_DESCRIBING,       /* description requested, none valid yet */
    ODD_COMPAT_COMPATIBLE,       /* same major; every declared capability understood */
    ODD_COMPAT_LIMITED,          /* same major; a usable subset (maybe empty) */
    ODD_COMPAT_INCOMPATIBLE,     /* another major: no controls */
    ODD_COMPAT_INVALID,          /* malformed description, or none within the bound */
} odd_compat_t;

typedef struct {
    odd_compat_t compat;
    uint8_t usable;              /* bit i: cap[i] may be offered / shown */
    uint8_t unknown;             /* records of a type or semantic this build does not know */
    uint8_t ambiguous;           /* understood records dropped because their semantic repeats */
    uint8_t unusable;            /* understood but degenerate (e.g. LEVEL min == max) */
    bool newer_minor;            /* the device speaks a later minor: known subset only */
} odd_contract_eval_t;

/* Evaluate a decoded CAPABILITIES set. Never fails: every set maps to a
 * compatibility state. */
void odd_contract_evaluate(const odd_caps_msg_t *caps, odd_contract_eval_t *out);

/* True if this build understands the record (known type, known ACTION semantic). */
bool odd_contract_understood(const odd_capability_t *c);

const char *odd_compat_name(odd_compat_t c);

#ifdef __cplusplus
}
#endif
