/*
 * MAO haptics: TI DRV2605L driving an LRA (LD0832AA-0099F, 235 Hz), with a
 * small vocabulary of named touches.
 *
 * Vocabulary (all ROM-library effects, closed-loop LRA, auto-resonance):
 *   tick          dial detent: the shortest, crispest tick
 *   double_tap    two quick taps: "again" / "look"
 *   heartbeat     lub-dub, strong then weaker
 *   short_pulse   one medium click: a notice
 *   tremor        a light shiver
 *   annoyed_buzz  two short buzzes
 *   wake_pulse    a smooth swell: waking up
 *   confirm       one strong click: done / selected, and every face press
 *
 * Every call is non-blocking and safe from any task; a new touch interrupts
 * the one playing. Strength 0..100 % picks one of three amplitude variants
 * of each touch (0 = silent). The driver is fully shut down (EN low) when
 * idle, disabled or while MAO rests (mao_haptics_suspend).
 *
 * Calibration: auto-calibration runs once (first boot, or on request) and
 * its results are kept in NVS ("mao" / "hap_cal"), then loaded on later
 * boots.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_HAPTIC_TICK = 0,
    MAO_HAPTIC_DOUBLE_TAP,
    MAO_HAPTIC_HEARTBEAT,
    MAO_HAPTIC_SHORT_PULSE,
    MAO_HAPTIC_TREMOR,
    MAO_HAPTIC_ANNOYED_BUZZ,
    MAO_HAPTIC_WAKE_PULSE,
    MAO_HAPTIC_CONFIRM,
    MAO_HAPTIC_COUNT,
} mao_haptic_t;

/* Probe the driver and start the haptics task. ESP_ERR_NOT_SUPPORTED on
 * boards without haptics, ESP_ERR_NOT_FOUND if the driver did not answer
 * (MAO then runs without touch feedback). */
esp_err_t mao_haptics_init(void);

void mao_haptics_play(mao_haptic_t haptic);

static inline void mao_haptics_tick(void)         { mao_haptics_play(MAO_HAPTIC_TICK); }
static inline void mao_haptics_double_tap(void)   { mao_haptics_play(MAO_HAPTIC_DOUBLE_TAP); }
static inline void mao_haptics_heartbeat(void)    { mao_haptics_play(MAO_HAPTIC_HEARTBEAT); }
static inline void mao_haptics_short_pulse(void)  { mao_haptics_play(MAO_HAPTIC_SHORT_PULSE); }
static inline void mao_haptics_tremor(void)       { mao_haptics_play(MAO_HAPTIC_TREMOR); }
static inline void mao_haptics_annoyed_buzz(void) { mao_haptics_play(MAO_HAPTIC_ANNOYED_BUZZ); }
static inline void mao_haptics_wake_pulse(void)   { mao_haptics_play(MAO_HAPTIC_WAKE_PULSE); }
static inline void mao_haptics_confirm(void)      { mao_haptics_play(MAO_HAPTIC_CONFIRM); }

/* Global switch. Disabling stops playback and shuts the driver down. */
void mao_haptics_set_enabled(bool enabled);
bool mao_haptics_is_enabled(void);

/* 0..100 %. */
void mao_haptics_set_strength(uint8_t percent);

/* Stop playback and shut the driver down now. */
void mao_haptics_stop(void);

/* MAO rests (light / deep sleep): stop, shut the driver down and refuse
 * touches until resumed (false). Harmless without haptics. */
void mao_haptics_suspend(bool suspend);

/* Re-run auto-calibration (actuator must be free to move) and persist it. */
esp_err_t mao_haptics_calibrate(void);

/* What the last auto-calibration found (diagnostics / factory test). */
typedef struct {
    uint32_t runs;            /* calibrations finished since boot (any result) */
    esp_err_t result;         /* of the last one; ESP_ERR_INVALID_STATE if none ran */
    bool from_nvs;            /* the values in use were loaded at boot */
    uint8_t comp;             /* A_CAL_COMP */
    uint8_t bemf;             /* A_CAL_BEMF */
    uint8_t bemf_gain;
    uint16_t resonance_hz;    /* from LRA_PERIOD, 0 if unknown */
} mao_haptics_cal_info_t;

void mao_haptics_get_cal_info(mao_haptics_cal_info_t *out);

/* Read the driver's device id (DRV2605L = 7) through the haptics task.
 * Blocks up to timeout_ms. */
esp_err_t mao_haptics_identify(uint8_t *device_id, uint32_t timeout_ms);

/* ms since boot (esp_timer) until which MAO itself may be vibrating, so
 * perception does not mistake it for being moved. */
uint32_t mao_haptics_busy_until_ms(void);

const char *mao_haptic_name(mao_haptic_t haptic);
/* Look up a vocabulary name ("tick", "double_tap", ...). */
bool mao_haptic_from_name(const char *name, mao_haptic_t *out);

#ifdef __cplusplus
}
#endif
