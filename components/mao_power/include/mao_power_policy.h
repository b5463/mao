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
 *
 * Charging (mao_power, boards with caps.charge_control): the LiPo cell may
 * be charged at 0-45 C, the charger's pack-NTC window ends at 50 C. While
 * USB is present mao_power reads the board temperature (IMU die) every
 * ~10 s and mao_policy_charge_pause() decides, with hysteresis, whether the
 * charger is paused. Without a temperature it never pauses: the hardware
 * window stays the limit. mao_policy_charging() says whether the cell is
 * charging when the board has no charger status line.
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

/* ---- Charging ------------------------------------------------------------ */

#define MAO_CHARGE_PAUSE_C          43.0f   /* pause at or above (cell limit 45 C) */
#define MAO_CHARGE_RESUME_C         40.0f   /* resume at or below */
#define MAO_CHARGE_RATE_MIN_PCT_H   1.0f    /* gauge CRATE above this = the cell gains charge */

/* Should the charger be paused now? paused: what it is now. No USB: no
 * (the next plug-in starts from the hardware default). temp_valid false (no
 * sensor, a failed read): no, fail-safe to the hardware limit. */
bool mao_policy_charge_pause(bool usb_present, bool paused, bool temp_valid, float temp_c);

typedef struct {
    bool usb_present;
    bool charge_paused;            /* by mao_policy_charge_pause() */
    bool gauge_valid;              /* a fuel-gauge reading exists */
    float soc_pct;
    float rate_pct_per_h;          /* gauge CRATE, + = charging */
} mao_charge_input_t;

/* Charging, for a board without a charger status line: USB present, not
 * paused, and the gauge either sees the cell gain charge (CRATE above
 * MAO_CHARGE_RATE_MIN_PCT_H) or the cell is not full (SOC < 100 %). An
 * estimate: right after plug-in it says yes before CRATE has turned; a cell
 * the charger terminates below 100 % SOC, or a load above the USB input
 * limit, still reads as charging while SOC < 100 %. Without a gauge it is
 * USB present and not paused. */
bool mao_policy_charging(const mao_charge_input_t *in);

#ifdef __cplusplus
}
#endif
