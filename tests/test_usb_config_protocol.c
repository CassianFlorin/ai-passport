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
    invalid(USB_CONFIG_PREFIX "1\tsave\t00\t*\t\t*");
    invalid(USB_CONFIG_PREFIX "1\tsave\t7g\t*\t\t*");
    invalid(USB_CONFIG_PREFIX "1\tsave\t7\t*\t\t*");
    invalid("ordinary console output");

    usb_config_values_t previous = { .version = 1 };
    usb_config_values_t candidate;
    // UTF-8 SSID, explicit empty password/token and a local WS endpoint.
    parse(USB_CONFIG_PREFIX "2\tsave\te5aeb6\t\t77733a2f2f3132372e302e302e313a383030302f6169\t", &request);
    assert(usb_config_apply(&previous, &request, &candidate) == NULL);
    assert(!strcmp(candidate.ssid, "\xe5\xae\xb6"));
    assert(!strcmp(candidate.service_url, "ws://127.0.0.1:8000/ai"));
    strcpy(previous.ssid, "Lab");
    strcpy(previous.password, "sample-password");
    strcpy(previous.service_token, "sample-token");
    parse(USB_CONFIG_PREFIX "3\tsave\t4c6162\t*\t\t*", &request);
    assert(usb_config_apply(&previous, &request, &candidate) == NULL);
    assert(!strcmp(candidate.password, previous.password));
    assert(!strcmp(candidate.service_token, previous.service_token));
    parse(USB_CONFIG_PREFIX "4\tsave\t4e6577\t*\t\t*", &request);
    assert(!strcmp(usb_config_apply(&previous, &request, &candidate), "password_required"));
    parse(USB_CONFIG_PREFIX "5\tsave\t4e6577\t\t\t", &request);
    assert(usb_config_apply(&previous, &request, &candidate) == NULL);
    assert(!candidate.password[0] && !candidate.service_token[0]);
    assert(!strcmp(previous.password, "sample-password"));

    usb_config_values_t values = { .version = 1 };
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
