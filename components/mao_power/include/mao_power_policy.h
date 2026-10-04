/*
 * MAO power policy: when MAO should be ACTIVE, IDLE, DROWSY or in DEEP
 * SLEEP (brief section 31). Pure decision logic, no ESP-IDF: mao_app asks
 * mao_policy_decide() once a second, mao_power asks
 * mao_policy_drowsy_to_deep() while it light-sleeps. Host-tested in
 * tests/host.
 *
 *   activity --idle_after--> IDLE --drowsy_after--> DROWSY --deep_after--> DEEP
 *
 * "Activity" is anything that shows someone is with MAO: input and the
 * presence percepts (touch, approach, pick-up, ...). Caps:
 *   - console attached (a USB host is talking to the USB-Serial/JTAG
 *     port): never below IDLE unless sleep_with_console, because light
 *     and deep sleep drop the USB connection;
 *   - USB power present: no deep sleep unless deep_on_usb;
 *   - hold_awake (first encounter, self-test, an open device view, ...):
 *     stays ACTIVE.
 * After a wake from DROWSY that nobody follows up with real activity
 * (woke_idle), MAO stays ACTIVE for redrowse_ms (so it can notice who or
 * what woke it) and then dozes off again, instead of waiting the full
 * drowsy_after.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Same order as mao_power_state_t. */
typedef enum {
    MAO_POLICY_ACTIVE = 0,
    MAO_POLICY_IDLE,
    MAO_POLICY_DROWSY,
    MAO_POLICY_DEEP_SLEEP,
} mao_policy_state_t;

typedef struct mao_policy_config {
    bool enabled;                  /* automatic power states at all */
    uint32_t idle_after_ms;
    uint32_t drowsy_after_ms;
    uint32_t deep_after_ms;        /* time spent DROWSY before DEEP SLEEP; 0 = never */
    uint32_t redrowse_ms;
    bool sleep_with_console;
    bool deep_on_usb;
} mao_policy_config_t;

typedef struct {
    uint32_t now_ms;
    uint32_t last_activity_ms;
    uint32_t last_wake_ms;         /* when MAO last woke from DROWSY */
    bool woke_idle;                /* ... and nothing has happened since */
    bool usb_present;
    bool console_attached;
    bool hold_awake;
} mao_policy_input_t;

/* ACTIVE, IDLE or DROWSY for an awake MAO (DEEP SLEEP is entered from
 * DROWSY, see below). */
mao_policy_state_t mao_policy_decide(const mao_policy_config_t *cfg, const mao_policy_input_t *in);

/* While DROWSY for drowsy_ms: go on to deep sleep now? */
bool mao_policy_drowsy_to_deep(const mao_policy_config_t *cfg, uint32_t drowsy_ms, bool usb_present,
                               bool console_attached);

const char *mao_policy_state_name(mao_policy_state_t state);

#ifdef __cplusplus
}
#endif
