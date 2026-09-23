/*
 * Capability -> control mapping. This is the only place that decides how a
 * device is operated, and it looks exclusively at capability types, flags
 * and ranges: never at the device type or name.
 */
#include "mao_devices.h"

static bool writable(const odd_capability_t *c)
{
    return (c->flags & ODD_CAP_F_WRITE) != 0;
}

void mao_device_controls(const mao_device_t *dev, mao_device_controls_t *out)
{
    out->level_idx = -1;
    out->toggle_idx = -1;
    if (!dev) {
        return;
    }
    for (int i = 0; i < dev->cap_count; i++) {
        const odd_capability_t *c = &dev->caps[i].cap;
        if (!writable(c)) {
            continue;
        }
        const bool binary = c->min == 0 && c->max == 1;
        if (out->toggle_idx < 0 && (c->type == ODD_CAP_POWER || binary)) {
            out->toggle_idx = i;
        } else if (out->level_idx < 0 && c->type == ODD_CAP_LEVEL && c->max > c->min) {
            out->level_idx = i;
        }
    }
}
