/* LAMP 01 test device internals. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define LAMP_CAP_POWER     1
#define LAMP_CAP_LEVEL     2
#define LAMP_CAP_IDENTIFY  3   /* ODD_CAP_ACTION, semantic IDENTIFY */
#define LAMP_CAP_TOP       3   /* highest capability id (array sizing) */

/* Output (lamp_output.c): LED and/or log. */
esp_err_t lamp_output_init(void);
void lamp_output_apply(bool power, int32_t level);

/* Console (lamp_console.c): development commands, posted to the lamp task. */
typedef enum {
    LAMP_CMD_STATUS = 1,
    LAMP_CMD_OFFLINE,       /* stop answering (simulates power loss) */
    LAMP_CMD_ONLINE,        /* resume and announce */
    LAMP_CMD_SET_POWER,     /* local change, arg = value */
    LAMP_CMD_SET_LEVEL,
    LAMP_CMD_JUNK,          /* broadcast invalid frames (validation test) */
    LAMP_CMD_DROP_ACK,      /* apply, but do not send the next arg ACKs (retry / dedupe test) */
    LAMP_CMD_DELAY_ACK,     /* send every ACK arg ms late (0 = off) */
    LAMP_CMD_FLOOD,         /* arg = hz | seconds << 8 | mode << 24: real traffic towards MAO */
    LAMP_CMD_REBOOT,
    LAMP_CMD_SESSION,       /* log per-controller session state */
    LAMP_CMD_DROP_SESSION,  /* forget all sessions (simulated device reboot) */
    LAMP_CMD_INJECT_PREV,   /* loop in a SET from the PREVIOUS incarnation; arg = cap*1000+value */
    LAMP_CMD_IDENTIFY,      /* run IDENTIFY locally (manual test) */
    LAMP_CMD_ACT_DELAY,     /* completion delay, ms */
    LAMP_CMD_ACT_BUSY,      /* arg 0/1: refuse actions with BUSY */
    LAMP_CMD_ACT_FAIL,      /* arg 0/1: accept, then complete FAILED */
    LAMP_CMD_ACT_DROP_RESULT, /* drop the next arg ACTION_RESULTs */
    LAMP_CMD_ACT_DUP_RESULT,  /* send the last result again */
    LAMP_CMD_ACT_STATUS,    /* action counters and current job */
} lamp_cmd_t;

/* LAMP_CMD_FLOOD modes. */
#define LAMP_FLOOD_STATE     0   /* STATE notifications to the controller */
#define LAMP_FLOOD_ANNOUNCE  1   /* broadcast ANNOUNCE beacons */

void lamp_console_start(void);
void lamp_post_command(lamp_cmd_t cmd, int32_t arg);
