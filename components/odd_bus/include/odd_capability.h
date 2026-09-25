/*
 * ODD BUS capability model.
 *
 * A capability is a typed, bounded value a device exposes. Controllers build
 * their interface from capabilities; they never branch on device type.
 * Values are int32 on the wire; the type says how to interpret them.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODD_MAX_CAPS 8

typedef enum {
    ODD_CAP_NONE   = 0,
    ODD_CAP_POWER  = 1,  /* on/off: min 0, max 1, step 1 */
    ODD_CAP_LEVEL  = 2,  /* generic bounded level (brightness, volume, speed...) */
    ODD_CAP_ACTION = 3,  /* a discrete operation performed exactly once; no stored value.
                          * Canonical encoding: min = max = semantic, step = 1 (fits the
                          * v1 record and its validation untouched). */
} odd_cap_type_t;

/* What an ACTION does, generically - never product-specific. 0 is invalid. */
typedef enum {
    ODD_ACTION_IDENTIFY = 1,   /* "make this physical device briefly identify itself" */
} odd_action_semantic_t;



typedef enum {
    ODD_CAP_F_READ   = 1u << 0,
    ODD_CAP_F_WRITE  = 1u << 1,
    ODD_CAP_F_NOTIFY = 1u << 2,   /* device reports changes it makes itself */
} odd_cap_flags_t;

typedef struct {
    uint8_t id;          /* device-local capability id (1..255) */
    uint8_t type;        /* odd_cap_type_t */
    uint8_t flags;       /* odd_cap_flags_t */
    int32_t min;
    int32_t max;
    int32_t step;
} odd_capability_t;

typedef struct {
    uint8_t cap_id;
    int32_t value;
} odd_value_t;

const char *odd_cap_type_name(uint8_t type);
const char *odd_action_semantic_name(int32_t semantic);

/* The semantic of an ACTION capability (carried in its min == max field). */
static inline int32_t odd_action_semantic_of(const odd_capability_t *c)
{
    return c->min;
}

#ifdef __cplusplus
}
#endif
