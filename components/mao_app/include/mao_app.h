/*
 * MAO application layer.
 *
 *   input driver -> event bus -> mao_app (state) -> mao_ui / mao_character /
 *                                                   mao_audio / mao_led
 *
 * mao_app is the only place that decides behaviour. It runs entirely in the
 * mao_system dispatcher task.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load settings, build the boot view, subscribe to events and switch the
 * display on with that first frame. Call after all drivers are initialised
 * and before mao_system_start(). */
esp_err_t mao_app_init(void);

#ifdef __cplusplus
}
#endif
