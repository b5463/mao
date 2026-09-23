/*
 * MAO IR. M0: interface only, hardware left untouched.
 *
 * The ESP32-C3-LCDkit shares one GPIO between the IR LED and the 38 kHz
 * receiver; a jumper selects which is connected. Until the jumper position is
 * physically verified the pin is not configured at all.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_IR_MODE_DISABLED = 0,
    MAO_IR_MODE_TX,
    MAO_IR_MODE_RX,
} mao_ir_mode_t;

/* Record the board IR resource and report it as disabled. Never touches the pin. */
esp_err_t mao_ir_init(void);

mao_ir_mode_t mao_ir_get_mode(void);

static inline bool mao_ir_is_enabled(void)
{
    return mao_ir_get_mode() != MAO_IR_MODE_DISABLED;
}

#ifdef __cplusplus
}
#endif
