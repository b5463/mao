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

#define MAO_SETTINGS_ROTATION_DEFAULT  (-1)   /* use the board's (Kconfig) rotation */

typedef struct {
    bool first_boot_done;
    uint8_t volume;        /* 0..100 % */
    uint8_t brightness;    /* 0..100 % */
    int16_t display_rotation;   /* 0/90/180/270, or MAO_SETTINGS_ROTATION_DEFAULT (bring-up override) */
    char hw_revision[8];        /* board revision the boot check last saw ("" = never checked) */
    uint32_t hw_faults;         /* fault mask of that check (mao_selftest MAO_FAULT_*) */
} mao_settings_t;

/* Initialise NVS, open the "mao" namespace and load values (defaults for
 * missing keys). If NVS is unusable, MAO keeps running on defaults. */
esp_err_t mao_settings_init(void);

/* Current values (always valid, defaults if not persisted). */
const mao_settings_t *mao_settings_get(void);

esp_err_t mao_settings_set_first_boot_done(bool done);
esp_err_t mao_settings_set_volume(uint8_t percent);
esp_err_t mao_settings_set_brightness(uint8_t percent);
esp_err_t mao_settings_set_display_rotation(int16_t degrees);
/* Hardware record of the boot check: the revision it ran on and its faults. */
esp_err_t mao_settings_set_hw_record(const char *revision, uint32_t faults);

#ifdef __cplusplus
}
#endif
