// Bounded USB configuration protocol; independent of ESP-IDF and LVGL.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_CONFIG_PREFIX "@PASSPORT1\t"
#define USB_CONFIG_SSID_MAX 32
#define USB_CONFIG_PASSWORD_MAX 64
#define USB_CONFIG_URL_MAX 384
#define USB_CONFIG_TOKEN_MAX 512
#define USB_CONFIG_MODEL_MAX 64
#define USB_CONFIG_LINE_MAX 4608
#define USB_CONFIG_VERSION 3

typedef struct {
    uint32_t version;
    char ssid[USB_CONFIG_SSID_MAX + 1];
    char password[USB_CONFIG_PASSWORD_MAX + 1];
    char service_url[USB_CONFIG_URL_MAX + 1];
    char service_token[USB_CONFIG_TOKEN_MAX + 1];
    // OpenAI-compatible model names and TTS voice; empty selects the default.
    char chat_model[USB_CONFIG_MODEL_MAX + 1];
    char asr_model[USB_CONFIG_MODEL_MAX + 1];
    char tts_model[USB_CONFIG_MODEL_MAX + 1];
    char tts_voice[USB_CONFIG_MODEL_MAX + 1];
    // Optional transcription/speech service; an empty URL reuses the chat
    // service URL and key, for providers that offer all three endpoints.
    char speech_url[USB_CONFIG_URL_MAX + 1];
    char speech_token[USB_CONFIG_TOKEN_MAX + 1];
} usb_config_values_t;

typedef enum {
    USB_CONFIG_GET,
    USB_CONFIG_STATUS,
    USB_CONFIG_SAVE,
    USB_CONFIG_SCAN,
    USB_CONFIG_RECONNECT,
    // List a service's models from the device, so keys never reach a server.
    // Uses values.service_url and service_token; keep_token selects the saved
    // key, which is only released to the URL it was saved with.
    USB_CONFIG_MODELS,
} usb_config_command_t;

typedef struct {
    uint32_t id;
    usb_config_command_t command;
    usb_config_values_t values;
    bool keep_password;
    bool keep_token;
    bool keep_speech_token;
} usb_config_request_t;

typedef struct {
    char line[USB_CONFIG_LINE_MAX];
    size_t used;
    bool dropping;
} usb_config_stream_t;

// Only a complete line is delivered; overflow discards through the next newline.
bool usb_config_stream_feed(usb_config_stream_t *stream, unsigned char byte);
void usb_config_clear(void *data, size_t size);
bool usb_config_values_valid(const usb_config_values_t *values);

// The key the device would send to url for a models request, or NULL.
const char *usb_config_models_token(const usb_config_values_t *saved,
                                    const usb_config_request_t *request);

// The parser replaces tabs in-place. Hex fields keep credentials out of framing.
// A "*" secret retains its previous value; an empty field explicitly clears it.
bool usb_config_parse(char *line, usb_config_request_t *request);
const char *usb_config_apply(const usb_config_values_t *previous,
                             const usb_config_request_t *request,
                             usb_config_values_t *candidate);
