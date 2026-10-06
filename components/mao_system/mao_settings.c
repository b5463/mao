#include "mao_settings.h"

#include <stdio.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "MAO_SYSTEM";

#define NVS_NAMESPACE     "mao"
#define KEY_VERSION       "ver"
#define KEY_FIRST_BOOT    "fb_done"
#define KEY_VOLUME        "volume"
#define KEY_BRIGHTNESS    "bright"
#define KEY_DEV_OPENED    "dev_open"
#define KEY_LAST_DEVICE   "last_dev"
#define KEY_LAST_LAMP     "last_lamp"
#define KEY_ROTATION      "rot"
#define KEY_CHECK_REV     "chk_rev"     /* the boot check's record ("hw_rev" is the A1 manufacturing record) */
#define KEY_HW_FAULTS     "hw_flt"

#define DEFAULT_VOLUME       60
#define DEFAULT_BRIGHTNESS   70

static mao_settings_t s_settings = {
    .first_boot_done = false,
    .volume = DEFAULT_VOLUME,
    .brightness = DEFAULT_BRIGHTNESS,
    .display_rotation = MAO_SETTINGS_ROTATION_DEFAULT,
};
static nvs_handle_t s_nvs;
static bool s_nvs_ok;

static void load_u8(const char *key, uint8_t *value)
{
    uint8_t v;
    if (nvs_get_u8(s_nvs, key, &v) == ESP_OK) {
        *value = v;
    }
}

static esp_err_t store_u8(const char *key, uint8_t value)
{
    if (!s_nvs_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u8(s_nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "settings: store %s failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t mao_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Structural problem with the partition itself (not merely unknown
         * keys). Do not erase automatically: run on defaults and say so. */
        ESP_LOGE(TAG, "settings: NVS partition unusable (%s); running on defaults. "
                 "Erase the nvs partition deliberately to recover.", esp_err_to_name(err));
        return err;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings: nvs_flash_init: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings: open namespace '%s': %s", NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }
    s_nvs_ok = true;

    uint8_t version = 0;
    load_u8(KEY_VERSION, &version);
    if (version == 0) {
        /* New MAO namespace. Future schema changes branch on this value. */
        store_u8(KEY_VERSION, MAO_SETTINGS_SCHEMA_VERSION);
        version = MAO_SETTINGS_SCHEMA_VERSION;
    }

    uint8_t first_boot_done = 0;
    load_u8(KEY_FIRST_BOOT, &first_boot_done);
    s_settings.first_boot_done = first_boot_done != 0;
    load_u8(KEY_VOLUME, &s_settings.volume);
    load_u8(KEY_BRIGHTNESS, &s_settings.brightness);
    load_u8(KEY_DEV_OPENED, &s_settings.devices_opened);
    {
        uint64_t v;
        if (nvs_get_u64(s_nvs, KEY_LAST_DEVICE, &v) == ESP_OK) {
            s_settings.last_device = v;
        }
        if (nvs_get_u64(s_nvs, KEY_LAST_LAMP, &v) == ESP_OK) {
            s_settings.last_lamp = v;
        }
    }
    if (s_settings.volume > 100) {
        s_settings.volume = DEFAULT_VOLUME;
    }
    if (s_settings.brightness > 100 || s_settings.brightness < 5) {
        s_settings.brightness = DEFAULT_BRIGHTNESS;
    }
    int16_t rot = MAO_SETTINGS_ROTATION_DEFAULT;
    if (nvs_get_i16(s_nvs, KEY_ROTATION, &rot) == ESP_OK && (rot == 0 || rot == 90 || rot == 180 || rot == 270)) {
        s_settings.display_rotation = rot;
    }
    size_t len = sizeof(s_settings.check_revision);
    if (nvs_get_str(s_nvs, KEY_CHECK_REV, s_settings.check_revision, &len) != ESP_OK) {
        s_settings.check_revision[0] = 0;
    }
    uint32_t faults = 0;
    if (nvs_get_u32(s_nvs, KEY_HW_FAULTS, &faults) == ESP_OK) {
        s_settings.hw_faults = faults;
    }

    ESP_LOGI(TAG, "settings: schema v%u, first_boot_done=%d volume=%u%% brightness=%u%%",
             version, s_settings.first_boot_done, s_settings.volume, s_settings.brightness);
    return ESP_OK;
}

const mao_settings_t *mao_settings_get(void)
{
    return &s_settings;
}

esp_err_t mao_settings_set_first_boot_done(bool done)
{
    s_settings.first_boot_done = done;
    if (!s_nvs_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!done) {
        /* Remove only MAO's own key. */
        esp_err_t err = nvs_erase_key(s_nvs, KEY_FIRST_BOOT);
        if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
            return nvs_commit(s_nvs);
        }
        return err;
    }
    return store_u8(KEY_FIRST_BOOT, 1);
}

void mao_settings_note_last_device(uint64_t id)
{
    if (!s_nvs_ok || id == s_settings.last_device) {
        return;                              /* the same one again: no write */
    }
    s_settings.last_device = id;
    if (nvs_set_u64(s_nvs, KEY_LAST_DEVICE, id) == ESP_OK) {
        nvs_commit(s_nvs);
    }
}

void mao_settings_note_last_lamp(uint64_t id)
{
    if (!s_nvs_ok || id == s_settings.last_lamp) {
        return;
    }
    s_settings.last_lamp = id;
    if (nvs_set_u64(s_nvs, KEY_LAST_LAMP, id) == ESP_OK) {
        nvs_commit(s_nvs);
    }
}

void mao_settings_note_devices_opened(void)
{
    if (s_settings.devices_opened >= MAO_SETTINGS_HINT_UNTIL) {
        return;                              /* learnt: no more writes */
    }
    s_settings.devices_opened++;
    store_u8(KEY_DEV_OPENED, s_settings.devices_opened);
}

esp_err_t mao_settings_set_volume(uint8_t percent)
{
    s_settings.volume = percent > 100 ? 100 : percent;
    return store_u8(KEY_VOLUME, s_settings.volume);
}

esp_err_t mao_settings_set_brightness(uint8_t percent)
{
    s_settings.brightness = percent > 100 ? 100 : percent;
    return store_u8(KEY_BRIGHTNESS, s_settings.brightness);
}

esp_err_t mao_settings_set_display_rotation(int16_t degrees)
{
    if (degrees != MAO_SETTINGS_ROTATION_DEFAULT && degrees != 0 && degrees != 90 && degrees != 180 &&
        degrees != 270) {
        return ESP_ERR_INVALID_ARG;
    }
    s_settings.display_rotation = degrees;
    if (!s_nvs_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = degrees == MAO_SETTINGS_ROTATION_DEFAULT ? nvs_erase_key(s_nvs, KEY_ROTATION)
                                                             : nvs_set_i16(s_nvs, KEY_ROTATION, degrees);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

esp_err_t mao_settings_set_hw_record(const char *revision, uint32_t faults)
{
    snprintf(s_settings.check_revision, sizeof(s_settings.check_revision), "%s", revision ? revision : "");
    s_settings.hw_faults = faults;
    if (!s_nvs_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_str(s_nvs, KEY_CHECK_REV, s_settings.check_revision);
    if (err == ESP_OK) {
        err = nvs_set_u32(s_nvs, KEY_HW_FAULTS, faults);
    }
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}
