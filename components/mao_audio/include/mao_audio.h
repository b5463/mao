/*
 * MAO audio: tiny synthesised UI sounds on the speaker (I2S0 PDM TX).
 *
 * Vocabulary (all < 150 ms, synthesised, no assets):
 *   tick     - dial detent. Rate-limited internally; callers may thin further.
 *   notice   - MAO acknowledges (click, wake, first encounter).
 *   confirm  - rising two-note: open / select.
 *   back     - falling two-note: leave / return home.
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

#ifdef __cplusplus
}
#endif
