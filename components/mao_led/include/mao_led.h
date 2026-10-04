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

typedef enum {
    MAO_LED_PULSE_INTERACTION = 0,   /* tiny warm pulse */
    MAO_LED_PULSE_NOTICE,            /* tiny cool pulse */
} mao_led_pulse_t;

/* Initialise the LED and show MAO_LED_STATE_BOOTING. ESP_ERR_NOT_SUPPORTED
 * on boards without an LED; all other calls are then harmless no-ops. */
esp_err_t mao_led_init(void);

/* Set the steady base indication. */
void mao_led_set_state(mao_led_state_t state);

/* Brief flash, then return to the base state. Non-blocking. */
void mao_led_pulse(mao_led_pulse_t kind);

#ifdef __cplusplus
}
#endif
