/*
 * MAO status LED (single RGB LED). Secondary to the display: off at rest,
 * used only for brief meaningful pulses. No effects engine.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_LED_STATE_OFF = 0,
    MAO_LED_STATE_BOOTING,     /* dim blue until the UI is up */
} mao_led_state_t;

/* The LED is subordinate to the screen: off at rest, and only these three
 * short, dim events. It never mirrors the character's state. */
typedef enum {
    MAO_LED_PULSE_NOTICE = 0,   /* cool: something was noticed */
    MAO_LED_PULSE_CONFIRM,      /* warm: an action completed / acknowledged */
    MAO_LED_PULSE_ERROR,        /* red: an actual failure */
} mao_led_pulse_t;

/* Initialise the LED and show MAO_LED_STATE_BOOTING. */
esp_err_t mao_led_init(void);

/* Set the steady base indication. */
void mao_led_set_state(mao_led_state_t state);

/* Brief flash, then return to the base state. Non-blocking. */
void mao_led_pulse(mao_led_pulse_t kind);

#ifdef __cplusplus
}
#endif
