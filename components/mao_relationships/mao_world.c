/*
 * The merged device world (see mao_world.h). Reads RAM only: the relationship
 * table and registry snapshots - never flash, so it is cheap enough to run on
 * every UI refresh.
 */
#include "mao_world.h"

#include <string.h>
#include "mao_devices.h"
#include "mao_rel.h"

static void from_live(mao_world_entry_t *e, const mao_device_t *d, int slot)
{
    memcpy(e->name, d->info.name, sizeof(e->name));
    e->device_type = d->info.device_type;
    e->online = d->online;
    e->slot = slot;
}

bool mao_world_get(uint64_t id, mao_world_entry_t *out)
{
    mao_rel_info_t r;
    const bool known = mao_rel_get(id, &r);
    const int slot = mao_devices_find(id);
    mao_device_t d;
    const bool live = slot >= 0 && mao_devices_get(slot, &d);
    if (!known && !live) {
        return false;
    }
    *out = (mao_world_entry_t) { .id = id, .known = known, .slot = -1 };
    if (known) {
        memcpy(out->name, r.name, sizeof(out->name));
        out->device_type = r.device_type;
    }
    if (live) {
        from_live(out, &d, slot);   /* live ANNOUNCE is authoritative */
    }
    return true;
}

int mao_world_list(mao_world_entry_t out[MAO_WORLD_MAX])
{
    int n = 0;
    mao_rel_info_t known[MAO_REL_MAX_KNOWN];
    const int k = mao_rel_list(known);
    for (int i = 0; i < k && n < MAO_WORLD_MAX; i++) {
        if (mao_world_get(known[i].id, &out[n])) {
            n++;
        }
    }
    /* Registry slots are assigned in first-heard order and never reused,
     * so slot order is this session's stable first-seen order. */
    for (int slot = 0; slot < MAO_DEVICES_MAX && n < MAO_WORLD_MAX; slot++) {
        mao_device_t d;
        if (!mao_devices_get(slot, &d) || !d.online || mao_rel_is_known(d.info.id)) {
            continue;
        }
        out[n] = (mao_world_entry_t) { .id = d.info.id, .known = false };
        from_live(&out[n], &d, slot);
        n++;
    }
    return n;
}
