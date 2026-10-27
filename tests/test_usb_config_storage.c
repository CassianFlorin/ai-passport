#include "usb_config_storage.h"
#include "nvs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static esp_err_t init_error, open_error, read_error, write_error, commit_error;
static unsigned opens, closes, writes, commits;
static size_t stored_size = sizeof(usb_config_values_t);
static usb_config_values_t stored, pending;

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
    assert(handle == 7 && !strcmp(key, "values_v1"));
    if (read_error) return read_error;
    assert(*size >= stored_size);
    memcpy(data, &stored, stored_size);
    *size = stored_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    assert(handle == 7 && !strcmp(key, "values_v1") && size == sizeof(pending));
    writes++;
    if (write_error) return write_error;
    memcpy(&pending, data, size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 7);
    commits++;
    if (commit_error) return commit_error;
    stored = pending;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 7); closes++; }

int main(void)
{
    usb_config_values_t values = { .version = 1 };
    open_error = ESP_ERR_NVS_NOT_FOUND;
    assert(usb_config_storage_load(&values) == ESP_OK && !values.ssid[0]);
    open_error = 0;
    read_error = ESP_ERR_NVS_NOT_FOUND;
    assert(usb_config_storage_load(&values) == ESP_OK);
    read_error = 0;
    stored.version = 9;
    assert(usb_config_storage_load(&values) == ESP_ERR_INVALID_STATE && values.version == 1);
    stored.version = 1;
    stored_size--;
    assert(usb_config_storage_load(&values) == ESP_ERR_INVALID_STATE);
    stored_size = sizeof(stored);
    strcpy(values.ssid, "USB test");
    strcpy(values.password, "sample-password");
    strcpy(values.service_url, "https://service.example/ai");
    assert(usb_config_storage_save(&values) == ESP_OK);
    usb_config_values_t restored = { .version = 1 };
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
    assert(closes + 1 == opens);
    puts("USB configuration storage: PASS");
    return 0;
}
