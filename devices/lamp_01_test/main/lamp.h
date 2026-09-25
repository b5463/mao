/* LAMP 01 test device internals. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define LAMP_CAP_POWER  1
#define LAMP_CAP_LEVEL  2

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
} lamp_cmd_t;

/* LAMP_CMD_FLOOD modes. */
#define LAMP_FLOOD_STATE     0   /* STATE notifications to the controller */
#define LAMP_FLOOD_ANNOUNCE  1   /* broadcast ANNOUNCE beacons */

void lamp_console_start(void);
void lamp_post_command(lamp_cmd_t cmd, int32_t arg);
