/*
 * MAO link security glue (below mao_devices, above mao_radio): link
 * sessions, the encrypted ESP-NOW peers and the authenticated DATA envelope
 * for paired devices, and MAO's side of the pairing ceremony. The protocol
 * itself is the shared odd_link component (docs/link_security.md).
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mao_link_init(void);

#ifdef __cplusplus
}
#endif
