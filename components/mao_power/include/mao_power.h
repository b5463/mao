/*
 * MAO power: battery, charger and USB status, power events, and the power
 * state manager.
 *
 *   MAX17048 fuel gauge + BQ24073 charger lines -> mao_power task
 *       -> MAO events: USB_CONNECTED / USB_DISCONNECTED, CHARGING_STARTED /
 *          CHARGING_DONE, BATTERY_LOW, BATTERY_CRITICAL, POWER_STATE
 *
 * Power states, from most to least awake:
 *   ACTIVE      everything running.
 *   IDLE        awake; subsystems lower their rates.
 *   DROWSY      panel asleep, mic stopped, amplifier / haptics off, ToF in
 *               threshold mode; the CPU light-sleeps until a wake line,
 *               touch or proximity fires (then back to ACTIVE).
 *   DEEP_SLEEP  rails off, dial sensors slow, IMU wake-on-motion armed; the
 *               chip restarts on press, motion, USB, expander alert, top
 *               touch or the housekeeping timer.
 * Components prepare for a state in a hook (mao_power_register_hook). The
 * application decides when to change state; nothing here does so on its own
 * except the critical-battery shutdown.
 *
 * Continuity: a small RTC-memory record survives deep sleep, so after a
 * wake-up mao_power_continuity() tells how long MAO slept, why, and what
 * woke it.
 *
 * Boards without battery / sleep wiring: mao_power_init() returns
 * ESP_ERR_NOT_SUPPORTED, state changes are refused, hooks never run.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_POWER_ACTIVE = 0,
    MAO_POWER_IDLE,
    MAO_POWER_DROWSY,
    MAO_POWER_DEEP_SLEEP,
    MAO_POWER_STATE_COUNT,
} mao_power_state_t;

typedef enum {
    MAO_SLEEP_REASON_NONE = 0,
    MAO_SLEEP_REASON_REQUEST,     /* asked for through the API */
    MAO_SLEEP_REASON_DEV,         /* development console */
    MAO_SLEEP_REASON_BATTERY,     /* battery critical: only USB can wake */
    MAO_SLEEP_REASON_IDLE,        /* power policy: nobody around for a long time */
} mao_sleep_reason_t;

typedef enum {
    MAO_WAKE_NONE = 0,            /* not a wake-up (power-on, reset) */
    MAO_WAKE_PRESS,
    MAO_WAKE_MOTION,
    MAO_WAKE_USB,
    MAO_WAKE_EXPANDER,            /* charger status / gauge or light alert */
    MAO_WAKE_TOUCH,
    MAO_WAKE_PROXIMITY,
    MAO_WAKE_TIMER,
    MAO_WAKE_OTHER,
} mao_wake_source_t;

typedef struct {
    bool gauge;                   /* fuel gauge answered */
    uint16_t voltage_mv;          /* cell voltage */
    float soc_pct;                /* state of charge */
    float rate_pct_per_h;         /* + charging, - discharging */
    bool usb_present;
    bool charging;
    bool low;
    bool critical;
    uint32_t updated_ms;          /* ms since boot of the last gauge reading */
} mao_power_status_t;

typedef struct {
    bool woke_from_sleep;         /* this boot ended a MAO deep sleep */
    mao_sleep_reason_t reason;    /* why it went to sleep */
    mao_wake_source_t source;     /* what woke it */
    uint64_t slept_ms;            /* time asleep, by the RTC clock */
    uint32_t sleep_count;         /* deep sleeps since power-on */
} mao_power_continuity_t;

typedef struct {
    mao_power_state_t from;
    mao_power_state_t to;
    mao_sleep_reason_t reason;    /* set when to == MAO_POWER_DEEP_SLEEP */
} mao_power_transition_t;

/* Called in the mao_power task, in registration order, before a state is
 * entered (and when leaving DROWSY). Must finish quickly and must not ask for
 * another state change. */
typedef void (*mao_power_hook_t)(const mao_power_transition_t *t, void *ctx);

/* May be called before mao_power_init() (hooks are kept in a static table). */
esp_err_t mao_power_register_hook(const char *name, mao_power_hook_t hook, void *ctx);

/* Gauge + charger + USB monitoring, events, state manager task. */
esp_err_t mao_power_init(void);

void mao_power_get_status(mao_power_status_t *out);
mao_power_state_t mao_power_get_state(void);

/* Change to ACTIVE, IDLE or DROWSY. Asynchronous: the mao_power task runs
 * the hooks and then posts MAO_EVENT_POWER_STATE. */
esp_err_t mao_power_request_state(mao_power_state_t state);

/* Enter deep sleep (asynchronous). wake_after_s: timer wake-up, 0 = the
 * Kconfig housekeeping interval. Does not return to the caller's world:
 * the next thing that runs is a fresh boot. */
esp_err_t mao_power_deep_sleep(uint32_t wake_after_s, mao_sleep_reason_t reason);

/* Light-sleep test: DROWSY for at most duration_s (0 = until woken). */
esp_err_t mao_power_drowsy_for(uint32_t duration_s);

const mao_power_continuity_t *mao_power_continuity(void);

/* What ended the last DROWSY (light sleep), MAO_WAKE_NONE if none yet. */
mao_wake_source_t mao_power_last_wake(void);

/* The power policy settings from Kconfig ("MAO power"), shared by mao_app
 * (ACTIVE / IDLE / DROWSY) and this component (DROWSY -> DEEP SLEEP).
 * enabled is false on boards without power management. */
struct mao_policy_config;
void mao_power_policy_config(struct mao_policy_config *out);

/* Read the fuel gauge now (normally every 30 s). */
esp_err_t mao_power_refresh(void);

/* Diagnostics: the fuel gauge's VERSION register (MAX17048: 0x001x). */
esp_err_t mao_power_gauge_version(uint16_t *version);

const char *mao_power_state_name(mao_power_state_t state);
const char *mao_wake_source_name(mao_wake_source_t source);

#ifdef __cplusplus
}
#endif
