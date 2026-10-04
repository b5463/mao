/* Private to mao_ir: the NEC transceiver for boards with separate IR TX/RX. */
#pragma once

#include "esp_err.h"
#include "mao_board.h"

/* Create the RMT TX (with carrier) and RX channels, the IR task and the
 * receiver power handling; start receiving. */
esp_err_t mao_ir_nec_start(const mao_board_ir_t *ir);
