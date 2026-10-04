/*
 * MAO perception: turns sensor observations and input / body events into
 * percepts (what MAO noticed) for the application.
 *
 *   mao_sense observations ─┐
 *   input, power events ────┼─> mao_perception task ─> percept engine
 *   IR frames (not echoes) ─┘      (core/, plain C)        │
 *                                                          v
 *                             MAO_EVENT_PERCEPT on the event bus ─> mao_app
 *
 * The engine (core/percept_engine.c) is platform-independent and host-tested
 * (tests/host). This component is the ESP-IDF glue: subscriptions, one task,
 * the self-stimulation gates (MAO's own sound and haptics), Kconfig tuning,
 * deep-sleep continuity of "how long has nobody been here".
 *
 * Boards without sensors (LCDkit) still run it: the dial and the press
 * yield FIDDLING_ESCALATION and LONG_ABSENCE; nothing else is invented.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "mao_events.h"
#include "mao_percept.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t senses;           /* PE_SENSE_* fed to the engine */
    uint8_t fiddle_level;      /* 0..3 */
    float fiddle_score;
    bool held;
    bool near;
    float ambient_dbfs;
    uint32_t absent_s;         /* since anyone was last around */
    uint32_t posted;           /* percepts posted since boot */
    uint32_t dropped;          /* engine rate limit + full queues */
} mao_perception_status_t;

/* Subscribe to mao_sense, the event bus and IR, start the task. Call after
 * the drivers and before mao_system_start() (bus subscriptions are static). */
esp_err_t mao_perception_init(void);

void mao_perception_get_status(mao_perception_status_t *out);

/* MAO_EVENT_PERCEPT -> message; false for any other event. */
static inline bool mao_percept_from_event(const mao_event_t *ev, mao_percept_msg_t *out)
{
    if (!ev || ev->type != MAO_EVENT_PERCEPT) {
        return false;
    }
    *out = mao_percept_unpack(ev->value);
    return out->percept != MAO_PERCEPT_NONE;
}

#ifdef __cplusplus
}
#endif
