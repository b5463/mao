/*
 * AW9364 pulse-count dimming (pure logic). See aw9364_dimming.h.
 */
#include "aw9364_dimming.h"

#define FULL_SCALE_UA   20000u      /* 20 mA per channel at step 1 */

static uint8_t clamp_step(uint8_t step)
{
    return step > AW9364_STEPS ? AW9364_STEPS : step;
}

uint8_t aw9364_step_from_percent(uint8_t percent)
{
    if (percent == 0) {
        return 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    /* ceil(16 * percent / 100) is 1..16 for percent 1..100 */
    const unsigned sixteenths = (AW9364_STEPS * (unsigned)percent + 99u) / 100u;
    return (uint8_t)(AW9364_STEPS + 1u - sixteenths);
}

uint32_t aw9364_step_current_ua(uint8_t step)
{
    if (step == 0 || step > AW9364_STEPS) {
        return 0;
    }
    return FULL_SCALE_UA * (uint32_t)(AW9364_STEPS + 1u - step) / AW9364_STEPS;
}

aw9364_plan_t aw9364_plan(uint8_t from_step, uint8_t to_step)
{
    from_step = clamp_step(from_step);
    to_step = clamp_step(to_step);
    if (to_step == from_step) {
        return (aw9364_plan_t) { .shutdown = false, .edges = 0 };
    }
    if (to_step == 0) {
        return (aw9364_plan_t) { .shutdown = true, .edges = 0 };
    }
    if (from_step != 0 && to_step > from_step) {
        /* Dimmer: count on from where the driver is. */
        return (aw9364_plan_t) { .shutdown = false, .edges = (uint8_t)(to_step - from_step) };
    }
    /* From off, or brighter: start over from edge 1 after a full shutdown
     * (never edge 17: the datasheet does not say whether it wraps). */
    return (aw9364_plan_t) { .shutdown = true, .edges = to_step };
}
