/* Private to mao_board: helpers shared by every board implementation. */
#pragma once

#include "esp_err.h"

/* Register the "mao board" development command (board-agnostic report:
 * name, revision, capabilities, I2C devices and scan, rails, lines). */
void mao_board_common_init(void);

/* Manufacturing ("mao board rev <name>"): store the board revision record.
 * ESP_ERR_NOT_SUPPORTED on boards without one. */
esp_err_t mao_board_store_revision(const char *rev);
