/*
 * NEC IR transceiver on the RMT peripheral (new driver: rmt_tx / rmt_rx).
 *
 * NEC frame, 1 us resolution: 9 ms mark + 4.5 ms space, 32 bits LSB first
 * (address, ~address or address high byte, command, ~command), each bit a
 * 560 us mark followed by 560 us (0) or 1690 us (1) of space, then a final
 * 560 us mark. Repeat code: 9 ms mark + 2.25 ms space + 560 us mark.
 * TX: the simple encoder writes all 34 symbols in one go; the RMT adds the
 * 38 kHz carrier (33 % duty) to every mark. RX: the demodulated receiver
 * output is captured as symbols; durations are matched with 25 % tolerance.
 */
#include "mao_ir.h"
#include "mao_ir_priv.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "mao_events.h"
#include "mao_system.h"

static const char *TAG = "MAO_IR";

#define RESOLUTION_HZ       1000000      /* 1 tick = 1 us */
#define MEM_BLOCK_SYMBOLS   64
#define NEC_SYMBOLS         34           /* leader + 32 bits + final mark */
#define NEC_LEAD_MARK_US    9000
#define NEC_LEAD_SPACE_US   4500
#define NEC_REPEAT_SPACE_US 2250
#define NEC_BIT_MARK_US     560
#define NEC_ZERO_SPACE_US   560
#define NEC_ONE_SPACE_US    1690
#define RX_MIN_NS           1250         /* glitch filter: shorter pulses are noise */
#define RX_MAX_NS           12000000     /* idle longer than this ends a frame */
#define TX_TIMEOUT_MS       200
#define ECHO_WINDOW_MS      150          /* own frame seen by our receiver */
#define REPEAT_VALID_MS     250          /* repeats come every 108 ms after a frame */
#define TASK_STACK          3072
#define TASK_PRIO           4

static rmt_channel_handle_t s_tx;
static rmt_channel_handle_t s_rx;
static rmt_encoder_handle_t s_encoder;
static QueueHandle_t s_rx_queue;
static SemaphoreHandle_t s_lock;
static rmt_symbol_word_t s_rx_symbols[MEM_BLOCK_SYMBOLS];
static uint8_t s_tx_frame[4];
static mao_board_ir_t s_ir;
static bool s_rx_on;
static bool s_rx_wanted = true;
static int64_t s_echo_until_us;
static mao_ir_rx_cb_t s_cb;
static void *s_cb_ctx;
static mao_ir_frame_t s_last;
static int64_t s_last_us;
static mao_ir_stats_t s_stats;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

static const rmt_receive_config_t kReceiveCfg = {
    .signal_range_min_ns = RX_MIN_NS,
    .signal_range_max_ns = RX_MAX_NS,
};

/* ------------------------------------------------------------------------ */
/* Encode                                                                   */
/* ------------------------------------------------------------------------ */

static rmt_symbol_word_t symbol(uint32_t mark_us, uint32_t space_us)
{
    return (rmt_symbol_word_t) { .level0 = 1, .duration0 = mark_us, .level1 = 0, .duration1 = space_us };
}

static size_t nec_encode(const void *data, size_t data_size, size_t symbols_written, size_t symbols_free,
                         rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    (void)arg;
    (void)symbols_written;
    if (symbols_free < NEC_SYMBOLS || data_size != 4) {
        return 0;
    }
    const uint8_t *bytes = data;
    size_t n = 0;
    symbols[n++] = symbol(NEC_LEAD_MARK_US, NEC_LEAD_SPACE_US);
    for (int i = 0; i < 32; i++) {
        const bool one = (bytes[i / 8] >> (i % 8)) & 1;
        symbols[n++] = symbol(NEC_BIT_MARK_US, one ? NEC_ONE_SPACE_US : NEC_ZERO_SPACE_US);
    }
    symbols[n++] = symbol(NEC_BIT_MARK_US, NEC_ZERO_SPACE_US);
    *done = true;
    return n;
}

esp_err_t mao_ir_send(uint16_t address, uint8_t command)
{
    ESP_RETURN_ON_FALSE(s_tx, ESP_ERR_NOT_SUPPORTED, TAG, "IR not available");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_tx_frame[0] = (uint8_t)address;
    s_tx_frame[1] = address > 0xFF ? (uint8_t)(address >> 8) : (uint8_t)~address;
    s_tx_frame[2] = command;
    s_tx_frame[3] = (uint8_t)~command;
    s_echo_until_us = esp_timer_get_time() + (int64_t)ECHO_WINDOW_MS * 1000;
    const rmt_transmit_config_t cfg = { .loop_count = 0 };
    /* The encoder reads the frame while it plays: wait before releasing it. */
    esp_err_t err = rmt_transmit(s_tx, s_encoder, s_tx_frame, sizeof(s_tx_frame), &cfg);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(s_tx, TX_TIMEOUT_MS);
    }
    xSemaphoreGive(s_lock);
    return err;
}

/* ------------------------------------------------------------------------ */
/* Decode                                                                   */
/* ------------------------------------------------------------------------ */

static bool near(uint32_t us, uint32_t spec)
{
    return us > spec * 3 / 4 && us < spec * 5 / 4;
}

static bool decode(const rmt_symbol_word_t *s, size_t n, mao_ir_frame_t *out)
{
    if (n >= 2 && near(s[0].duration0, NEC_LEAD_MARK_US) && near(s[0].duration1, NEC_REPEAT_SPACE_US)) {
        if (s_last_us == 0 || esp_timer_get_time() - s_last_us > (int64_t)REPEAT_VALID_MS * 1000) {
            return false;   /* repeat of nothing we heard */
        }
        *out = s_last;
        out->repeat = true;
        return true;
    }
    if (n < NEC_SYMBOLS - 1 || !near(s[0].duration0, NEC_LEAD_MARK_US) ||
        !near(s[0].duration1, NEC_LEAD_SPACE_US)) {
        return false;
    }
    uint32_t bits = 0;
    for (int i = 0; i < 32; i++) {
        const rmt_symbol_word_t *b = &s[1 + i];
        if (!near(b->duration0, NEC_BIT_MARK_US)) {
            return false;
        }
        if (near(b->duration1, NEC_ONE_SPACE_US)) {
            bits |= 1u << i;
        } else if (!near(b->duration1, NEC_ZERO_SPACE_US)) {
            return false;
        }
    }
    const uint8_t a0 = (uint8_t)bits, a1 = (uint8_t)(bits >> 8);
    const uint8_t c0 = (uint8_t)(bits >> 16), c1 = (uint8_t)(bits >> 24);
    if ((uint8_t)~c0 != c1) {
        return false;
    }
    const bool extended = (uint8_t)~a0 != a1;
    *out = (mao_ir_frame_t) {
        .address = extended ? (uint16_t)(a0 | (a1 << 8)) : a0,
        .command = c0,
        .extended = extended,
    };
    return true;
}

static bool rx_done_cb(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *ctx)
{
    (void)channel;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_rx_queue, edata, &woken);
    return woken == pdTRUE;
}

static void ir_task(void *arg)
{
    (void)arg;
    rmt_rx_done_event_data_t ev;
    for (;;) {
        if (xQueueReceive(s_rx_queue, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        mao_ir_frame_t f;
        if (decode(ev.received_symbols, ev.num_symbols, &f)) {
            const int64_t now = esp_timer_get_time();
            f.echo = now < s_echo_until_us;
            if (!f.repeat) {
                s_last = f;
            }
            s_last_us = now;
            portENTER_CRITICAL(&s_stats_lock);
            s_stats.frames++;
            s_stats.echoes += f.echo ? 1 : 0;
            s_stats.last = f;
            s_stats.last_ms = (uint32_t)(now / 1000);
            portEXIT_CRITICAL(&s_stats_lock);
            ESP_LOGI(TAG, "rx: address 0x%0*X command 0x%02X%s%s", f.extended ? 4 : 2, f.address, f.command,
                     f.repeat ? " (repeat)" : "", f.echo ? " (own echo)" : "");
            mao_event_post(MAO_EVENT_IR_RECEIVED, (int32_t)(((uint32_t)f.address << 16) | f.command));
            if (s_cb) {
                s_cb(&f, s_cb_ctx);
            }
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_rx_on) {
            rmt_receive(s_rx, s_rx_symbols, sizeof(s_rx_symbols), &kReceiveCfg);
        }
        xSemaphoreGive(s_lock);
    }
}

/* ------------------------------------------------------------------------ */
/* Receiver power                                                           */
/* ------------------------------------------------------------------------ */

static esp_err_t rx_apply(bool on)
{
    esp_err_t err = ESP_OK;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (on && !s_rx_on) {
        err = mao_board_rail_set(MAO_RAIL_IR_RX, true);
        if (err == ESP_OK) {
            /* The receiver supply comes up through an RC filter; its output
             * is not trustworthy before it settles. */
            vTaskDelay(pdMS_TO_TICKS(s_ir.rx_settle_ms) + 1);
            err = rmt_enable(s_rx);
        }
        if (err == ESP_OK) {
            err = rmt_receive(s_rx, s_rx_symbols, sizeof(s_rx_symbols), &kReceiveCfg);
        }
        s_rx_on = err == ESP_OK;
    } else if (!on && s_rx_on) {
        rmt_disable(s_rx);
        err = mao_board_rail_set(MAO_RAIL_IR_RX, false);
        s_rx_on = false;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t mao_ir_rx_enable(bool on)
{
    ESP_RETURN_ON_FALSE(s_rx, ESP_ERR_NOT_SUPPORTED, TAG, "IR not available");
    s_rx_wanted = on;
    return rx_apply(on);
}

bool mao_ir_rx_is_on(void)
{
    return s_rx_on;
}

void mao_ir_get_stats(mao_ir_stats_t *out)
{
    if (out) {
        portENTER_CRITICAL(&s_stats_lock);
        *out = s_stats;
        portEXIT_CRITICAL(&s_stats_lock);
    }
}

void mao_ir_set_rx_callback(mao_ir_rx_cb_t cb, void *ctx)
{
    s_cb_ctx = ctx;
    s_cb = cb;
}

/* MAO rests (mao_app's power ladder): the receiver is unpowered while it
 * does; it comes back as it was wanted. */
void mao_ir_suspend(bool suspend)
{
    if (s_lock) {
        rx_apply(!suspend && s_rx_wanted);
    }
}

/* ------------------------------------------------------------------------ */
/* Development console                                                      */
/* ------------------------------------------------------------------------ */

#if CONFIG_MAO_DEV_CONSOLE

static void devcmd_ir(char *arg)
{
    const char *args = arg ? arg : "";
    if (strncmp(args, "send ", 5) == 0) {
        char *end = NULL;
        const long address = strtol(args + 5, &end, 0);
        const long command = end ? strtol(end, NULL, 0) : -1;
        if (address < 0 || address > 0xFFFF || command < 0 || command > 0xFF) {
            ESP_LOGW(TAG, "dev: ir send <address 0..0xFFFF> <command 0..0xFF>");
            return;
        }
        const esp_err_t err = mao_ir_send((uint16_t)address, (uint8_t)command);
        ESP_LOGI(TAG, "dev: sent NEC address 0x%lX command 0x%02lX: %s", address, command, esp_err_to_name(err));
    } else if (strcmp(args, "rx on") == 0 || strcmp(args, "rx off") == 0) {
        const esp_err_t err = mao_ir_rx_enable(args[4] == 'n');
        ESP_LOGI(TAG, "dev: receiver %s: %s", args + 3, esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "dev: ir send <address> <command> | ir rx <on|off>");
    }
}

static void register_devcmds(void)
{
    mao_devcmd_register("ir", devcmd_ir);   /* ir send <address> <command> | ir rx <on|off> (NEC) */
}

#else

static void register_devcmds(void)
{
}

#endif

/* ------------------------------------------------------------------------ */
/* Start                                                                    */
/* ------------------------------------------------------------------------ */

esp_err_t mao_ir_nec_start(const mao_board_ir_t *ir)
{
    ESP_RETURN_ON_FALSE(ir && ir->tx_gpio >= 0 && ir->rx_gpio >= 0, ESP_ERR_NOT_SUPPORTED, TAG, "no IR");
    s_ir = *ir;
    s_lock = xSemaphoreCreateMutex();
    s_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    ESP_RETURN_ON_FALSE(s_lock && s_rx_queue, ESP_ERR_NO_MEM, TAG, "alloc");

    const rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = ir->tx_gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RESOLUTION_HZ,
        .mem_block_symbols = MEM_BLOCK_SYMBOLS,
        .trans_queue_depth = 2,
        .flags.init_level = 0,                   /* LEDs off when idle */
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_tx), TAG, "tx channel");
    const rmt_carrier_config_t carrier = {
        .frequency_hz = ir->carrier_hz,
        .duty_cycle = (float)ir->carrier_duty_pct / 100.0f,
    };
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(s_tx, &carrier), TAG, "carrier");
    const rmt_simple_encoder_config_t enc_cfg = {
        .callback = nec_encode,
        .min_chunk_size = NEC_SYMBOLS,
    };
    ESP_RETURN_ON_ERROR(rmt_new_simple_encoder(&enc_cfg, &s_encoder), TAG, "encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_tx), TAG, "tx enable");

    const rmt_rx_channel_config_t rx_cfg = {
        .gpio_num = ir->rx_gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RESOLUTION_HZ,
        .mem_block_symbols = MEM_BLOCK_SYMBOLS,
        .flags.invert_in = ir->rx_active_low,    /* so a mark reads as level 1 */
    };
    ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rx_cfg, &s_rx), TAG, "rx channel");
    const rmt_rx_event_callbacks_t cbs = { .on_recv_done = rx_done_cb };
    ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(s_rx, &cbs, NULL), TAG, "rx callbacks");

    if (xTaskCreate(ir_task, "mao_ir", TASK_STACK, NULL, TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(rx_apply(true), TAG, "receiver");
    register_devcmds();
    ESP_LOGI(TAG, "IR: NEC TX (%u kHz carrier, %u %% duty) and RX, receiver powered",
             (unsigned)(ir->carrier_hz / 1000), ir->carrier_duty_pct);
    return ESP_OK;
}
