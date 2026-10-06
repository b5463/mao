/*
 * MAO audio: tiny synthesised UI sounds on the speaker (the board's I2S TX
 * channel: PDM into the NS4150 on the LCDkit, standard I2S into a MAX98357A
 * on the A1).
 *
 * Vocabulary (all < 170 ms, synthesised, no assets). One purpose each:
 *   tick     - dial detent; softer and sparser as the dial spins faster.
 *   touch    - the knob is pressed on HOME (low, soft contact).
 *   release  - the knob is released on HOME (slightly higher lift).
 *   confirm  - rising two-note: open / select.
 *   back     - falling two-note: leave / return.
 *   notice   - something happened that MAO noticed (first encounter).
 *   warm     - long press on HOME: attention acknowledged.
 *   bump     - MAO hits the screen edge (failed transfer), strength 1..3.
 *   depart   - MAO leaves into a connected device (tiny; the motion is the
 *              feedback, this is just the air it displaces).
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
/* Sleep (M4.1): park the stream on its silent floor and stop the channel /
 * start it again from the floor - no click either way. Sounds requested
 * while suspended are dropped. */
esp_err_t mao_audio_suspend(void);
esp_err_t mao_audio_resume(void);
/* DEV: the PDM gain stages (0 /2, 1 x1, 2 x2, 3 x4). LCDkit only
 * (ESP_ERR_NOT_SUPPORTED elsewhere). */
esp_err_t mao_audio_debug_scale(int hp, int sd);

/* intensity 0 (slow) .. 255 (fastest spin). */
void mao_audio_tick(uint8_t intensity);
void mao_audio_touch(void);
void mao_audio_release(void);
void mao_audio_warm(void);
void mao_audio_notice(void);
void mao_audio_confirm(void);
void mao_audio_back(void);
/* strength 1 = soft bump, 2 = firmer, 3 = the decisive THUNK. */
void mao_audio_bump(uint8_t strength);
void mao_audio_depart(void);
/* A frame was taken (the camera's shutter). */
void mao_audio_shutter(void);
/* Two dry clicks: mild disapproval (A0 vocabulary; not used by the M4.1
 * character). */
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
