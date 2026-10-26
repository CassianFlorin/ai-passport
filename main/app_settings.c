// main/app_settings.c - Persistent user preferences for the firmware UI.
#include "app_settings.h"

#include "demo_radio.h"
#include "esp_log.h"
#include "nvs.h"

#include <stdatomic.h>

static const char *TAG = "app_settings";

#define APP_SETTINGS_NAMESPACE "app"
#define APP_SETTINGS_AUDIO_KEY "audio"
#define APP_SETTINGS_LEGACY_SOUND_KEY "sound"
#define AUDIO_ENABLED_BIT 0x100U
#define AUDIO_DEFAULT (AUDIO_ENABLED_BIT | APP_SETTINGS_DEFAULT_VOLUME)

static bool s_loaded;
// Publish volume + mute together so UI/audio readers cannot see a mixed pair.
// After startup, only the input worker writes; a future Web API must enqueue.
static atomic_uint s_audio = ATOMIC_VAR_INIT(AUDIO_DEFAULT);

static bool audio_valid(uint16_t value)
{
    unsigned volume = value & 0xFFU;
    return !(value & ~0x1FFU) && volume >= 1 && volume <= 100;
}

esp_err_t app_settings_init(void)
{
    if (s_loaded) return ESP_OK;

    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;

    nvs_handle_t handle;
    err = nvs_open(APP_SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // A first boot keeps sound enabled without creating a Flash entry yet.
        s_loaded = true;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open settings failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t value = AUDIO_DEFAULT;
    err = nvs_get_u16(handle, APP_SETTINGS_AUDIO_KEY, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        uint8_t enabled = 1;
        err = nvs_get_u8(handle, APP_SETTINGS_LEGACY_SOUND_KEY, &enabled);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        value = APP_SETTINGS_DEFAULT_VOLUME | (enabled ? AUDIO_ENABLED_BIT : 0);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read audio settings failed: %s", esp_err_to_name(err));
        return err;
    }
    if (!audio_valid(value)) return ESP_ERR_INVALID_STATE;

    atomic_store(&s_audio, value);
    s_loaded = true;
    return ESP_OK;
}

bool app_settings_sound_enabled(void)
{
    return (atomic_load(&s_audio) & AUDIO_ENABLED_BIT) != 0;
}

uint8_t app_settings_volume(void)
{
    return (uint8_t)(atomic_load(&s_audio) & 0xFFU);
}

uint8_t app_settings_output_volume(void)
{
    unsigned value = atomic_load(&s_audio);
    return (value & AUDIO_ENABLED_BIT) ? (uint8_t)(value & 0xFFU) : 0;
}

static esp_err_t save_audio(uint16_t value)
{
    if (atomic_load(&s_audio) == value) return ESP_OK;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open settings for write failed: %s", esp_err_to_name(err));
        return err;
    }

    // A single value keeps mute and the remembered level consistent across reboot.
    err = nvs_set_u16(handle, APP_SETTINGS_AUDIO_KEY, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        // Keep the cached value unchanged when Flash persistence fails.
        ESP_LOGE(TAG, "save audio settings failed: %s", esp_err_to_name(err));
        return err;
    }

    atomic_store(&s_audio, value);
    return ESP_OK;
}

esp_err_t app_settings_set_sound_enabled(bool enabled)
{
    esp_err_t err = app_settings_init();
    if (err != ESP_OK) return err;
    return save_audio(app_settings_volume() | (enabled ? AUDIO_ENABLED_BIT : 0));
}

esp_err_t app_settings_set_volume(uint8_t percent)
{
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    esp_err_t err = app_settings_init();
    if (err != ESP_OK) return err;
    if (percent == 0) {
        // Zero means mute while retaining the last nonzero level for restore.
        return save_audio((uint16_t)(app_settings_volume() & ~AUDIO_ENABLED_BIT));
    }
    return save_audio((uint16_t)(AUDIO_ENABLED_BIT | percent));
}

esp_err_t app_settings_step_volume(bool increase)
{
    esp_err_t err = app_settings_init();
    if (err != ESP_OK) return err;
    int percent = app_settings_output_volume();
    percent += increase ? APP_SETTINGS_VOLUME_STEP : -APP_SETTINGS_VOLUME_STEP;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return app_settings_set_volume((uint8_t)percent);
}
