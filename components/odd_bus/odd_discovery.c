/*
 * Discovery pacing for controllers: a DISCOVER broadcast every interval.
 * Devices answer with ANNOUNCE (see the device side: lamp_01_test).
 */
#include "odd_bus.h"

void odd_discovery_init(odd_discovery_t *d, uint32_t interval_ms)
{
    d->interval_ms = interval_ms;
    d->next_ms = 0;          /* first round immediately */
    d->sent = 0;
}

void odd_discovery_set_interval(odd_discovery_t *d, uint32_t interval_ms, uint32_t now_ms)
{
    if (interval_ms < d->interval_ms && (int32_t)(d->next_ms - (now_ms + interval_ms)) > 0) {
        d->next_ms = now_ms;  /* speeding up: go now rather than wait out the long gap */
    }
    d->interval_ms = interval_ms;
}

uint32_t odd_discovery_poll(odd_discovery_t *d, uint32_t now_ms)
{
    if ((int32_t)(now_ms - d->next_ms) >= 0) {
        odd_bus_discover();
        d->sent++;
        d->next_ms = now_ms + d->interval_ms;
    }
    return d->next_ms - now_ms;
}
