// One worker owns USB requests, persistent connection settings and the radio.
#include "usb_config.h"
#include "usb_config_protocol.h"
#include "usb_config_storage.h"
#include "demo_radio.h"
#include "ai_client.h"

#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCAN_LIMIT 10

static const char *TAG = "usb_config";
static usb_config_values_t s_values = { .version = USB_CONFIG_VERSION };
static usb_config_request_t s_request;
static usb_config_stream_t s_stream;
static TaskHandle_t s_task;
static esp_netif_t *s_netif;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;
static bool s_radio_initialized, s_radio_started, s_wifi_registered, s_ip_registered;
static esp_err_t s_storage_error, s_radio_error;
static const char *s_wifi_state = "unconfigured";
static int64_t s_retry_at, s_connect_started;
static unsigned s_attempt;
// Event callbacks only publish scalar facts; Flash, Wi-Fi control and USB I/O
// stay on the worker. Release/acquire preserves the associated reason/IP value.
static atomic_int s_link_event;
static atomic_uint s_ip, s_disconnect_reason;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static usb_config_status_t s_public_status;
// Guards replacement of s_values against copies made by the AI worker. Only the
// USB worker writes s_values, so its own reads need no lock.
static portMUX_TYPE s_values_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_values_loaded;

static void replace_values(const usb_config_values_t *values)
{
    taskENTER_CRITICAL(&s_values_lock);
    s_values = *values;
    s_values_loaded = true;
    taskEXIT_CRITICAL(&s_values_lock);
}

bool usb_config_copy_service(usb_config_values_t *values)
{
    if (!values) return false;
    taskENTER_CRITICAL(&s_values_lock);
    bool loaded = s_values_loaded;
    if (loaded) *values = s_values;
    taskEXIT_CRITICAL(&s_values_lock);
    usb_config_clear(values->ssid, sizeof(values->ssid));
    usb_config_clear(values->password, sizeof(values->password));
    return loaded;
}

static void publish_status(bool ready, bool transport_error)
{
    usb_config_wifi_state_t wifi = USB_WIFI_UNCONFIGURED;
    if (!strcmp(s_wifi_state, "connecting")) wifi = USB_WIFI_CONNECTING;
    else if (!strcmp(s_wifi_state, "connected")) wifi = USB_WIFI_CONNECTED;
    else if (!strcmp(s_wifi_state, "retrying")) wifi = USB_WIFI_RETRYING;
    else if (!strcmp(s_wifi_state, "error")) wifi = USB_WIFI_ERROR;
    const usb_config_status_t status = {
        .ready = ready,
        .transport_error = transport_error,
        .storage_error = s_storage_error != ESP_OK,
        .radio_error = s_radio_error != ESP_OK,
        .wifi_configured = s_values.ssid[0] != '\0',
        .service_configured = s_values.service_url[0] != '\0',
        .wifi_state = wifi,
    };
    // The worker is the only settings owner. Publish a small value copy so the
    // UI never races credential changes or holds a lock across Flash/radio work.
    taskENTER_CRITICAL(&s_status_lock);
    s_public_status = status;
    taskEXIT_CRITICAL(&s_status_lock);
}

void usb_config_get_status(usb_config_status_t *status)
{
    if (!status) return;
    taskENTER_CRITICAL(&s_status_lock);
    *status = s_public_status;
    taskEXIT_CRITICAL(&s_status_lock);
}

static void radio_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        atomic_store(&s_ip, 0);
        atomic_store(&s_disconnect_reason, event->reason);
        atomic_store(&s_link_event, 1);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        atomic_store(&s_ip, event->ip_info.ip.addr);
        atomic_store(&s_disconnect_reason, 0);
        atomic_store(&s_link_event, 2);
    }
}

static void radio_stop(void)
{
    if (s_radio_started) esp_wifi_stop();
    s_radio_started = false;
    if (s_wifi_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               s_wifi_handler);
    }
    if (s_ip_registered) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
    }
    s_wifi_registered = s_ip_registered = false;
    if (s_radio_initialized) esp_wifi_deinit();
    s_radio_initialized = false;
    if (s_netif) esp_netif_destroy_default_wifi(s_netif);
    s_netif = NULL;
    atomic_store(&s_link_event, 0);
    atomic_store(&s_ip, 0);
}

static esp_err_t radio_start(void)
{
    if (s_radio_started) return ESP_OK;
    esp_err_t err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
    s_netif = esp_netif_new(&netif_config);
    if (!s_netif) return ESP_ERR_NO_MEM;
    err = esp_netif_attach_wifi_station(s_netif);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_default_wifi_sta_handlers();
    if (err != ESP_OK) goto fail;
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) goto fail;
    s_radio_initialized = true;
    err = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                              radio_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) goto fail;
    s_wifi_registered = true;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              radio_event, NULL, &s_ip_handler);
    if (err != ESP_OK) goto fail;
    s_ip_registered = true;
    // The single application blob is authoritative, avoiding a second credential
    // copy in the Wi-Fi driver's own NVS namespace.
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_radio_started = true;
    return ESP_OK;
fail:
    radio_stop();
    return err;
}

static void connect_wifi(void)
{
    s_retry_at = 0;
    s_connect_started = 0;
    if (!s_values.ssid[0]) {
        radio_stop();
        s_wifi_state = "unconfigured";
        s_radio_error = ESP_OK;
        return;
    }
    s_radio_error = radio_start();
    if (s_radio_error != ESP_OK) {
        s_wifi_state = "error";
        return;
    }
    wifi_config_t config = { 0 };
    memcpy(config.sta.ssid, s_values.ssid, strlen(s_values.ssid));
    memcpy(config.sta.password, s_values.password, strlen(s_values.password));
    config.sta.pmf_cfg.capable = true;
    atomic_store(&s_ip, 0);
    atomic_store(&s_link_event, 0);
    s_radio_error = esp_wifi_set_config(WIFI_IF_STA, &config);
    usb_config_clear(&config, sizeof(config));
    if (s_radio_error == ESP_OK) s_radio_error = esp_wifi_connect();
    s_wifi_state = s_radio_error == ESP_OK ? "connecting" : "error";
    if (s_radio_error == ESP_OK) s_connect_started = esp_timer_get_time();
}

static void network_tick(void)
{
    int event = atomic_exchange(&s_link_event, 0);
    int64_t now = esp_timer_get_time();
    if (event == 2) {
        s_wifi_state = "connected";
        s_attempt = 0;
        s_connect_started = s_retry_at = 0;
        s_radio_error = ESP_OK;
    } else if (event == 1 && s_values.ssid[0]) {
        s_wifi_state = "retrying";
        s_connect_started = 0;
        // Retry at 2, 4, 8, 16, then 30 seconds; never spin on a wrong password.
        unsigned seconds = s_attempt < 4 ? (2U << s_attempt) : 30U;
        if (s_attempt < 4) s_attempt++;
        s_retry_at = now + (int64_t)seconds * 1000000;
    }
    if (s_connect_started && now - s_connect_started > 20000000) {
        (void)esp_wifi_disconnect();
        s_connect_started = 0;
        s_wifi_state = "retrying";
        s_retry_at = now + 30000000;
    }
    if (s_retry_at && now >= s_retry_at) connect_wifi();
}

static cJSON *public_state(void)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) return NULL;
    esp_ip4_addr_t address = { .addr = atomic_load(&s_ip) };
    char ip[16] = "";
    if (address.addr) snprintf(ip, sizeof(ip), IPSTR, IP2STR(&address));
    cJSON_AddStringToObject(data, "device", "AI Passport");
    cJSON_AddNumberToObject(data, "protocol", USB_CONFIG_VERSION);
    cJSON_AddStringToObject(data, "ssid", s_values.ssid);
    cJSON_AddStringToObject(data, "service_url", s_values.service_url);
    cJSON_AddStringToObject(data, "chat_model", s_values.chat_model);
    cJSON_AddStringToObject(data, "asr_model", s_values.asr_model);
    cJSON_AddStringToObject(data, "tts_model", s_values.tts_model);
    cJSON_AddStringToObject(data, "tts_voice", s_values.tts_voice);
    cJSON_AddStringToObject(data, "speech_url", s_values.speech_url);
    // Credentials are write-only: neither logs nor configuration reads expose them.
    cJSON_AddBoolToObject(data, "password_set", s_values.password[0] != '\0');
    cJSON_AddBoolToObject(data, "token_set", s_values.service_token[0] != '\0');
    cJSON_AddBoolToObject(data, "speech_token_set", s_values.speech_token[0] != '\0');
    cJSON_AddStringToObject(data, "wifi_state", s_wifi_state);
    cJSON_AddStringToObject(data, "ip", ip);
    cJSON_AddNumberToObject(data, "disconnect_reason", atomic_load(&s_disconnect_reason));
    cJSON_AddStringToObject(data, "storage_error", esp_err_to_name(s_storage_error));
    cJSON_AddStringToObject(data, "radio_error", esp_err_to_name(s_radio_error));
    return data;
}

static void reply(uint32_t id, cJSON *data, const char *error)
{
    cJSON *response = cJSON_CreateObject();
    if (response) {
        cJSON_AddNumberToObject(response, "id", id);
        cJSON_AddBoolToObject(response, "ok", error == NULL && data != NULL);
        if (error || !data) cJSON_AddStringToObject(response, "error", error ? error : "out_of_memory");
        if (data) cJSON_AddItemToObject(response, "data", data);
        char *json = cJSON_PrintUnformatted(response);
        if (json) {
            // One locked stdio write prevents ordinary console logs from splitting
            // a protocol response. The browser ignores every unprefixed line.
            flockfile(stdout);
            printf(USB_CONFIG_PREFIX "%s\n", json);
            fflush(stdout);
            funlockfile(stdout);
            cJSON_free(json);
        }
        cJSON_Delete(response);
    } else {
        cJSON_Delete(data);
    }
}

// Runs on the model-fetch task. The JSON is assembled by hand because the
// list can hold hundreds of IDs, more than cJSON can build in this heap.
static void reply_models(uint32_t id, const char *models, bool truncated,
                         const char *error, int status)
{
    flockfile(stdout);
    if (models) {
        printf(USB_CONFIG_PREFIX "{\"id\":%" PRIu32 ",\"ok\":true,\"data\":{\"models\":[%s],"
               "\"truncated\":%s}}\n", id, models, truncated ? "true" : "false");
    } else {
        // error is one of a fixed set of plain identifiers.
        printf(USB_CONFIG_PREFIX "{\"id\":%" PRIu32 ",\"ok\":false,\"error\":\"%s\","
               "\"status\":%d}\n", id, error, status);
    }
    fflush(stdout);
    funlockfile(stdout);
}

static void fetch_models(uint32_t id)
{
    const char *token = usb_config_models_token(&s_values, &s_request);
    if (!token || !token[0]) {
        reply(id, NULL, "key_required");
    } else if (strcmp(s_wifi_state, "connected")) {
        reply(id, NULL, "wifi_required");
    } else if (!ai_client_fetch_models(id, s_request.values.service_url, token, reply_models)) {
        reply(id, NULL, "busy");
    }
    // On success the fetch task replies; this worker keeps serving USB.
}

static void scan_wifi(uint32_t id)
{
    esp_err_t err = radio_start();
    if (err != ESP_OK) {
        s_radio_error = err;
        reply(id, NULL, "wifi_unavailable");
        return;
    }
    // The driver rejects scans while an association is in progress. When the
    // saved network is unreachable the retry loop is almost always connecting,
    // which is exactly when a different network must be chosen, so pause it.
    bool paused = s_values.ssid[0] && strcmp(s_wifi_state, "connected");
    if (paused) {
        (void)esp_wifi_disconnect();
        s_connect_started = s_retry_at = 0;
        s_wifi_state = "retrying";
    }
    wifi_scan_config_t config = { .show_hidden = false };
    err = esp_wifi_scan_start(&config, true);
    if (paused) {
        // The blocking scan outlasts delivery of the pause's own disconnect
        // event; drop it so it does not stretch the backoff, then resume soon.
        atomic_store(&s_link_event, 0);
        s_retry_at = esp_timer_get_time() + 2000000;
    }
    if (err != ESP_OK) {
        reply(id, NULL, "scan_busy");
        return;
    }
    wifi_ap_record_t records[SCAN_LIMIT];
    uint16_t count = SCAN_LIMIT;
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        esp_wifi_clear_ap_list();
        reply(id, NULL, "scan_failed");
        return;
    }
    cJSON *data = cJSON_CreateObject();
    cJSON *networks = data ? cJSON_AddArrayToObject(data, "networks") : NULL;
    for (uint16_t i = 0; networks && i < count; i++) {
        cJSON *network = cJSON_CreateObject();
        if (!network) break;
        records[i].ssid[sizeof(records[i].ssid) - 1] = 0;
        cJSON_AddStringToObject(network, "ssid", (const char *)records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", records[i].rssi);
        cJSON_AddBoolToObject(network, "open", records[i].authmode == WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(networks, network);
    }
    reply(id, data, NULL);
}

static void handle_request(void)
{
    if (!usb_config_parse(s_stream.line, &s_request)) {
        if (s_request.id) reply(s_request.id, NULL, "invalid_request");
        return;
    }
    if (s_request.command == USB_CONFIG_SCAN) {
        scan_wifi(s_request.id);
        return;
    }
    if (s_request.command == USB_CONFIG_MODELS) {
        fetch_models(s_request.id);
        return;
    }
    if (s_request.command == USB_CONFIG_SAVE) {
        // The worker owns this one static candidate; it is wiped after use.
        static usb_config_values_t candidate;
        const char *error = usb_config_apply(&s_values, &s_request, &candidate);
        if (error) {
            reply(s_request.id, NULL, error);
        } else {
            bool wifi_changed = strcmp(candidate.ssid, s_values.ssid) ||
                                strcmp(candidate.password, s_values.password);
            // A no-op save does not wear Flash.
            s_storage_error = memcmp(&candidate, &s_values, sizeof(candidate)) ||
                              s_storage_error != ESP_OK
                            ? usb_config_storage_save(&candidate) : ESP_OK;
            if (s_storage_error == ESP_OK) {
                replace_values(&candidate);
                if (wifi_changed) {
                    s_attempt = 0;
                    // Finish the old station lifecycle before applying a new
                    // network; stale disconnect events cannot cancel its attempt.
                    radio_stop();
                    connect_wifi();
                }
                reply(s_request.id, public_state(), NULL);
            } else {
                reply(s_request.id, public_state(), "storage_failed");
            }
        }
        usb_config_clear(&candidate, sizeof(candidate));
    } else {
        if (s_request.command == USB_CONFIG_RECONNECT) {
            s_attempt = 0;
            radio_stop();
            connect_wifi();
        }
        network_tick();
        reply(s_request.id, public_state(), NULL);
    }
}

static void usb_worker(void *arg)
{
    (void)arg;
    // Settings records are too large for this task's stack; copy via the heap.
    usb_config_values_t *loaded = malloc(sizeof(*loaded));
    if (loaded) {
        *loaded = s_values;
        s_storage_error = usb_config_storage_load(loaded);
        replace_values(loaded);
        usb_config_clear(loaded, sizeof(*loaded));
        free(loaded);
    } else {
        s_storage_error = ESP_ERR_NO_MEM;
        replace_values(&s_values);
    }
    if (s_values.ssid[0]) connect_wifi();
    publish_status(true, false);
    ESP_LOGI(TAG, "USB configuration ready (protocol %u)", USB_CONFIG_VERSION);
    unsigned char bytes[64];
    for (;;) {
        int count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), pdMS_TO_TICKS(50));
        for (int i = 0; i < count; i++) {
            if (usb_config_stream_feed(&s_stream, bytes[i])) {
                handle_request();
                usb_config_clear(&s_request, sizeof(s_request));
                usb_config_clear(&s_stream, sizeof(s_stream));
            }
        }
        usb_config_clear(bytes, sizeof(bytes));
        network_tick();
        publish_status(true, false);
    }
}

esp_err_t usb_config_start(void)
{
    if (s_task) return ESP_OK;
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 3072,
        .rx_buffer_size = 3072,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) {
        publish_status(false, true);
        return err;
    }
    usb_serial_jtag_vfs_use_driver();
    if (xTaskCreate(usb_worker, "usb_config", 6144, NULL, 3, &s_task) != pdPASS) {
        usb_serial_jtag_vfs_use_nonblocking();
        usb_serial_jtag_driver_uninstall();
        s_task = NULL;
        publish_status(false, true);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
