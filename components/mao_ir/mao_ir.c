#include "mao_ir.h"
#include "mao_ir_priv.h"

#include "esp_log.h"
#include "mao_board.h"
#include "mao_system.h"

static const char *TAG = "MAO_IR";

static mao_board_ir_t s_ir;
static mao_ir_mode_t s_mode = MAO_IR_MODE_DISABLED;

esp_err_t mao_ir_init(void)
{
    mao_board_ir_get(&s_ir);
    s_mode = MAO_IR_MODE_DISABLED;
    if (s_ir.tx_gpio < 0 && s_ir.rx_gpio < 0) {
        /* Shared, jumpered line (LCDkit): never configured. */
        if (!s_ir.mode_known) {
            ESP_LOGI(TAG, "MAO IR: disabled — hardware jumper mode not yet verified");
        }
        mao_system_report_disabled("IR", "disabled");
        return ESP_OK;
    }
    const esp_err_t err = mao_system_report("IR", mao_ir_nec_start(&s_ir));
    if (err == ESP_OK) {
        s_mode = MAO_IR_MODE_TX_RX;
    }
    return err;
}

mao_ir_mode_t mao_ir_get_mode(void)
{
    return s_mode;
}
