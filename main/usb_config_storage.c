#include "usb_config_storage.h"
#include "demo_radio.h"
#include "nvs.h"

#define CONFIG_NAMESPACE "connection"
#define CONFIG_KEY "values_v1"

esp_err_t usb_config_storage_load(usb_config_values_t *values)
{
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    nvs_handle_t handle;
    err = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    size_t size = sizeof(*values);
    usb_config_values_t loaded = { 0 };
    err = nvs_get_blob(handle, CONFIG_KEY, &loaded, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else if (err == ESP_OK) {
        if (size != sizeof(loaded) || !usb_config_values_valid(&loaded)) {
            err = ESP_ERR_INVALID_STATE;
        } else {
            *values = loaded;
        }
    }
    usb_config_clear(&loaded, sizeof(loaded));
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
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
