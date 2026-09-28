/*
 * MAO persistent settings (NVS namespace "mao").
 *
 * MAO owns only the keys in its own namespace and never erases foreign data
 * (e.g. the factory firmware's leftover namespaces). Each value is its own
 * key; a schema version key ("ver") allows later migrations.
 *
 * Only small, genuinely persistent preferences belong here. Volatile state
 * (character animation, view, dial position) is never stored.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_SETTINGS_SCHEMA_VERSION  1

typedef struct {
    bool first_boot_done;
    uint8_t volume;        /* 0..100 % */
    uint8_t brightness;    /* 0..100 % */
    uint8_t devices_opened; /* times DEVICES was opened from HOME, up to MAO_SETTINGS_HINT_UNTIL */
} mao_settings_t;

#define MAO_SETTINGS_HINT_UNTIL 5   /* HOME shows its PRESS hint until DEVICES was opened this often */

/* Initialise NVS, open the "mao" namespace and load values (defaults for
 * missing keys). If NVS is unusable, MAO keeps running on defaults. */
esp_err_t mao_settings_init(void);

/* Current values (always valid, defaults if not persisted). */
const mao_settings_t *mao_settings_get(void);

esp_err_t mao_settings_set_first_boot_done(bool done);
esp_err_t mao_settings_set_volume(uint8_t percent);
esp_err_t mao_settings_set_brightness(uint8_t percent);
/* DEVICES was opened from HOME (counts up to MAO_SETTINGS_HINT_UNTIL, then stops writing). */
void mao_settings_note_devices_opened(void);

#ifdef __cplusplus
}
#endif
