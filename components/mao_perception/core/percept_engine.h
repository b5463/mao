/*
 * MAO perception engine: the platform-independent core of mao_perception.
 *
 *   filtered observations (pe_feed_*) --> engine state --> percepts (pe_pop)
 *
 * Plain C99 + libm, no ESP-IDF, no RTOS, no clock of its own: every input
 * carries its time in ms (uint32, wrap-safe), and pe_tick() advances time
 * between inputs. That keeps the whole engine deterministic and unit-testable
 * on a host (tests/host).
 *
 * What it does with the data (brief sections 29/30):
 *   smoothing     EMA filters on gravity, motion energy, distance, light, sound
 *   debounce      touches, USB, proximity zones need consecutive agreement
 *   hysteresis    separate enter / leave thresholds for every state percept
 *   windows       recent touches, shakes, dial reversals, noise vs knock
 *   fusion        GENTLE_PET, COVERED, PICKED_UP, KNOCK, FIDDLING_ESCALATION
 *                 combine several senses, each adding evidence
 *   confidence    0..100 per percept, from how much evidence agreed
 *   cooldowns     per percept, plus a global rate limit (no sensor spam)
 *   self-gating   MAO's own sounds and haptics are not mistaken for the world
 *   history       presence / absence, lingering annoyance (residue)
 * A missing sensor is simply never fed; its evidence counts as neutral.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mao_percept.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Which senses exist (pe_init): fusion weighs only what can be known. */
#define PE_SENSE_IMU      (1u << 0)
#define PE_SENSE_TOF      (1u << 1)
#define PE_SENSE_ALS      (1u << 2)
#define PE_SENSE_TOUCH    (1u << 3)
#define PE_SENSE_MIC      (1u << 4)

/* IMU event flags (same bits as mao_sense MAO_IMU_EV_*). */
#define PE_IMU_ACTIVITY   (1u << 0)
#define PE_IMU_TAP        (1u << 1)
#define PE_IMU_DOUBLE_TAP (1u << 2)
#define PE_IMU_FREE_FALL  (1u << 4)

typedef enum {
    PE_POWER_ACTIVE = 0,
    PE_POWER_IDLE,
    PE_POWER_DROWSY,
} pe_power_t;

/* Tuning. pe_config_default() gives the values documented in
 * docs/firmware/mao-a0-firmware.md; the ESP glue overrides a few from Kconfig. */
typedef struct {
    /* motion (accel in g) */
    float upright_sign;            /* +1: sensor +Z points up when MAO sits on its base (VERIFY) */
    float still_g;                 /* motion energy below this = still */
    float move_g;                  /* above this = moving */
    float shake_g;                 /* a single "strong" sample for shake counting */
    float shock_g;                 /* |a| deviation that marks a hard landing */
    uint32_t pickup_ms;            /* sustained motion before PICKED_UP */
    uint32_t settle_ms;            /* stillness that ends being held */
    /* proximity (mm) */
    uint16_t near_mm;              /* enter NEAR */
    uint16_t near_exit_mm;         /* leave NEAR (hysteresis) */
    uint16_t far_mm;               /* closer than this = something is there */
    uint16_t cover_mm;             /* closer than this = possibly covered */
    float approach_mm_s;           /* closing speed for APPROACH_STARTED */
    /* light (lux) */
    float dark_lux;
    float light_lux;
    uint32_t dark_ms;              /* sustained darkness before DARK_ROOM */
    /* touch */
    uint32_t touch_debounce_ms;
    uint32_t hold_ms;
    uint32_t pet_ms;
    /* sound (dBFS) */
    float quiet_dbfs;              /* ambient below this counts as quiet */
    uint32_t quiet_ms;             /* sustained quiet before QUIET_ROOM */
    float noise_rise_db;           /* SUDDEN_NOISE: jump above ambient */
    float noise_min_dbfs;          /* SUDDEN_NOISE: absolute minimum level */
    /* history */
    uint32_t long_absence_ms;
    uint32_t usb_debounce_ms;
    uint8_t max_percepts_per_s;    /* global rate limit for event-type percepts */
} pe_config_t;

typedef struct {
    uint32_t t_ms;
    float ax, ay, az;              /* g */
    bool accel_valid;              /* false for event-only observations */
    uint8_t events;                /* PE_IMU_* */
} pe_imu_t;

typedef struct {
    uint32_t t_ms;
    uint16_t distance_mm;
    uint8_t status;                /* VL53L4CD range status, 0 = valid */
    bool threshold;                /* low-power approach threshold fired */
} pe_tof_t;

typedef struct {
    uint32_t t_ms;
    uint8_t touched;               /* bit per zone */
} pe_touch_t;

typedef struct {
    uint32_t t_ms;
    float rms_dbfs;
    bool onset;
} pe_mic_t;

typedef struct {
    mao_percept_t percept;
    uint8_t confidence;
    uint16_t detail;
    uint32_t t_ms;
} pe_out_t;

#define PE_OUT_QUEUE      16
#define PE_TOUCH_HISTORY  8
#define PE_RATE_HISTORY   8
#define PE_TOF_MEDIAN     3

typedef struct {
    pe_config_t cfg;
    uint32_t senses;
    uint32_t now;
    uint32_t last_tick;
    pe_power_t power;

    pe_out_t out[PE_OUT_QUEUE];
    uint8_t out_head;
    uint8_t out_count;
    uint32_t dropped;                  /* lost to the rate limit or a full queue */
    uint32_t last_emit[MAO_PERCEPT_COUNT];
    bool emitted[MAO_PERCEPT_COUNT];
    uint32_t rate_t[PE_RATE_HISTORY];
    uint8_t rate_head;

    /* self-stimulation gates: MAO's own sound / vibration */
    uint32_t self_sound_until;
    uint32_t self_motion_until;

    /* presence history */
    uint32_t last_presence;
    uint32_t absence_carry_ms;         /* absence before this boot (deep sleep) */

    struct {
        bool have;
        uint32_t last_t;
        float gx, gy, gz;              /* gravity estimate (low-passed accel) */
        float rest_gx, rest_gy, rest_gz;   /* gravity when last resting */
        float px, py, pz;              /* previous accel sample */
        float energy;                  /* EMA of |a - gravity| */
        float last_dyn;
        uint32_t last_motion_t;        /* last sample with real motion */
        uint32_t still_since;          /* 0 = not still */
        uint32_t moving_since;         /* 0 = not moving */
        bool was_resting;              /* resting when the current motion began */
        bool held;
        uint32_t held_since;
        float pickup_tilt;
        uint32_t last_shock;
        uint32_t last_freefall;
        uint32_t last_tap;
        uint8_t last_tap_kind;
        uint32_t last_putdown;
        uint32_t shake_t[6];
        uint8_t shake_n;
        bool upside;
        uint32_t upside_cand;          /* enter / leave candidate since */
        uint32_t nudge_start;          /* short motion from rest, 0 = none */
        float nudge_peak;
        bool nudge_tap;
    } m;

    struct {
        bool have;
        uint32_t last_t;
        uint32_t last_valid_t;
        uint16_t hist[PE_TOF_MEDIAN];
        uint8_t hist_n;
        float d;                       /* filtered distance (mm) */
        float v;                       /* mm/s, negative = closing */
        bool d_ok;
        uint8_t near_votes;
        uint8_t closing_votes;
        bool near;
        uint32_t near_since;
        uint32_t leave_since;          /* NEAR candidate to leave, 0 = none */
        bool episode;                  /* something has been in range */
        bool was_near;                 /* ... and came near during this episode */
        bool started_sent;
        float episode_far_d;
        uint32_t gone_since;           /* nothing in range since, 0 = something there */
    } p;

    struct {
        bool have;
        uint32_t last_t;
        float fast;                    /* lux, smoothed */
        float ref;                     /* recent bright level */
        bool dark;
        uint32_t cand_since;
    } l;

    struct {
        uint8_t raw;                   /* last observed mask */
        uint8_t stable;                /* debounced mask */
        uint32_t raw_since[MAO_PERCEPT_ZONE_COUNT];
        uint32_t down_since[MAO_PERCEPT_ZONE_COUNT];
        bool hold_done[MAO_PERCEPT_ZONE_COUNT];
        bool grip;
        uint32_t grip_since;
        int8_t pending_zone;           /* side touch waiting for a possible grip, -1 = none */
        uint32_t pending_t;
        uint32_t starts_t[PE_TOUCH_HISTORY];
        uint8_t starts_zone[PE_TOUCH_HISTORY];
        uint8_t starts_head;
        uint32_t strokes_t[3];
        uint8_t strokes_n;
        bool pet_done;
    } t;

    struct {
        bool have;
        uint32_t last_t;
        float ambient;                 /* room level, dBFS */
        bool quiet;
        uint32_t quiet_cand;
        uint32_t last_onset;
        uint32_t pending_noise;        /* onset waiting for the knock window */
        float pending_rise;
    } a;

    struct {
        bool covered;
        uint32_t cand_since;
        uint32_t clear_since;
        uint32_t last_uncover;
    } c;

    struct {
        float dial, touch, motion, press, residue;   /* fiddling evidence (points) */
        uint8_t level;
        uint8_t peak;
        uint32_t last_level_up;
        float dial_energy;
        int8_t last_dir;
        uint32_t last_dial_t;
        uint32_t press_t[4];
        uint8_t press_head;
        bool residue_armed;
    } f;

    struct {
        bool present;
        bool pending;
        bool pending_value;
        uint32_t pending_since;
    } usb;

    uint8_t battery_level;
    uint32_t battery_reported_t;
} pe_engine_t;

void pe_config_default(pe_config_t *cfg);

/* senses: PE_SENSE_* mask of the inputs that will be fed. */
void pe_init(pe_engine_t *e, const pe_config_t *cfg, uint32_t senses, uint32_t now_ms);

/* Initial conditions that are facts, not news (no percept). */
void pe_set_usb_initial(pe_engine_t *e, bool present);
/* Time nobody was around before now (e.g. a long deep sleep). */
void pe_set_absence(pe_engine_t *e, uint32_t absent_ms);
void pe_set_power(pe_engine_t *e, pe_power_t power);

/* Observations. */
void pe_feed_imu(pe_engine_t *e, const pe_imu_t *imu);
void pe_feed_tof(pe_engine_t *e, const pe_tof_t *tof);
void pe_feed_als(pe_engine_t *e, uint32_t t_ms, float lux);
void pe_feed_touch(pe_engine_t *e, const pe_touch_t *touch);
void pe_feed_mic(pe_engine_t *e, const pe_mic_t *mic);

/* Input and body events. */
void pe_feed_dial(pe_engine_t *e, uint32_t t_ms, int32_t detents);
void pe_feed_press(pe_engine_t *e, uint32_t t_ms);
void pe_feed_usb(pe_engine_t *e, uint32_t t_ms, bool present);
void pe_feed_battery(pe_engine_t *e, uint32_t t_ms, uint8_t level);   /* 1 low, 2 critical */
void pe_feed_ir(pe_engine_t *e, uint32_t t_ms, uint8_t command, bool repeat);

/* MAO itself makes a sound / vibrates until until_ms: ignore what that
 * does to the mic / IMU. */
void pe_note_self_sound(pe_engine_t *e, uint32_t until_ms);
void pe_note_self_motion(pe_engine_t *e, uint32_t until_ms);

/* Advance time: timeouts, decays, pending decisions. Call every ~50-100 ms. */
void pe_tick(pe_engine_t *e, uint32_t now_ms);

/* Next percept, oldest first. */
bool pe_pop(pe_engine_t *e, pe_out_t *out);

/* Introspection (diagnostics, tests). */
uint8_t pe_fiddle_level(const pe_engine_t *e);
float pe_fiddle_score(const pe_engine_t *e);
bool pe_is_held(const pe_engine_t *e);
bool pe_is_near(const pe_engine_t *e);
float pe_ambient_dbfs(const pe_engine_t *e);
uint32_t pe_absence_ms(const pe_engine_t *e, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
