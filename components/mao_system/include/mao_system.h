/*
 * MAO system core: boot banner, subsystem status reporting, event dispatcher
 * task and periodic health logging.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mao_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAO_FIRMWARE_STAGE "M2"

/* Values carried by MAO_EVENT_DEV_COMMAND. */
typedef enum {
    MAO_DEVCMD_STATUS = 1,
    MAO_DEVCMD_ODD_RESET,            /* reset ODD latency statistics */
    MAO_DEVCMD_ODD_SELFTEST,         /* ODD BUS codec self-test */
    MAO_DEVCMD_FLOOD_BASE = 2000,    /* value - base = seconds of 50 Hz broadcast traffic */
    MAO_DEVCMD_STRESS_BASE = 1000,   /* value - base = duration in seconds */
} mao_devcmd_t;

/* Print banner and chip/boot info, create the event bus, load settings.
 * Call first. */
esp_err_t mao_system_init(void);

/* Start the development console task (no-op unless CONFIG_MAO_DEV_CONSOLE).
 * Called by mao_system_start(). */
esp_err_t mao_devcmd_start(void);

/* Development console command: args is the text after the command word
 * (never NULL, possibly empty). Runs in the console task, so a handler must
 * only call thread-safe APIs and must not block for long. */
typedef void (*mao_devcmd_handler_t)(const char *args);

/* Register "mao <name> [args]" for the development console; usage is the
 * line "mao help" prints. Components register their own commands during
 * init, so mao_system needs no knowledge of them. Without
 * CONFIG_MAO_DEV_CONSOLE this does nothing and returns ESP_OK. */
esp_err_t mao_devcmd_register(const char *name, const char *usage, mao_devcmd_handler_t handler);

/* Arm / re-arm the idle timer: MAO_EVENT_IDLE_TIMEOUT is posted after
 * timeout_ms without another call. 0 disarms. */
void mao_system_idle_kick(uint32_t timeout_ms);

/* Record the outcome of a subsystem init and print "[OK] name" / "[!!] name".
 * Returns err unchanged so it can be chained. */
esp_err_t mao_system_report(const char *name, esp_err_t err);

/* Print "[--] name <reason>" for a subsystem intentionally left disabled. */
void mao_system_report_disabled(const char *name, const char *reason);

/* For hardware a board may not have: ESP_ERR_NOT_SUPPORTED prints
 * "[--] name not fitted" and returns ESP_OK; anything else is reported like
 * mao_system_report(). */
esp_err_t mao_system_report_optional(const char *name, esp_err_t err);

/* What the last report under this name said (ESP_ERR_NOT_FOUND if no
 * subsystem of that name reported; ESP_ERR_NOT_SUPPORTED for "not fitted"
 * and "disabled"). Lets the boot check read the bring-up results. */
esp_err_t mao_system_report_status(const char *name);

/* True while a USB host is connected to the USB-Serial/JTAG console (start
 * of frame packets seen). Light and deep sleep would drop that link, so the
 * power policy keeps MAO awake meanwhile. False on other console types. */
bool mao_system_console_attached(void);

/* Log current heap figures with a short label. */
void mao_system_log_heap(const char *tag, const char *label);

/* Start the dispatcher task, post MAO_EVENT_SYSTEM_READY, print MAO READY. */
esp_err_t mao_system_start(void);

#ifdef __cplusplus
}
#endif
