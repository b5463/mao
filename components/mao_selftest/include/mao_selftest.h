/*
 * MAO self-test: the boot check every boot runs, and the factory / board
 * self-test (brief sections 32, 33, 44).
 *
 * Boot check (mao_selftest_init, every boot, no I/O of its own): collects
 * the bring-up results every subsystem reported, the board revision, and
 * which fitted hardware is missing. Faults are logged clearly and kept in
 * NVS; on the first boot of a board (or a different revision) a BOOTCHECK
 * line is printed. Only a fatal fault (no display or no input) stops MAO
 * from being MAO; everything else degrades gracefully.
 *
 * Factory self-test ("mao selftest [run|auto] [nopads]", or at boot with
 * CONFIG_MAO_SELFTEST_AT_BOOT): every device on the board, automatic checks
 * first (including the expander reset line and the switched rails at their
 * probe pads, measured by the fixture DMM or the operator), then the
 * operator steps (look, listen, turn, press, touch). Output
 * is line-oriented for tools/factory_test.py:
 *   SELFTEST BEGIN ...
 *   SELFTEST STEP <n>/<total> <id> PASS|FAIL|SKIP <detail>
 *   SELFTEST PROMPT <id> <confirm|action> <text>     (answer: mao selftest yes|no|skip)
 *   SELFTEST MEASURE <id> <pad> <net> <on|off> <min_mV> <max_mV> <text>
 *                                         (answer: mao selftest mv <millivolts> | skip)
 *   SELFTEST PASS 33/33 | SELFTEST FAIL 31/33 failed=a,b | SELFTEST INCOMPLETE ...
 *   SELFTEST_JSON {...}
 * Limits and procedure: docs/hardware/mao-factory-test.md.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Boot-check fault bits (kept in NVS, "mao"/"hw_flt"). */
#define MAO_FAULT_DISPLAY     (1u << 0)    /* fatal: MAO cannot show itself */
#define MAO_FAULT_INPUT       (1u << 1)    /* fatal: nobody can reach MAO */
#define MAO_FAULT_EXPANDER    (1u << 2)
#define MAO_FAULT_IMU         (1u << 3)
#define MAO_FAULT_TOF         (1u << 4)
#define MAO_FAULT_ALS         (1u << 5)
#define MAO_FAULT_GAUGE       (1u << 6)
#define MAO_FAULT_HAPTIC      (1u << 7)
#define MAO_FAULT_TOUCH       (1u << 8)
#define MAO_FAULT_MIC         (1u << 9)
#define MAO_FAULT_IR          (1u << 10)
#define MAO_FAULT_AUDIO       (1u << 11)
#define MAO_FAULT_BOARD_ID    (1u << 12)
#define MAO_FAULT_RADIO       (1u << 13)
#define MAO_FAULT_NVS         (1u << 14)
#define MAO_FAULT_POWER       (1u << 15)
#define MAO_FAULT_COUNT       16
#define MAO_FAULT_FATAL       (MAO_FAULT_DISPLAY | MAO_FAULT_INPUT)

typedef struct {
    uint32_t faults;          /* MAO_FAULT_* */
    bool fatal;
    bool first_boot;          /* first boot on this board / revision */
    char code[16];            /* service code for the screen, e.g. "SERVICE 02" */
} mao_boot_check_t;

/* Run the boot check and register "mao selftest". Call after every driver
 * and before mao_app_init() / mao_system_start(). */
esp_err_t mao_selftest_init(void);

const mao_boot_check_t *mao_selftest_boot_check(void);

/* Start the factory self-test in its own task. interactive = false runs
 * only the steps that need no operator (the others are SKIP). */
esp_err_t mao_selftest_start(bool interactive);

bool mao_selftest_running(void);

const char *mao_fault_name(uint32_t bit);

#ifdef __cplusplus
}
#endif
