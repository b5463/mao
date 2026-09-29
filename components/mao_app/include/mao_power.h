/*
 * MAO's power ladder (M4.1): when to rest, how deeply, and what the first
 * touch after resting means. Pure (host-tested: tests/power); mao_app_power.c
 * does the hardware.
 *
 *   ACTIVE         normal MAO
 *   SLEEP_DISPLAY  after MAO_PWR_SLEEP_MS without input: the sleeping eyes
 *                  and the ODD JOBS maker's mark in lavender, the screen at
 *                  MAO_PWR_SLEEP_PCT, then the chip in LIGHT SLEEP - the
 *                  encoder (any GPIO) wakes it
 *   NIGHT          after MAO_PWR_NIGHT_MS: still light sleep, the backlight
 *                  off and the panel asleep - the deepest state the current
 *                  board can wake from with the knob
 *   (TRUE DEEP SLEEP is not on this ladder: on the ESP32-C3-LCDkit the
 *    encoder (IO6 / IO9 / IO10) is not an RTC IO (only GPIO0-5 can wake the
 *    C3 from deep sleep), so the knob could not wake it. It is DEV-only and
 *    explicit, with a timer or reset wake - see mao_app_power.c.)
 *
 * Nothing on the ladder is entered while MAO is busy with something that
 * needs it awake (a pairing, a FORGET being committed, an action waiting for
 * its result, a transfer): user inactivity is not system idle.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAO_PWR_SLEEP_MS   (10u * 60u * 1000u)
#define MAO_PWR_NIGHT_MS   (30u * 60u * 1000u)
#define MAO_PWR_SLEEP_PCT  3          /* the sleeping screen's backlight */
#define MAO_PWR_WAKE_QUIET_MS 350u    /* after a knob wake, input this soon only belongs to the waking touch */

typedef enum {
    MAO_PWR_ACTIVE = 0,
    MAO_PWR_SLEEP_DISPLAY,
    MAO_PWR_NIGHT,
} mao_pwr_state_t;

/* What must keep MAO awake whatever the idle time. */
typedef struct {
    bool pairing;          /* a ceremony (or its SAS question) is open */
    bool forgetting;       /* a FORGET is being committed (revocation in flight) */
    bool action_pending;   /* a device action awaits its result */
    bool transfer;         /* CONNECT / AWAY */
} mao_pwr_busy_t;

typedef enum {
    MAO_PWR_DO_NOTHING = 0,
    MAO_PWR_DO_SLEEP_DISPLAY,   /* compose the sleeping screen, then light sleep */
    MAO_PWR_DO_NIGHT,           /* backlight off, panel asleep; stay in light sleep */
    MAO_PWR_DO_WAKE,            /* back to ACTIVE */
} mao_pwr_do_t;

typedef struct {
    mao_pwr_state_t st;
} mao_power_t;

void mao_power_init(mao_power_t *p);
bool mao_power_busy(const mao_pwr_busy_t *b);

/* The idle clock moved (any time; idle_ms = time since the last input).
 * Returns the step to take, and advances the state when it does one. */
mao_pwr_do_t mao_power_idle(mao_power_t *p, uint32_t idle_ms, const mao_pwr_busy_t *busy);

/* Input arrived (or the knob woke the chip): back to ACTIVE if resting. */
mao_pwr_do_t mao_power_input(mao_power_t *p);

/* May the DEV-only true deep sleep be entered now? (never while busy) */
bool mao_power_deep_allowed(const mao_pwr_busy_t *busy);

/* ------------------------------------------------------------------ */
/* The first touch after resting (light-sleep knob wake, or a dimmed   */
/* MAO): it only wakes. A press is eaten whole (held, turned while     */
/* held, released, and the click that comes with the release); a turn */
/* is eaten; input within MAO_PWR_WAKE_QUIET_MS of a knob wake belongs */
/* to that waking touch.                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    MAO_IN_TURN = 0,
    MAO_IN_PRESS,
    MAO_IN_RELEASE,
    MAO_IN_CLICK,
    MAO_IN_LONG,
    MAO_IN_DOUBLE,
} mao_in_t;

typedef struct {
    uint8_t eat;           /* 0 none; 1 the waking press is down; 2 released (its click follows) */
    uint32_t release_ms;
    uint32_t quiet_until;  /* 0 = none */
    bool armed_quiet;
} mao_wake_eat_t;

#define MAO_WAKE_CLICK_MS 150u   /* a click belongs to that release only if it comes with it */

void mao_wake_eat_reset(mao_wake_eat_t *w);
/* The knob woke the chip from light sleep; `switch_down` = the button is
 * still held at wake. */
void mao_wake_eat_knob(mao_wake_eat_t *w, bool switch_down, uint32_t now_ms);
/* An input event: true when it must be eaten. `dimmed` = MAO was resting
 * when it arrived (the dimmed-screen case, without a chip wake). */
bool mao_wake_eat(mao_wake_eat_t *w, mao_in_t ev, bool dimmed, uint32_t now_ms);

/* ------------------------------------------------------------------ */
/* Cold boot, or MAO waking from its own (DEV) deep sleep?             */
/* ------------------------------------------------------------------ */

#define MAO_PWR_RTC_MAGIC 0x4D414F5Au   /* "MAOZ": written just before deep sleep, in RTC memory */

typedef enum {
    MAO_BOOT_COLD = 0,       /* power on, reset, flash: the full boot with the maker's mark */
    MAO_BOOT_FROM_DEEP,      /* MAO's own deep sleep ended: the short wake */
} mao_boot_kind_t;

/* woke_from_sleep: esp_sleep_get_wakeup_cause() was a sleep wake (timer,
 * GPIO...), not undefined; marker: the RTC word. */
mao_boot_kind_t mao_power_boot_kind(bool woke_from_sleep, uint32_t marker);
