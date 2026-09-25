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
    out->action_count = 0;
    out->primary_action = -1;
    out->ready_idx = -1;
    out->storage_idx = -1;
    for (int i = 0; i < MAO_CONTROLS_MAX_ACTIONS; i++) {
        out->action_idx[i] = -1;
        out->action_sem[i] = 0;
    }
    if (!dev) {
        return;
    }
    for (int i = 0; i < dev->cap_count; i++) {
        const odd_capability_t *c = &dev->caps[i].cap;
        /* Read-only facts are status, never controls. */
        if (c->type == ODD_CAP_READY) {
            if (out->ready_idx < 0) {
                out->ready_idx = i;
            }
            continue;
        }
        if (c->type == ODD_CAP_STORAGE) {
            if (out->storage_idx < 0) {
                out->storage_idx = i;
            }
            continue;
        }
        if (!writable(c)) {
            continue;
        }
        if (c->type == ODD_CAP_ACTION) {
            if (out->action_count < MAO_CONTROLS_MAX_ACTIONS) {
                out->action_idx[out->action_count] = i;
                out->action_sem[out->action_count] = odd_action_semantic_of(c);
                /* Semantic priority, not product knowledge: a device's
                 * primary operation (CAPTURE today) leads its page. */
                if (out->primary_action < 0 && odd_action_semantic_of(c) == ODD_ACTION_CAPTURE) {
                    out->primary_action = out->action_count;
                }
                out->action_count++;
            }
            continue;   /* an action is never a toggle or a level */
        }
        const bool binary = c->min == 0 && c->max == 1;
        if (out->toggle_idx < 0 && (c->type == ODD_CAP_POWER || binary)) {
            out->toggle_idx = i;
        } else if (out->level_idx < 0 && c->type == ODD_CAP_LEVEL && c->max > c->min) {
            out->level_idx = i;
        }
    }
}
