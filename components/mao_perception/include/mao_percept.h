/*
 * MAO percepts: the interpreted-event vocabulary of the perception layer.
 *
 * A percept is what MAO has *noticed* ("someone came close", "I was picked
 * up"), never what a sensor read. Percepts are produced by mao_perception
 * from filtered, fused observations and travel on the event bus as
 * MAO_EVENT_PERCEPT with a packed value (percept, confidence, detail).
 * mao_app decides what MAO does about them.
 *
 * Plain C, no ESP-IDF dependency: the perception core and its host unit
 * tests use this header too.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_PERCEPT_NONE = 0,
    /* Something / someone in front of MAO (proximity, fused with motion). */
    MAO_PERCEPT_APPROACH_STARTED,      /* closing in from a distance */
    MAO_PERCEPT_APPROACH_NEAR,         /* close in front of the face */
    MAO_PERCEPT_WITHDRAWN,             /* went away again after being near */
    /* Body touch. detail = zone (MAO_PERCEPT_ZONE_*). */
    MAO_PERCEPT_TOUCH,                 /* a deliberate touch began (not a grip) */
    MAO_PERCEPT_TOUCH_HOLD,            /* touch held without being a pet */
    MAO_PERCEPT_TOUCH_REPEAT,          /* poked again and again */
    MAO_PERCEPT_GENTLE_PET,            /* calm top touch + low motion + quiet + hand near */
    /* Being handled (IMU, fused with grip touch). */
    MAO_PERCEPT_PICKED_UP,
    MAO_PERCEPT_PUT_DOWN,
    MAO_PERCEPT_HARD_PUT_DOWN,         /* detail 1 = it was dropped (free fall first) */
    MAO_PERCEPT_UPSIDE_DOWN,           /* detail 1 = turned over, 0 = upright again */
    MAO_PERCEPT_SHAKE,
    MAO_PERCEPT_NUDGED,                /* desk bumped / slid a little while resting */
    MAO_PERCEPT_KNOCK,                 /* tapped on the body while resting; detail = taps (1, 2) */
    /* Surroundings. State percepts: detail 1 = entered, 0 = left. */
    MAO_PERCEPT_QUIET_ROOM,
    MAO_PERCEPT_SUDDEN_NOISE,
    MAO_PERCEPT_COVERED,
    MAO_PERCEPT_DARK_ROOM,
    /* Body state. */
    MAO_PERCEPT_USB_CONNECTED,         /* detail 1 = plugged in, 0 = unplugged */
    MAO_PERCEPT_LOW_BATTERY,           /* detail 1 = low, 2 = critical */
    MAO_PERCEPT_REMOTE_SIGNAL,         /* someone else's IR frame; detail = command */
    /* History. */
    MAO_PERCEPT_LONG_ABSENCE,          /* nobody for a long time, now someone is back; detail = minutes */
    MAO_PERCEPT_FIDDLING_ESCALATION,   /* detail = level 1..3, 0 = calmed down again */
    MAO_PERCEPT_COUNT,
} mao_percept_t;

/* Touch zones as carried in detail (same order as mao_touch_zone_t). */
#define MAO_PERCEPT_ZONE_RIGHT   0
#define MAO_PERCEPT_ZONE_LEFT    1
#define MAO_PERCEPT_ZONE_TOP     2
#define MAO_PERCEPT_ZONE_REAR    3
#define MAO_PERCEPT_ZONE_COUNT   4

#define MAO_PERCEPT_DETAIL_MAX   0x7FFF

typedef struct {
    mao_percept_t percept;
    uint8_t confidence;      /* 0..100 */
    uint16_t detail;         /* 0..MAO_PERCEPT_DETAIL_MAX, meaning per percept */
} mao_percept_msg_t;

/* Event-bus value: bits 0-7 percept, 8-14 confidence, 16-30 detail.
 * Always non-negative. */
static inline int32_t mao_percept_pack(mao_percept_t p, uint8_t confidence, uint16_t detail)
{
    if (confidence > 100) {
        confidence = 100;
    }
    if (detail > MAO_PERCEPT_DETAIL_MAX) {
        detail = MAO_PERCEPT_DETAIL_MAX;
    }
    return (int32_t)(((uint32_t)p & 0xFFu) | ((uint32_t)confidence << 8) | ((uint32_t)detail << 16));
}

static inline mao_percept_msg_t mao_percept_unpack(int32_t value)
{
    const uint32_t v = (uint32_t)value;
    mao_percept_msg_t m;
    m.percept = (mao_percept_t)(v & 0xFFu);
    m.confidence = (uint8_t)((v >> 8) & 0x7Fu);
    m.detail = (uint16_t)((v >> 16) & 0x7FFFu);
    if (m.percept >= MAO_PERCEPT_COUNT) {
        m.percept = MAO_PERCEPT_NONE;
    }
    return m;
}

const char *mao_percept_name(mao_percept_t p);
const char *mao_percept_zone_name(uint16_t zone);

#ifdef __cplusplus
}
#endif
