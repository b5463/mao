/*
 * Awinic AW9364 backlight driver: 1-wire pulse-count dimming, pure logic
 * (no ESP-IDF). PRIVATE to mao_board; host-tested in tests/host.
 *
 * Datasheet V2.3 (Feb 2018), "Enable Input", table 1 and figure 7:
 *   - EN has an internal 150 k pull-down: the driver is off from reset.
 *   - The rising edge that enables the device is edge 1 = 20 mA per channel.
 *     Edge n sets (17 - n) / 16 x 20 mA, so 16 steps from 20 mA down to
 *     1.25 mA. The current is held while EN stays high.
 *   - Ready time after the enable edge > 20 us (TON); between edges EN low
 *     0.5 .. 500 us (TLO), high > 0.5 us (THI).
 *   - EN low for longer than TOFF (0.8 .. 2.5 ms) shuts the device down.
 *   - The datasheet does not say what edge 17 does. The plan below never
 *     relies on it: a brighter step (smaller n) shuts the driver down and
 *     counts again from edge 1; a dimmer one adds the missing edges.
 *
 * "Step" here is the edge number n: 0 = off, 1 = brightest .. 16 = dimmest.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AW9364_STEPS            16
#define AW9364_T_READY_US       30      /* > 20 us after the enable edge */
#define AW9364_T_LOW_US         2       /* 0.5 .. 500 us between edges */
#define AW9364_T_HIGH_US        2       /* > 0.5 us */
#define AW9364_T_SHUTDOWN_US    3000    /* > TOFF max 2.5 ms: guaranteed off */

/* Brightness percent -> step: 0 = off, else n = 17 - ceil(16 * percent / 100)
 * clamped to 1..16 (values above 100 count as 100). The step's current is
 * never below the requested share: 1 % gives step 16 (1.25 mA), 50 % step 9
 * (10 mA), 100 % step 1 (20 mA per channel). */
uint8_t aw9364_step_from_percent(uint8_t percent);

/* Per-channel current of a step in microamps (0 for step 0 or out of range). */
uint32_t aw9364_step_current_ua(uint8_t step);

typedef struct {
    /* Drive EN low first. If to_step is 0 it stays low (off). Otherwise EN
     * must have been low for AW9364_T_SHUTDOWN_US before the first edge,
     * which then is the enable edge (step 1). */
    bool shutdown;
    /* Rising edges to emit after that (or on top of the current step when
     * shutdown is false). */
    uint8_t edges;
} aw9364_plan_t;

/* How to get from from_step (what the driver is set to now, 0 = off) to
 * to_step. Steps above 16 are treated as 16. */
aw9364_plan_t aw9364_plan(uint8_t from_step, uint8_t to_step);

#ifdef __cplusplus
}
#endif
