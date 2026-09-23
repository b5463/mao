/*
 * Wi-Fi bring-up for ESP-NOW: station mode, RAM-only Wi-Fi storage, no power
 * save, fixed channel, never associated. Heap is logged at every step so the
 * radio's memory cost is visible on each boot.
 */
#include "mao_radio.h"
#include "mao_radio_priv.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "mao_system.h"

static const char *TAG = "MAO_RADIO";

static uint8_t s_mac[6];

esp_err_t mao_radio_init(uint8_t channel)
{
    mao_system_log_heap(TAG, "before wifi");

    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    mao_system_log_heap(TAG, "after wifi init");

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    /* ESP-NOW latency matters more than radio power for a desk controller. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "ps");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE), TAG, "channel");
    mao_system_log_heap(TAG, "after wifi start");

    ESP_RETURN_ON_ERROR(esp_read_mac(s_mac, ESP_MAC_WIFI_STA), TAG, "mac");
    ESP_RETURN_ON_ERROR(mao_espnow_init(), TAG, "espnow");
    mao_system_log_heap(TAG, "after espnow init");

    ESP_LOGI(TAG, "radio up: Wi-Fi STA (unassociated) + ESP-NOW, channel %u, mac " MACSTR,
             channel, MAC2STR(s_mac));
    return ESP_OK;
}

void mao_radio_get_mac(uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) {
        mac[i] = s_mac[i];
    }
}
