#include "ai_protocol.h"

#include <stdio.h>
#include <string.h>

const char *ai_model_or_default(const char *value, const char *fallback)
{
    return value && value[0] ? value : fallback;
}

bool ai_endpoint(char *out, size_t capacity, const char *base, const char *path)
{
    if (!out || !capacity || !base || !path) return false;
    out[0] = '\0';
    size_t scheme = !strncmp(base, "https://", 8) ? 8 : !strncmp(base, "http://", 7) ? 7 : 0;
    if (!scheme || strchr(base, '?') || strchr(base, '#')) return false;
    size_t length = strlen(base);
    while (length > scheme && base[length - 1] == '/') length--;
    if (length == scheme) return false;
    size_t path_length = strlen(path);
    if (length + path_length + 1 > capacity) return false;
    memcpy(out, base, length);
    memcpy(out + length, path, path_length + 1);
    return true;
}

static size_t utf8_length(unsigned char lead)
{
    if (lead < 0x80) return 1;
    if (lead >= 0xf0) return 4;
    if (lead >= 0xe0) return 3;
    if (lead >= 0xc0) return 2;
    return 1;
}

size_t ai_utf8_copy(char *out, size_t capacity, const char *text)
{
    if (!out || !capacity) return 0;
    size_t used = 0;
    while (text && text[used]) {
        size_t step = utf8_length((unsigned char)text[used]);
        if (used + step >= capacity) break;
        size_t i = 1;
        while (i < step && text[used + i]) i++;
        if (i < step) break;
        used += step;
    }
    if (text) memcpy(out, text, used);
    out[used] = '\0';
    return used;
}

void ai_text_clean(char *text)
{
    if (!text) return;
    const unsigned char *in = (const unsigned char *)text;
    char *out = text;
    unsigned newlines = 0;
    bool started = false;
    while (*in) {
        size_t step = utf8_length(*in);
        unsigned char c = *in;
        bool keep = true;
        if (c == '*' || c == '#' || c == '`') {
            keep = false;
        } else if (c == '\r') {
            keep = false;
        } else if (c == '\t') {
            c = ' ';
        } else if (c < 0x20 && c != '\n') {
            keep = false;
        } else if (c == 0x7f || step == 4) {
            keep = false;
        } else if (step == 3 && ((c == 0xef && in[1] == 0xb8 && in[2] == 0x8f) ||
                                 (c == 0xe2 && in[1] == 0x80 && in[2] == 0x8d))) {
            // Emoji presentation selector and zero-width joiner.
            keep = false;
        }
        size_t available = 1;
        while (available < step && in[available]) available++;
        if (available < step) break;
        if (keep) {
            if (c == '\n') {
                if (started && ++newlines <= 2) *out++ = '\n';
            } else if (c == ' ' && !started) {
                // Drop leading spaces.
            } else {
                newlines = 0;
                started = true;
                if (step == 1) *out++ = (char)c;
                else {
                    memmove(out, in, step);
                    out += step;
                }
            }
        }
        in += step;
    }
    while (out > text && (out[-1] == '\n' || out[-1] == ' ')) out--;
    *out = '\0';
}

typedef struct {
    char *out;
    size_t capacity;
    size_t used;
    bool overflow;
} writer_t;

static void put(writer_t *writer, const char *text, size_t length)
{
    if (writer->overflow || writer->used + length >= writer->capacity) {
        writer->overflow = true;
        return;
    }
    memcpy(writer->out + writer->used, text, length);
    writer->used += length;
    writer->out[writer->used] = '\0';
}

static void raw(writer_t *writer, const char *text)
{
    put(writer, text, strlen(text));
}

static void string(writer_t *writer, const char *text)
{
    raw(writer, "\"");
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
        char escaped[8];
        if (*p == '"' || *p == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)*p;
            put(writer, escaped, 2);
        } else if (*p == '\n') {
            raw(writer, "\\n");
        } else if (*p < 0x20) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
            raw(writer, escaped);
        } else {
            put(writer, (const char *)p, 1);
        }
    }
    raw(writer, "\"");
}

static void message(writer_t *writer, const char *role, const char *content)
{
    raw(writer, "{\"role\":");
    string(writer, role);
    raw(writer, ",\"content\":");
    string(writer, content);
    raw(writer, "}");
}

static size_t finish(writer_t *writer)
{
    if (writer->overflow) {
        if (writer->capacity) writer->out[0] = '\0';
        return 0;
    }
    return writer->used;
}

size_t ai_chat_body(char *out, size_t capacity, const char *model, const char *system,
                    const ai_turn_t *turns, size_t turn_count, const char *user)
{
    writer_t writer = { .out = out, .capacity = capacity };
    raw(&writer, "{\"model\":");
    string(&writer, model);
    raw(&writer, ",\"stream\":false,\"messages\":[");
    message(&writer, "system", system);
    for (size_t i = 0; i < turn_count; i++) {
        if (!turns[i].user || !turns[i].user[0] ||
            !turns[i].assistant || !turns[i].assistant[0]) continue;
        raw(&writer, ",");
        message(&writer, "user", turns[i].user);
        raw(&writer, ",");
        message(&writer, "assistant", turns[i].assistant);
    }
    raw(&writer, ",");
    message(&writer, "user", user);
    raw(&writer, "]}");
    return finish(&writer);
}

size_t ai_speech_body(char *out, size_t capacity, const char *model,
                      const char *voice, const char *input)
{
    writer_t writer = { .out = out, .capacity = capacity };
    raw(&writer, "{\"model\":");
    string(&writer, model);
    raw(&writer, ",\"voice\":");
    string(&writer, voice);
    raw(&writer, ",\"input\":");
    string(&writer, input);
    raw(&writer, ",\"response_format\":\"wav\"}");
    return finish(&writer);
}

size_t ai_transcription_head(char *out, size_t capacity, const char *model)
{
    if (!model || strpbrk(model, "\r\n\"")) return 0;
    int length = snprintf(out, capacity,
        "--" AI_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
        "--" AI_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"
        "--" AI_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n", model);
    if (length < 0 || (size_t)length >= capacity) {
        if (capacity) out[0] = '\0';
        return 0;
    }
    return (size_t)length;
}

const char *ai_transcription_tail(void)
{
    return "\r\n--" AI_MULTIPART_BOUNDARY "--\r\n";
}

static void le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void le32(uint8_t *out, uint32_t value)
{
    le16(out, (uint16_t)value);
    le16(out + 2, (uint16_t)(value >> 16));
}

void ai_wav_header(uint8_t out[AI_WAV_HEADER_SIZE], uint32_t rate,
                   uint16_t bits, uint16_t channels)
{
    uint16_t block = (uint16_t)(channels * bits / 8);
    memcpy(out, "RIFF", 4);
    le32(out + 4, UINT32_MAX);
    memcpy(out + 8, "WAVEfmt ", 8);
    le32(out + 16, 16);
    le16(out + 20, 1);
    le16(out + 22, channels);
    le32(out + 24, rate);
    le32(out + 28, rate * block);
    le16(out + 32, block);
    le16(out + 34, bits);
    memcpy(out + 36, "data", 4);
    le32(out + 40, UINT32_MAX);
}

size_t ai_chunk_prefix(char out[12], size_t length)
{
    int written = snprintf(out, 12, "%zX\r\n", length);
    return written > 0 && written < 12 ? (size_t)written : 0;
}

static const char *skip_space(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    return p;
}

// Return the closing quote of the string that starts at p, or NULL.
static const char *string_end(const char *p, const char *end)
{
    for (p++; p < end; p++) {
        if (*p == '\\') p++;
        else if (*p == '"') return p;
    }
    return NULL;
}

// Find `"key"` used as an object key (followed by a colon) outside strings.
static const char *find_key(const char *p, const char *end, const char *key)
{
    size_t key_length = strlen(key);
    while (p < end) {
        if (*p != '"') {
            p++;
            continue;
        }
        const char *close = string_end(p, end);
        if (!close) return NULL;
        const char *after = skip_space(close + 1, end);
        if ((size_t)(close - p - 1) == key_length && !memcmp(p + 1, key, key_length) &&
            after < end && *after == ':') {
            return skip_space(after + 1, end);
        }
        p = close + 1;
    }
    return NULL;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_u16(const char *p, const char *end, uint32_t *value)
{
    if (end - p < 4) return false;
    *value = 0;
    for (int i = 0; i < 4; i++) {
        int digit = hex_value(p[i]);
        if (digit < 0) return false;
        *value = (*value << 4) | (uint32_t)digit;
    }
    return true;
}

static size_t encode_utf8(uint32_t point, char out[4])
{
    if (point < 0x80) {
        out[0] = (char)point;
        return 1;
    }
    if (point < 0x800) {
        out[0] = (char)(0xc0 | (point >> 6));
        out[1] = (char)(0x80 | (point & 0x3f));
        return 2;
    }
    if (point < 0x10000) {
        out[0] = (char)(0xe0 | (point >> 12));
        out[1] = (char)(0x80 | ((point >> 6) & 0x3f));
        out[2] = (char)(0x80 | (point & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (point >> 18));
    out[1] = (char)(0x80 | ((point >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((point >> 6) & 0x3f));
    out[3] = (char)(0x80 | (point & 0x3f));
    return 4;
}

bool ai_json_find_string(const char *json, size_t length, const char *scope,
                         const char *key, char *out, size_t capacity)
{
    if (!out || !capacity) return false;
    out[0] = '\0';
    if (!json || !key) return false;
    const char *end = json + length;
    const char *p = json;
    if (scope) {
        p = find_key(p, end, scope);
        if (!p) return false;
    }
    p = find_key(p, end, key);
    if (!p || p >= end || *p != '"') return false;
    size_t used = 0;
    bool full = false;
    for (p++; p < end && *p != '"'; p++) {
        char encoded[4];
        size_t count = 1;
        if (*p != '\\') {
            encoded[0] = *p;
            // Keep a multi-byte character whole when the buffer fills.
            count = utf8_length((unsigned char)*p);
            if (end - p < (ptrdiff_t)count) return false;
            memcpy(encoded, p, count);
            p += count - 1;
        } else {
            if (++p >= end) return false;
            uint32_t point;
            switch (*p) {
            case '"': case '\\': case '/': encoded[0] = *p; break;
            case 'b': encoded[0] = '\b'; break;
            case 'f': encoded[0] = '\f'; break;
            case 'n': encoded[0] = '\n'; break;
            case 'r': encoded[0] = '\r'; break;
            case 't': encoded[0] = '\t'; break;
            case 'u':
                if (!read_u16(p + 1, end, &point)) return false;
                p += 4;
                if (point >= 0xd800 && point <= 0xdbff) {
                    uint32_t low;
                    if (end - p < 7 || p[1] != '\\' || p[2] != 'u' ||
                        !read_u16(p + 3, end, &low) || low < 0xdc00 || low > 0xdfff) {
                        return false;
                    }
                    p += 6;
                    point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
                } else if (point >= 0xdc00 && point <= 0xdfff) {
                    return false;
                }
                count = encode_utf8(point, encoded);
                break;
            default:
                return false;
            }
        }
        if (!full && used + count < capacity) {
            memcpy(out + used, encoded, count);
            used += count;
        } else {
            full = true;
        }
    }
    out[used] = '\0';
    return p < end;
}

enum {
    WAV_RIFF,
    WAV_CHUNK,
    WAV_FMT,
    WAV_SKIP,
    WAV_DATA,
};

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void ai_wav_init(ai_wav_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
    parser->stage = WAV_RIFF;
    parser->want = 12;
}

ai_wav_result_t ai_wav_feed(ai_wav_parser_t *parser, const uint8_t **data, size_t *length)
{
    while (parser->stage != WAV_DATA) {
        if (!*length) return AI_WAV_NEED_MORE;
        if (parser->stage == WAV_SKIP) {
            size_t step = *length < parser->skip ? *length : parser->skip;
            *data += step;
            *length -= step;
            parser->skip -= (uint32_t)step;
            if (!parser->skip) {
                parser->stage = WAV_CHUNK;
                parser->want = 8;
                parser->have = 0;
            }
            continue;
        }
        size_t step = parser->want - parser->have;
        if (step > *length) step = *length;
        memcpy(parser->header + parser->have, *data, step);
        parser->have += (uint32_t)step;
        *data += step;
        *length -= step;
        if (parser->have < parser->want) return AI_WAV_NEED_MORE;

        if (parser->stage == WAV_RIFF) {
            if (memcmp(parser->header, "RIFF", 4) || memcmp(parser->header + 8, "WAVE", 4)) {
                return AI_WAV_INVALID;
            }
            parser->stage = WAV_CHUNK;
            parser->want = 8;
        } else if (parser->stage == WAV_CHUNK) {
            uint32_t size = read_le32(parser->header + 4);
            if (!memcmp(parser->header, "data", 4)) {
                if (!parser->fmt_seen) return AI_WAV_INVALID;
                // Streaming encoders write 0 or 0xFFFFFFFF for an unknown size.
                parser->data_left = size == 0 || size == UINT32_MAX ? UINT32_MAX : size;
                parser->stage = WAV_DATA;
            } else if (!memcmp(parser->header, "fmt ", 4)) {
                if (size < 16) return AI_WAV_INVALID;
                parser->stage = WAV_FMT;
                parser->want = 16;
                parser->skip = size - 16 + (size & 1);
            } else {
                parser->stage = WAV_SKIP;
                parser->skip = size + (size & 1);
                if (!parser->skip) parser->stage = WAV_CHUNK;
                parser->want = 8;
            }
        } else {
            uint16_t format = (uint16_t)(parser->header[0] | parser->header[1] << 8);
            parser->channels = (uint16_t)(parser->header[2] | parser->header[3] << 8);
            parser->rate = read_le32(parser->header + 4);
            parser->bits = (uint16_t)(parser->header[14] | parser->header[15] << 8);
            // WAVE_FORMAT_EXTENSIBLE (0xFFFE) is accepted as plain PCM here.
            if ((format != 1 && format != 0xfffe) || parser->bits != 16 ||
                parser->channels < 1 || parser->channels > 2 ||
                parser->rate < 8000 || parser->rate > 48000) {
                return AI_WAV_INVALID;
            }
            parser->fmt_seen = true;
            parser->stage = parser->skip ? WAV_SKIP : WAV_CHUNK;
            parser->want = 8;
        }
        parser->have = 0;
    }
    if (parser->data_left != UINT32_MAX) {
        if (*length > parser->data_left) *length = parser->data_left;
        parser->data_left -= (uint32_t)*length;
    }
    return AI_WAV_PCM;
}

static bool model_id_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == ':' || c == '/' || c == '@' || c == '+' || c == '-';
}

void ai_model_scanner_init(ai_model_scanner_t *scanner, char *out, size_t capacity)
{
    memset(scanner, 0, sizeof(*scanner));
    scanner->out = out;
    scanner->capacity = capacity;
    if (capacity) out[0] = '\0';
}

static void emit_model(ai_model_scanner_t *scanner)
{
    // '"' + id + '"' + optional ',' + NUL
    size_t need = scanner->length + 2 + (scanner->count ? 1 : 0) + 1;
    if (scanner->used + need > scanner->capacity) {
        scanner->truncated = true;
        return;
    }
    char *p = scanner->out + scanner->used;
    if (scanner->count) *p++ = ',';
    *p++ = '"';
    memcpy(p, scanner->token, scanner->length);
    p += scanner->length;
    *p++ = '"';
    *p = '\0';
    scanner->used = (size_t)(p - scanner->out);
    scanner->count++;
}

void ai_model_scanner_feed(ai_model_scanner_t *scanner, const char *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        char c = data[i];
        if (scanner->in_string) {
            if (scanner->escape) {
                scanner->escape = false;
                scanner->token_valid = false; // Escaped IDs are not kept.
            } else if (c == '\\') {
                scanner->escape = true;
            } else if (c == '"') {
                scanner->in_string = false;
                if (scanner->capturing) {
                    if (scanner->token_valid && scanner->length) emit_model(scanner);
                    scanner->capturing = false;
                    scanner->after_string = false;
                } else {
                    scanner->after_string = true;
                }
            } else if (scanner->length < AI_MODEL_ID_MAX && model_id_char(c)) {
                scanner->token[scanner->length++] = c;
            } else {
                scanner->token_valid = false;
            }
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '"') {
            scanner->in_string = true;
            scanner->capturing = scanner->expect_value;
            scanner->expect_value = false;
            scanner->after_string = false;
            scanner->token_valid = true;
            scanner->length = 0;
            continue;
        }
        // A key string followed by ':' selects whether its value is an ID.
        scanner->expect_value = scanner->after_string && c == ':' &&
                                scanner->token_valid && scanner->length == 2 &&
                                !memcmp(scanner->token, "id", 2);
        scanner->after_string = false;
    }
}
