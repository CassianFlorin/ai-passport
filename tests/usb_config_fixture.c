// Host serial peer using the real firmware parser, for browser protocol tests.
#include "usb_config_protocol.h"

#include <stdio.h>
#include <string.h>

static void json_string(const char *text)
{
    putchar('"');
    for (; *text; text++) {
        if (*text == '"' || *text == '\\') putchar('\\');
        putchar((unsigned char)*text);
    }
    putchar('"');
}

int main(void)
{
    usb_config_values_t values = { .version = USB_CONFIG_VERSION };
    char line[USB_CONFIG_LINE_MAX];
    puts("I (10) fixture: unrelated boot log");
    fflush(stdout);
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        usb_config_request_t request;
        if (!usb_config_parse(line, &request)) continue;
        const char *error = NULL;
        if (request.command == USB_CONFIG_SAVE) {
            usb_config_values_t candidate;
            error = usb_config_apply(&values, &request, &candidate);
            if (!error) values = candidate;
            usb_config_clear(&candidate, sizeof(candidate));
        }
        printf(USB_CONFIG_PREFIX "{\"id\":%u,\"ok\":%s,", (unsigned)request.id,
               error ? "false" : "true");
        if (error) {
            printf("\"error\":");
            json_string(error);
        } else if (request.command == USB_CONFIG_MODELS) {
            // Mirrors the firmware's key binding; a real device fetches the list.
            const char *token = usb_config_models_token(&values, &request);
            if (token && token[0]) {
                printf("\"data\":{\"models\":[\"fixture-chat\",\"whisper-1\"],\"truncated\":false}");
            } else {
                printf("\"error\":\"key_required\"");
            }
        } else if (request.command == USB_CONFIG_SCAN) {
            printf("\"data\":{\"networks\":[{\"ssid\":\"USB test\",\"rssi\":-40,\"open\":true}]}");
        } else {
            printf("\"data\":{\"device\":\"AI Passport\",\"protocol\":%d,\"ssid\":",
                   USB_CONFIG_VERSION);
            json_string(values.ssid);
            printf(",\"service_url\":");
            json_string(values.service_url);
            printf(",\"chat_model\":");
            json_string(values.chat_model);
            printf(",\"asr_model\":");
            json_string(values.asr_model);
            printf(",\"tts_model\":");
            json_string(values.tts_model);
            printf(",\"tts_voice\":");
            json_string(values.tts_voice);
            printf(",\"speech_url\":");
            json_string(values.speech_url);
            printf(",\"speech_token_set\":%s", values.speech_token[0] ? "true" : "false");
            printf(",\"password_set\":%s,\"token_set\":%s,\"wifi_state\":\"unconfigured\","
                   "\"ip\":\"\",\"storage_error\":\"ESP_OK\"}",
                   values.password[0] ? "true" : "false",
                   values.service_token[0] ? "true" : "false");
        }
        puts("}");
        fflush(stdout);
        usb_config_clear(line, sizeof(line));
        usb_config_clear(&request, sizeof(request));
    }
    return 0;
}
