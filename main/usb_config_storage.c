#include "usb_config_storage.h"
#include "demo_radio.h"
#include "nvs.h"

#include <stdlib.h>
#include <string.h>

#define CONFIG_NAMESPACE "connection"
#define CONFIG_KEY "values_v3"
#define V2_KEY "values_v2"
#define V1_KEY "values_v1"

// Layout written by protocol version 1, before model names were configurable.
typedef struct {
    uint32_t version;
    char ssid[USB_CONFIG_SSID_MAX + 1];
    char password[USB_CONFIG_PASSWORD_MAX + 1];
    char service_url[USB_CONFIG_URL_MAX + 1];
    char service_token[USB_CONFIG_TOKEN_MAX + 1];
} usb_config_values_v1_t;

// Layout written by protocol version 2, before the separate speech service.
typedef struct {
    usb_config_values_v1_t v1;
    char chat_model[USB_CONFIG_MODEL_MAX + 1];
    char asr_model[USB_CONFIG_MODEL_MAX + 1];
    char tts_model[USB_CONFIG_MODEL_MAX + 1];
    char tts_voice[USB_CONFIG_MODEL_MAX + 1];
} usb_config_values_v2_t;

// Read the newest older record. Fields added later stay empty, which selects
// firmware defaults and reuses the chat service for speech.
// Records are kilobytes in size and NVS lookups are stack-hungry, so the
// copies live on the heap rather than the caller's task stack.
static esp_err_t load_legacy(nvs_handle_t handle, usb_config_values_t *loaded)
{
    usb_config_values_v2_t *legacy = calloc(1, sizeof(*legacy));
    if (!legacy) return ESP_ERR_NO_MEM;
    size_t size = sizeof(*legacy);
    uint32_t version = 2;
    esp_err_t err = nvs_get_blob(handle, V2_KEY, legacy, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        version = 1;
        size = sizeof(legacy->v1);
        err = nvs_get_blob(handle, V1_KEY, &legacy->v1, &size);
    }
    if (err == ESP_OK) {
        size_t expected = version == 2 ? sizeof(*legacy) : sizeof(legacy->v1);
        if (size != expected || legacy->v1.version != version) {
            err = ESP_ERR_INVALID_STATE;
        } else {
            loaded->version = USB_CONFIG_VERSION;
            memcpy(loaded->ssid, legacy->v1.ssid, sizeof(legacy->v1.ssid));
            memcpy(loaded->password, legacy->v1.password, sizeof(legacy->v1.password));
            memcpy(loaded->service_url, legacy->v1.service_url,
                   sizeof(legacy->v1.service_url));
            memcpy(loaded->service_token, legacy->v1.service_token,
                   sizeof(legacy->v1.service_token));
            memcpy(loaded->chat_model, legacy->chat_model, sizeof(legacy->chat_model));
            memcpy(loaded->asr_model, legacy->asr_model, sizeof(legacy->asr_model));
            memcpy(loaded->tts_model, legacy->tts_model, sizeof(legacy->tts_model));
            memcpy(loaded->tts_voice, legacy->tts_voice, sizeof(legacy->tts_voice));
        }
    }
    usb_config_clear(legacy, sizeof(*legacy));
    free(legacy);
    return err;
}

esp_err_t usb_config_storage_load(usb_config_values_t *values)
{
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    nvs_handle_t handle;
    err = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    usb_config_values_t *loaded = calloc(1, sizeof(*loaded));
    if (!loaded) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    size_t size = sizeof(*loaded);
    err = nvs_get_blob(handle, CONFIG_KEY, loaded, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // The first v3 save replaces older records; until then read them.
        err = load_legacy(handle, loaded);
        size = sizeof(*loaded);
    }
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else if (err == ESP_OK) {
        if (size != sizeof(*loaded) || !usb_config_values_valid(loaded)) {
            err = ESP_ERR_INVALID_STATE;
        } else {
            *values = *loaded;
        }
    }
    usb_config_clear(loaded, sizeof(*loaded));
    free(loaded);
    return err;
}

esp_err_t usb_config_storage_save(const usb_config_values_t *values)
{
    if (!usb_config_values_valid(values)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    nvs_handle_t handle;
    err = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    // One record prevents Wi-Fi and service fields being mixed across saves.
    // The application publishes its in-memory copy only after this returns OK.
    err = nvs_set_blob(handle, CONFIG_KEY, values, sizeof(*values));
    // Drop superseded records in the same commit so old credentials do not
    // outlive a save that changed or cleared them.
    const char *superseded[] = { V2_KEY, V1_KEY };
    for (size_t i = 0; err == ESP_OK && i < sizeof(superseded) / sizeof(superseded[0]); i++) {
        esp_err_t erase = nvs_erase_key(handle, superseded[i]);
        if (erase != ESP_ERR_NVS_NOT_FOUND) err = erase;
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
