#include "usb_config_protocol.h"

#include <string.h>

void usb_config_clear(void *data, size_t size)
{
    volatile unsigned char *bytes = data;
    while (size--) *bytes++ = 0;
}

bool usb_config_stream_feed(usb_config_stream_t *stream, unsigned char byte)
{
    if (byte == '\n') {
        if (stream->dropping) {
            usb_config_clear(stream, sizeof(*stream));
            return false;
        }
        if (stream->used && stream->line[stream->used - 1] == '\r') stream->used--;
        stream->line[stream->used] = '\0';
        return stream->used != 0;
    }
    if (stream->dropping) return false;
    if (byte == 0 || stream->used + 1 >= sizeof(stream->line)) {
        // Never execute the suffix of a truncated or binary-contaminated command.
        usb_config_clear(stream->line, sizeof(stream->line));
        stream->used = 0;
        stream->dropping = true;
        return false;
    }
    stream->line[stream->used++] = (char)byte;
    return false;
}

static int hex_digit(unsigned char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool decode(const char *text, char *out, size_t capacity)
{
    size_t length = strlen(text);
    if (length % 2 || length / 2 >= capacity) return false;
    for (size_t i = 0; i < length; i += 2) {
        int high = hex_digit((unsigned char)text[i]);
        int low = hex_digit((unsigned char)text[i + 1]);
        if (high < 0 || low < 0 || (high == 0 && low == 0)) return false;
        out[i / 2] = (char)((high << 4) | low);
    }
    out[length / 2] = '\0';
    return true;
}

static bool text_valid(const char *text, size_t capacity)
{
    const unsigned char *bytes = (const unsigned char *)text;
    size_t length = 0;
    while (length < capacity && bytes[length]) length++;
    if (length == capacity) return false;
    for (size_t i = 0; i < length;) {
        uint32_t point = bytes[i++];
        if (point < 0x20 || point == 0x7f) return false;
        if (point < 0x80) continue;
        unsigned extra;
        uint32_t minimum;
        if (point >= 0xc2 && point <= 0xdf) {
            extra = 1; minimum = 0x80; point &= 0x1f;
        } else if (point >= 0xe0 && point <= 0xef) {
            extra = 2; minimum = 0x800; point &= 0x0f;
        } else if (point >= 0xf0 && point <= 0xf4) {
            extra = 3; minimum = 0x10000; point &= 0x07;
        } else {
            return false;
        }
        if (i + extra > length) return false;
        while (extra--) {
            unsigned char tail = bytes[i++];
            if ((tail & 0xc0) != 0x80) return false;
            point = (point << 6) | (tail & 0x3f);
        }
        if (point < minimum || point > 0x10ffff ||
            (point >= 0xd800 && point <= 0xdfff)) return false;
    }
    return true;
}

static bool url_valid(const char *url)
{
    if (!url[0]) return true;
    const char *host;
    if (!strncmp(url, "https://", 8)) host = url + 8;
    else if (!strncmp(url, "http://", 7)) host = url + 7;
    else if (!strncmp(url, "wss://", 6)) host = url + 6;
    else if (!strncmp(url, "ws://", 5)) host = url + 5;
    else return false;

    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        if (*p <= 0x20 || *p >= 0x7f || *p == '#' || *p == '\\') return false;
    }
    const char *end = host + strcspn(host, "/?");
    if (end == host || memchr(host, '@', (size_t)(end - host))) return false;
    const char *port = NULL;
    if (*host == '[') {
        const char *close = memchr(host, ']', (size_t)(end - host));
        if (!close || close == host + 1) return false;
        for (const char *p = host + 1; p < close; p++) {
            if (hex_digit((unsigned char)*p) < 0 && *p != ':' && *p != '.') return false;
        }
        if (close + 1 < end) {
            if (close[1] != ':') return false;
            port = close + 2;
        }
    } else {
        const char *colon = memchr(host, ':', (size_t)(end - host));
        const char *host_end = colon ? colon : end;
        if (host_end == host) return false;
        for (const char *p = host; p < host_end; p++) {
            unsigned char c = (unsigned char)*p;
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
        }
        if (colon) port = colon + 1;
    }
    if (port) {
        if (port == end) return false;
        unsigned number = 0;
        for (const char *p = port; p < end; p++) {
            if (*p < '0' || *p > '9' || number > 6553) return false;
            number = number * 10 + (unsigned)(*p - '0');
            if (number > 65535) return false;
        }
        if (!number) return false;
    }
    return true;
}

bool usb_config_values_valid(const usb_config_values_t *values)
{
    if (values->version != USB_CONFIG_VERSION ||
        !text_valid(values->ssid, sizeof(values->ssid)) ||
        !text_valid(values->password, sizeof(values->password)) ||
        !text_valid(values->service_url, sizeof(values->service_url)) ||
        !text_valid(values->service_token, sizeof(values->service_token)) ||
        !url_valid(values->service_url)) return false;
    size_t length = strlen(values->password);
    if (!values->ssid[0] && length) return false;
    if (length && length < 8) return false;
    if (length == 64) {
        for (size_t i = 0; i < length; i++) {
            if (hex_digit((unsigned char)values->password[i]) < 0) return false;
        }
    }
    return true;
}

bool usb_config_parse(char *line, usb_config_request_t *request)
{
    memset(request, 0, sizeof(*request));
    size_t prefix_length = strlen(USB_CONFIG_PREFIX);
    if (strncmp(line, USB_CONFIG_PREFIX, prefix_length)) return false;
    char *parts[7];
    size_t count = 0;
    char *field = line + prefix_length;
    for (;;) {
        if (count == sizeof(parts) / sizeof(parts[0])) return false;
        parts[count++] = field;
        char *tab = strchr(field, '\t');
        if (!tab) break;
        *tab = '\0';
        field = tab + 1;
    }
    if (count < 2 || !parts[0][0]) return false;
    uint32_t id = 0;
    for (const char *p = parts[0]; *p; p++) {
        if (*p < '0' || *p > '9' || id > (UINT32_MAX - (unsigned)(*p - '0')) / 10) return false;
        id = id * 10 + (unsigned)(*p - '0');
    }
    if (!id) return false;
    request->id = id;
    if (!strcmp(parts[1], "get")) request->command = USB_CONFIG_GET;
    else if (!strcmp(parts[1], "status")) request->command = USB_CONFIG_STATUS;
    else if (!strcmp(parts[1], "scan")) request->command = USB_CONFIG_SCAN;
    else if (!strcmp(parts[1], "reconnect")) request->command = USB_CONFIG_RECONNECT;
    else if (!strcmp(parts[1], "save")) request->command = USB_CONFIG_SAVE;
    else return false;
    if (request->command != USB_CONFIG_SAVE) return count == 2;
    if (count != 6) return false;
    request->values.version = USB_CONFIG_VERSION;
    request->keep_password = !strcmp(parts[3], "*");
    request->keep_token = !strcmp(parts[5], "*");
    return decode(parts[2], request->values.ssid, sizeof(request->values.ssid)) &&
           (request->keep_password || decode(parts[3], request->values.password,
                                             sizeof(request->values.password))) &&
           decode(parts[4], request->values.service_url, sizeof(request->values.service_url)) &&
           (request->keep_token || decode(parts[5], request->values.service_token,
                                          sizeof(request->values.service_token)));
}

const char *usb_config_apply(const usb_config_values_t *previous,
                             const usb_config_request_t *request,
                             usb_config_values_t *candidate)
{
    *candidate = request->values;
    if (request->keep_password) {
        // Reusing another network's password is easy to do accidentally in a form.
        if (strcmp(previous->ssid, candidate->ssid) && previous->password[0]) {
            return "password_required";
        }
        memcpy(candidate->password, previous->password, sizeof(candidate->password));
    }
    if (request->keep_token) {
        memcpy(candidate->service_token, previous->service_token,
               sizeof(candidate->service_token));
    }
    return usb_config_values_valid(candidate) ? NULL : "invalid_settings";
}
