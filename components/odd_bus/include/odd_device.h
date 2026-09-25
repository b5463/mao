/*
 * ODD BUS device identity.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODD_NAME_MAX   16    /* bytes, not NUL-terminated on the wire */

/* Device classes. Describe what a device is, never how to control it:
 * control is derived from capabilities. */
typedef enum {
    ODD_DEVICE_UNKNOWN    = 0,
    ODD_DEVICE_CONTROLLER = 1,   /* MAO */
    ODD_DEVICE_LIGHT      = 2,   /* LAMP 01, LAMP 02 */
    ODD_DEVICE_DISPLAY    = 3,   /* KINO D4 (future) */
    ODD_DEVICE_CLOCK      = 4,   /* CLOCK 01 (future) */
    ODD_DEVICE_SPEAKER    = 5,   /* SPEAKER 01 (future) */
    ODD_DEVICE_CAMERA     = 6,   /* CAMERA 01 emulator today; KINO D4 later */
} odd_device_type_t;

/*
 * Stable identity: 64-bit id derived from the device's factory MAC
 * (0x0DD0 marker in the top 16 bits, MAC in the low 48). Never shown to users;
 * `name` is the user-facing label.
 */
typedef struct {
    uint64_t id;
    char name[ODD_NAME_MAX + 1];     /* NUL-terminated locally */
    uint16_t device_type;            /* odd_device_type_t */
    uint8_t protocol_version;
    uint8_t cap_count;
} odd_device_info_t;

uint64_t odd_id_from_mac(const uint8_t mac[6]);
const char *odd_device_type_name(uint16_t type);

#ifdef __cplusplus
}
#endif
