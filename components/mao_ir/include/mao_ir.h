/*
 * MAO IR: NEC transmit and receive.
 *
 * ESP32-C3-LCDkit: one GPIO shared between the IR LED and the 38 kHz
 * receiver through a jumper whose position was never verified, so IR stays
 * disabled and the pin is never configured (as in M0-M2).
 *
 * MAO_MAIN A1: separate transmitter (IR LEDs, 38 kHz carrier from the RMT)
 * and demodulating receiver (RMT RX on IR_RX, receiver supply AUX_PWR_EN
 * switched by the board). Received frames are posted as
 * MAO_EVENT_IR_RECEIVED (value = (address << 16) | command) and passed to an
 * optional callback. The receiver is powered down while MAO rests.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_IR_MODE_DISABLED = 0,
    MAO_IR_MODE_TX,
    MAO_IR_MODE_RX,
    MAO_IR_MODE_TX_RX,
} mao_ir_mode_t;

typedef struct {
    uint16_t address;         /* 8-bit for standard NEC, 16-bit for extended NEC */
    uint8_t command;
    bool extended;            /* address byte was not followed by its inverse */
    bool repeat;              /* NEC repeat code (address / command of the last frame) */
    bool echo;                /* arrived while MAO itself was transmitting */
} mao_ir_frame_t;

/* Called in the IR task; copy and return. */
typedef void (*mao_ir_rx_cb_t)(const mao_ir_frame_t *frame, void *ctx);

/* LCDkit: record the IR resource and report it as disabled (touches no pin).
 * A1: bring up NEC TX + RX and report it. */
esp_err_t mao_ir_init(void);

mao_ir_mode_t mao_ir_get_mode(void);

static inline bool mao_ir_is_enabled(void)
{
    return mao_ir_get_mode() != MAO_IR_MODE_DISABLED;
}

/* Send one NEC frame. address <= 0xFF: standard NEC (address, ~address);
 * larger: extended NEC with a 16-bit address. The command is always sent
 * with its inverse. Blocks for the ~70 ms the frame takes. */
esp_err_t mao_ir_send(uint16_t address, uint8_t command);

void mao_ir_set_rx_callback(mao_ir_rx_cb_t cb, void *ctx);

/* Receiver power + reception on / off. */
esp_err_t mao_ir_rx_enable(bool on);
bool mao_ir_rx_is_on(void);

/* Reception counters since boot (diagnostics, factory loopback test). */
typedef struct {
    uint32_t frames;          /* decoded frames, repeats included */
    uint32_t echoes;          /* ... of which arrived while MAO transmitted */
    mao_ir_frame_t last;
    uint32_t last_ms;         /* ms since boot of the last frame */
} mao_ir_stats_t;

void mao_ir_get_stats(mao_ir_stats_t *out);

/* MAO rests (true): receiver unpowered; false: back as it was wanted.
 * Harmless where IR is disabled. */
void mao_ir_suspend(bool suspend);

#ifdef __cplusplus
}
#endif
