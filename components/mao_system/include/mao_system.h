/*
 * MAO system core: boot banner, subsystem status reporting, event dispatcher
 * task and periodic health logging.
 */
#pragma once

#include "esp_err.h"
#include "mao_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_FIRMWARE_STAGE "M2"

/* Values carried by MAO_EVENT_DEV_COMMAND. */
typedef enum {
    MAO_DEVCMD_STATUS = 1,
    MAO_DEVCMD_SNAP,                 /* stream a screen snapshot */
    MAO_DEVCMD_REPLAY_BOOT,          /* replay the HOME boot sequence */
    MAO_DEVCMD_ODD_RESET,            /* reset ODD latency statistics */
    MAO_DEVCMD_PERF_BURST,           /* record frame intervals for 3 s (cadence check) */
    MAO_DEVCMD_ODD_INCARNATION,      /* log this boot's controller incarnation */
    MAO_DEVCMD_STALE_SET,            /* send one SET with a foreign incarnation (must be refused) */
    MAO_DEVCMD_ODD_SELFTEST,         /* ODD BUS codec self-test */
    MAO_DEVCMD_FLOOD_BASE = 2000,    /* value - base = seconds of 50 Hz broadcast traffic */
    MAO_DEVCMD_STRESS_BASE = 1000,   /* value - base = duration in seconds */
    MAO_DEVCMD_ANIM_BASE = 3000,     /* + mao_character_preview_t */
    MAO_DEVCMD_VIEW_BASE = 4000,     /* + mao_view_t */
    MAO_DEVCMD_LOOK_BASE = 5000,     /* + look preset index */
    MAO_DEVCMD_REACT_BASE = 7000,    /* + mao_character_reaction_t; base + 999 = list */
    MAO_DEVCMD_TRANSFER_BASE = 8000, /* + mao_transfer_cmd_t */
    MAO_DEVCMD_INTEREST_BASE = 9000, /* + 0..100: force the mind's interest */
    MAO_DEVCMD_NOVELTY_BASE = 9200,  /* + 0..100: force device novelty */
    MAO_DEVCMD_EXPR_BASE = 6000,     /* + expression state index; base + 999 = list */
    MAO_DEVCMD_DIAL_BASE = 100000,   /* + (dps + 500) * 1000 + seconds */
    MAO_DEVCMD_SEQSEED_BASE = 300000, /* + n: force the next ODD sequence number (tests) */
} mao_devcmd_t;

/* MAO_DEVCMD_TRANSFER_BASE offsets ("mao transfer <...>"). */
typedef enum {
    MAO_TRANSFER_CMD_LEFT = 0,    /* real connect towards a logical edge */
    MAO_TRANSFER_CMD_RIGHT,
    MAO_TRANSFER_CMD_UP,
    MAO_TRANSFER_CMD_DOWN,
    MAO_TRANSFER_CMD_SUCCESS,     /* synthetic: exit + away (animation iteration) */
    MAO_TRANSFER_CMD_FAIL,        /* synthetic: failure escape */
    MAO_TRANSFER_CMD_RETURN,
    MAO_TRANSFER_CMD_ABORT,
} mao_transfer_cmd_t;

/* Print banner and chip/boot info, create the event bus, load settings.
 * Call first. */
esp_err_t mao_system_init(void);

/* Start the development console task (no-op unless CONFIG_MAO_DEV_CONSOLE).
 * Called by mao_system_start(). */
esp_err_t mao_devcmd_start(void);

/* Arm / re-arm the idle timer: MAO_EVENT_IDLE_TIMEOUT is posted after
 * timeout_ms without another call. 0 disarms. */
void mao_system_idle_kick(uint32_t timeout_ms);

/* Record the outcome of a subsystem init and print "[OK] name" / "[!!] name".
 * Returns err unchanged so it can be chained. */
esp_err_t mao_system_report(const char *name, esp_err_t err);

/* Print "[--] name <reason>" for a subsystem intentionally left disabled. */
void mao_system_report_disabled(const char *name, const char *reason);

/* Log current heap figures with a short label. */
void mao_system_log_heap(const char *tag, const char *label);

/* Start the dispatcher task, post MAO_EVENT_SYSTEM_READY, print MAO READY. */
esp_err_t mao_system_start(void);

#ifdef __cplusplus
}
#endif
