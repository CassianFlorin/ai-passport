#include "usb_config_storage.h"
#include "nvs.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static esp_err_t init_error, open_error, read_error, write_error, commit_error, erase_error;
static unsigned opens, closes, writes, commits;
static size_t stored_size = sizeof(usb_config_values_t);
static usb_config_values_t stored, pending;
static bool has_stored = true, has_legacy, legacy_erased, has_v2, v2_erased;

// Protocol version 1 and 2 record layouts, retained only to test migration.
typedef struct {
    uint32_t version;
    char ssid[33];
    char password[65];
    char service_url[385];
    char service_token[513];
} legacy_t;
static legacy_t legacy;
static struct {
    legacy_t v1;
    char models[4][65];
} v2;

esp_err_t demo_radio_nvs_prepare(void) { return init_error; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(name, "connection"));
    assert(mode == NVS_READONLY || mode == NVS_READWRITE);
    opens++;
    if (open_error) return open_error;
    *handle = 7;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size)
{
    assert(handle == 7);
    if (!strcmp(key, "values_v1")) {
        if (!has_legacy) return ESP_ERR_NVS_NOT_FOUND;
        assert(*size == sizeof(legacy));
        memcpy(data, &legacy, sizeof(legacy));
        return ESP_OK;
    }
    if (!strcmp(key, "values_v2")) {
        if (!has_v2) return ESP_ERR_NVS_NOT_FOUND;
        assert(*size == sizeof(v2));
        memcpy(data, &v2, sizeof(v2));
        return ESP_OK;
    }
    assert(!strcmp(key, "values_v3"));
    if (read_error) return read_error;
    if (!has_stored) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size >= stored_size);
    memcpy(data, &stored, stored_size);
    *size = stored_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    assert(handle == 7 && !strcmp(key, "values_v3") && size == sizeof(pending));
    writes++;
    if (write_error) return write_error;
    memcpy(&pending, data, size);
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle == 7);
    if (erase_error) return erase_error;
    if (!strcmp(key, "values_v2")) {
        if (!has_v2) return ESP_ERR_NVS_NOT_FOUND;
        v2_erased = true;
        return ESP_OK;
    }
    assert(!strcmp(key, "values_v1"));
    if (!has_legacy) return ESP_ERR_NVS_NOT_FOUND;
    legacy_erased = true;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 7);
    commits++;
    if (commit_error) return commit_error;
    stored = pending;
    has_stored = true;
    if (legacy_erased) has_legacy = false;
    if (v2_erased) has_v2 = false;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 7); closes++; }

int main(void)
{
    usb_config_values_t values = { .version = USB_CONFIG_VERSION };
    open_error = ESP_ERR_NVS_NOT_FOUND;
    assert(usb_config_storage_load(&values) == ESP_OK && !values.ssid[0]);
    open_error = 0;
    read_error = ESP_ERR_NVS_NOT_FOUND;
    assert(usb_config_storage_load(&values) == ESP_OK);
    read_error = 0;
    stored.version = 9;
    assert(usb_config_storage_load(&values) == ESP_ERR_INVALID_STATE && values.version == USB_CONFIG_VERSION);
    stored.version = USB_CONFIG_VERSION;
    stored_size--;
    assert(usb_config_storage_load(&values) == ESP_ERR_INVALID_STATE);
    stored_size = sizeof(stored);
    strcpy(values.ssid, "USB test");
    strcpy(values.password, "sample-password");
    strcpy(values.service_url, "https://service.example/ai");
    assert(usb_config_storage_save(&values) == ESP_OK);
    usb_config_values_t restored = { .version = USB_CONFIG_VERSION };
    assert(usb_config_storage_load(&restored) == ESP_OK);
    assert(!memcmp(&restored, &values, sizeof(values)));
    write_error = ESP_FAIL;
    unsigned before = commits;
    assert(usb_config_storage_save(&values) == ESP_FAIL && commits == before);
    write_error = 0;
    commit_error = ESP_FAIL;
    assert(usb_config_storage_save(&values) == ESP_FAIL);
    commit_error = 0;
    init_error = ESP_FAIL;
    before = opens;
    assert(usb_config_storage_load(&values) == ESP_FAIL && opens == before);
    assert(usb_config_storage_save(&values) == ESP_FAIL && opens == before);
    init_error = 0;
    strcpy(values.service_url, "not a service URL");
    before = writes;
    assert(usb_config_storage_save(&values) == ESP_ERR_INVALID_ARG && writes == before);
    strcpy(values.service_url, "https://service.example/ai");

    // A version 1 record migrates with empty model fields, then is erased by
    // the first version 2 save so old credentials do not remain in Flash.
    has_stored = false;
    has_legacy = true;
    legacy.version = 1;
    strcpy(legacy.ssid, "Legacy");
    strcpy(legacy.password, "legacy-password");
    strcpy(legacy.service_url, "https://legacy.example/v1");
    strcpy(legacy.service_token, "legacy-token");
    usb_config_values_t migrated = { .version = USB_CONFIG_VERSION };
    strcpy(migrated.chat_model, "stale");
    assert(usb_config_storage_load(&migrated) == ESP_OK);
    assert(migrated.version == USB_CONFIG_VERSION && !strcmp(migrated.ssid, "Legacy"));
    assert(!strcmp(migrated.service_token, "legacy-token") && !migrated.chat_model[0]);
    legacy.version = 3;
    assert(usb_config_storage_load(&migrated) == ESP_ERR_INVALID_STATE);
    legacy.version = 1;
    erase_error = ESP_FAIL;
    before = commits;
    assert(usb_config_storage_save(&migrated) == ESP_FAIL && commits == before && has_legacy);
    erase_error = 0;
    strcpy(migrated.chat_model, "gpt-4o-mini");
    assert(usb_config_storage_save(&migrated) == ESP_OK && !has_legacy);
    assert(usb_config_storage_load(&restored) == ESP_OK);
    assert(!strcmp(restored.chat_model, "gpt-4o-mini"));

    // A version 2 record keeps its models, takes precedence over version 1,
    // and both are erased by the next save.
    has_stored = false;
    has_v2 = has_legacy = true;
    v2.v1 = legacy;
    v2.v1.version = 2;
    strcpy(v2.v1.service_url, "https://api.deepseek.com/v1");
    strcpy(v2.models[0], "deepseek-chat");
    strcpy(v2.models[3], "alloy");
    assert(usb_config_storage_load(&migrated) == ESP_OK);
    assert(!strcmp(migrated.service_url, "https://api.deepseek.com/v1"));
    assert(!strcmp(migrated.chat_model, "deepseek-chat") && !strcmp(migrated.tts_voice, "alloy"));
    assert(!migrated.speech_url[0] && !migrated.speech_token[0]);
    v2.v1.version = 1;
    assert(usb_config_storage_load(&migrated) == ESP_ERR_INVALID_STATE);
    v2.v1.version = 2;
    strcpy(migrated.speech_url, "https://api.siliconflow.cn/v1");
    assert(usb_config_storage_save(&migrated) == ESP_OK && !has_v2 && !has_legacy);
    assert(usb_config_storage_load(&restored) == ESP_OK);
    assert(!strcmp(restored.speech_url, "https://api.siliconflow.cn/v1"));
    assert(closes + 1 == opens);
    puts("USB configuration storage: PASS");
    return 0;
}
