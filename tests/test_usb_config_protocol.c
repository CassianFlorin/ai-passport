#include "usb_config_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parse(const char *frame, usb_config_request_t *request)
{
    char line[USB_CONFIG_LINE_MAX];
    assert(strlen(frame) < sizeof(line));
    strcpy(line, frame);
    assert(usb_config_parse(line, request));
}

static void invalid(const char *frame)
{
    char line[USB_CONFIG_LINE_MAX];
    strcpy(line, frame);
    usb_config_request_t request;
    assert(!usb_config_parse(line, &request));
}

int main(void)
{
    usb_config_request_t request;
    parse(USB_CONFIG_PREFIX "1\tget", &request);
    assert(request.id == 1 && request.command == USB_CONFIG_GET);
    parse(USB_CONFIG_PREFIX "4294967295\tstatus", &request);
    assert(request.id == UINT32_MAX);
    invalid(USB_CONFIG_PREFIX "4294967296\tget");
    invalid(USB_CONFIG_PREFIX "0\tget");
    invalid(USB_CONFIG_PREFIX "1x\tget");
    invalid(USB_CONFIG_PREFIX "1\tget\textra");
    invalid(USB_CONFIG_PREFIX "1\tunknown");
    invalid(USB_CONFIG_PREFIX "1\tsave\t00\t*\t\t*\t\t\t\t\t\t*");
    invalid(USB_CONFIG_PREFIX "1\tsave\t7g\t*\t\t*\t\t\t\t\t\t*");
    invalid(USB_CONFIG_PREFIX "1\tsave\t7\t*\t\t*\t\t\t\t\t\t*");
    // Version 1 clients sent six fields; they must not silently clear models.
    invalid(USB_CONFIG_PREFIX "1\tsave\t4c6162\t*\t\t*");
    invalid(USB_CONFIG_PREFIX "1\tsave\t4c6162\t*\t\t*\t\t\t\t\t");
    // Version 2 clients sent ten fields; they must not clear the speech service.
    invalid(USB_CONFIG_PREFIX "1\tsave\t4c6162\t*\t\t*\t\t\t\t");
    invalid(USB_CONFIG_PREFIX "1\tsave\t4c6162\t*\t\t*\t\t\t\t\t\t*\t");
    invalid("ordinary console output");

    // models: URL plus a typed key, or "*" for the saved key of that exact URL.
    usb_config_values_t saved = { .version = USB_CONFIG_VERSION };
    strcpy(saved.service_url, "https://api.deepseek.com/v1");
    strcpy(saved.service_token, "chat-key");
    strcpy(saved.speech_url, "https://api.siliconflow.cn/v1");
    strcpy(saved.speech_token, "speech-key");
    parse(USB_CONFIG_PREFIX "8\tmodels\t"
          "68747470733a2f2f6170692e646565707365656b2e636f6d2f7631\t*", &request);
    assert(request.command == USB_CONFIG_MODELS && request.keep_token);
    assert(!strcmp(usb_config_models_token(&saved, &request), "chat-key"));
    strcpy(request.values.service_url, "https://api.siliconflow.cn/v1");
    assert(!strcmp(usb_config_models_token(&saved, &request), "speech-key"));
    strcpy(request.values.service_url, "https://attacker.example/v1");
    assert(usb_config_models_token(&saved, &request) == NULL);
    parse(USB_CONFIG_PREFIX "9\tmodels\t68747470733a2f2f78\t6b6579", &request);
    assert(!request.keep_token && !strcmp(usb_config_models_token(&saved, &request), "key"));
    invalid(USB_CONFIG_PREFIX "9\tmodels\t\t*");
    invalid(USB_CONFIG_PREFIX "9\tmodels\t6674703a2f2f78\t*");
    invalid(USB_CONFIG_PREFIX "9\tmodels\t68747470733a2f2f78");
    invalid(USB_CONFIG_PREFIX "9\tmodels\t68747470733a2f2f78\t0a");

    usb_config_values_t previous = { .version = USB_CONFIG_VERSION };
    usb_config_values_t candidate;
    // UTF-8 SSID, explicit empty password/token and a local WS endpoint.
    parse(USB_CONFIG_PREFIX "2\tsave\te5aeb6\t\t77733a2f2f3132372e302e302e313a383030302f6169\t"
          "\t676d\t\t\t616c6c6f79\t68747470733a2f2f73\t6b", &request);
    assert(usb_config_apply(&previous, &request, &candidate) == NULL);
    assert(!strcmp(candidate.ssid, "\xe5\xae\xb6"));
    assert(!strcmp(candidate.service_url, "ws://127.0.0.1:8000/ai"));
    assert(!strcmp(candidate.chat_model, "gm") && !candidate.asr_model[0]);
    assert(!strcmp(candidate.tts_voice, "alloy"));
    assert(!strcmp(candidate.speech_url, "https://s") && !strcmp(candidate.speech_token, "k"));
    strcpy(previous.ssid, "Lab");
    strcpy(previous.password, "sample-password");
    strcpy(previous.service_token, "sample-token");
    strcpy(previous.speech_token, "speech-token");
    parse(USB_CONFIG_PREFIX "3\tsave\t4c6162\t*\t\t*\t\t\t\t\t\t*", &request);
    assert(usb_config_apply(&previous, &request, &candidate) == NULL);
    assert(!strcmp(candidate.password, previous.password));
    assert(!strcmp(candidate.service_token, previous.service_token));
    assert(!strcmp(candidate.speech_token, "speech-token"));
    parse(USB_CONFIG_PREFIX "4\tsave\t4e6577\t*\t\t*\t\t\t\t\t\t*", &request);
    assert(!strcmp(usb_config_apply(&previous, &request, &candidate), "password_required"));
    parse(USB_CONFIG_PREFIX "5\tsave\t4e6577\t\t\t\t\t\t\t\t\t", &request);
    assert(usb_config_apply(&previous, &request, &candidate) == NULL);
    assert(!candidate.password[0] && !candidate.service_token[0] && !candidate.speech_token[0]);
    assert(!strcmp(previous.password, "sample-password"));

    usb_config_values_t values = { .version = USB_CONFIG_VERSION };
    memset(values.ssid, 's', USB_CONFIG_SSID_MAX);
    memset(values.password, 'a', USB_CONFIG_PASSWORD_MAX);
    assert(usb_config_values_valid(&values));
    values.password[63] = 'z';
    assert(!usb_config_values_valid(&values));
    values.password[63] = 0;
    assert(usb_config_values_valid(&values));
    strcpy(values.password, "short");
    assert(!usb_config_values_valid(&values));
    values.password[0] = 0;
    const char *bad_urls[] = {"ftp://host", "https://", "wss://host:0", "wss://host:65536",
        "http://host:abc", "https://user:pass@host", "https://host/#fragment",
        "http://host/a b", "https://host\\other", "ws://[bad", "ws://[]/"};
    for (size_t i = 0; i < sizeof(bad_urls) / sizeof(bad_urls[0]); i++) {
        strcpy(values.service_url, bad_urls[i]);
        assert(!usb_config_values_valid(&values));
    }
    strcpy(values.service_url, "wss://[::1]:8080/ai?mode=voice");
    assert(usb_config_values_valid(&values));
    strcpy(values.ssid, "\xc0\xaf");
    assert(!usb_config_values_valid(&values));
    strcpy(values.ssid, "\xed\xa0\x80");
    assert(!usb_config_values_valid(&values));
    strcpy(values.ssid, "line\nbreak");
    assert(!usb_config_values_valid(&values));
    memset(values.ssid, 's', sizeof(values.ssid));
    assert(!usb_config_values_valid(&values));
    strcpy(values.ssid, "Lab");
    assert(usb_config_values_valid(&values));
    memset(values.chat_model, 'm', USB_CONFIG_MODEL_MAX);
    assert(usb_config_values_valid(&values));
    strcpy(values.tts_voice, "tab\there");
    assert(!usb_config_values_valid(&values));
    values.tts_voice[0] = 0;
    strcpy(values.speech_url, "ftp://speech");
    assert(!usb_config_values_valid(&values));
    strcpy(values.speech_url, "https://api.siliconflow.cn/v1");
    assert(usb_config_values_valid(&values));
    values.version = 2;
    assert(!usb_config_values_valid(&values));

    usb_config_stream_t stream = { 0 };
    const char *frame = USB_CONFIG_PREFIX "99\tget\r\n";
    for (size_t i = 0; i < strlen(frame); i++) {
        bool ready = usb_config_stream_feed(&stream, (unsigned char)frame[i]);
        assert(ready == (i + 1 == strlen(frame)));
    }
    assert(usb_config_parse(stream.line, &request) && request.id == 99);
    usb_config_clear(&stream, sizeof(stream));
    for (size_t i = 0; i < USB_CONFIG_LINE_MAX + 200; i++) {
        assert(!usb_config_stream_feed(&stream, 'x'));
    }
    assert(!usb_config_stream_feed(&stream, '\n'));
    assert(!stream.dropping && !stream.used);
    assert(!usb_config_stream_feed(&stream, 0));
    assert(!usb_config_stream_feed(&stream, '\n'));

    // Arbitrary USB bytes cannot escape the fixed line and parser buffers.
    for (unsigned i = 0; i < 100000; i++) {
        unsigned char byte = (unsigned char)rand();
        if (usb_config_stream_feed(&stream, byte)) {
            (void)usb_config_parse(stream.line, &request);
            usb_config_clear(&request, sizeof(request));
            usb_config_clear(&stream, sizeof(stream));
        }
    }
    usb_config_clear(&previous, sizeof(previous));
    for (size_t i = 0; i < sizeof(previous); i++) assert(((unsigned char *)&previous)[i] == 0);
    puts("USB configuration protocol: PASS");
    return 0;
}
