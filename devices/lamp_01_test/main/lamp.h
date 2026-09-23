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
} lamp_cmd_t;

void lamp_console_start(void);
void lamp_post_command(lamp_cmd_t cmd, int32_t arg);
