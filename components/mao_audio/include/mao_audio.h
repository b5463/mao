/*
 * MAO audio: tiny synthesised UI sounds on the speaker (the board's I2S TX
 * channel: PDM on the LCDkit, standard I2S to a MAX98357A on the A0).
 *
 * Vocabulary (all < 150 ms, synthesised, no assets):
 *   tick     - dial detent. Rate-limited internally; callers may thin further.
 *   notice   - MAO acknowledges (click, wake, first encounter).
 *   confirm  - rising two-note: open / select.
 *   back     - falling two-note: leave / return home.
 *   tsk      - two dry clicks: mild disapproval (fiddling escalation).
 * Not character vocabulary: test_chirp, the factory self-test sweep.
 *
 * All play functions are non-blocking and safe from any task. When the queue
 * is busy, the request is dropped (UI sounds are disposable).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mao_audio_init(void);

/* Output level 0..100 %. 60 % equals the M0 level. */
void mao_audio_set_volume(uint8_t percent);

void mao_audio_tick(void);
void mao_audio_notice(void);
void mao_audio_confirm(void);
void mao_audio_back(void);
void mao_audio_tsk(void);

/* Self-test: a ~450 ms stepped sweep at a fixed level (0..100 % of the
 * board's full gain), independent of the volume setting. */
void mao_audio_test_chirp(uint8_t level_percent);

/* ms since boot (esp_timer) until which MAO's own sound may still be heard,
 * so perception does not mistake it for the room. */
uint32_t mao_audio_busy_until_ms(void);

#ifdef __cplusplus
}
#endif
