// OpenAI-compatible request/response helpers; independent of ESP-IDF and LVGL.
// Every function writes into caller buffers and never allocates.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AI_DEFAULT_CHAT_MODEL "gpt-4o-mini"
#define AI_DEFAULT_ASR_MODEL "whisper-1"
#define AI_DEFAULT_TTS_MODEL "tts-1"
#define AI_DEFAULT_TTS_VOICE "alloy"

#define AI_MULTIPART_BOUNDARY "PassportAudioBoundary7d4a9c"
#define AI_WAV_HEADER_SIZE 44

typedef struct {
    const char *user;
    const char *assistant;
} ai_turn_t;

const char *ai_model_or_default(const char *value, const char *fallback);

// Append path to an http(s) base URL without a query string. Trailing slashes
// on the base are dropped. Returns false for unsupported bases or overflow.
bool ai_endpoint(char *out, size_t capacity, const char *base, const char *path);

// Copy at most capacity - 1 bytes, cutting only at a UTF-8 character boundary.
size_t ai_utf8_copy(char *out, size_t capacity, const char *text);

// Prepare model text for a small screen and speech: drop Markdown markers,
// control characters, characters outside the BMP (emoji) and extra blank lines.
void ai_text_clean(char *text);

// Return the body length, or 0 when capacity is too small.
size_t ai_chat_body(char *out, size_t capacity, const char *model, const char *system,
                    const ai_turn_t *turns, size_t turn_count, const char *user);
size_t ai_speech_body(char *out, size_t capacity, const char *model,
                      const char *voice, const char *input);

// Multipart fields before the streamed WAV file, and the closing delimiter.
size_t ai_transcription_head(char *out, size_t capacity, const char *model);
const char *ai_transcription_tail(void);

// RIFF/data sizes are 0xFFFFFFFF because the recording length is unknown when
// streaming starts; decoders treat this as "read to the end of the part".
void ai_wav_header(uint8_t out[AI_WAV_HEADER_SIZE], uint32_t rate,
                   uint16_t bits, uint16_t channels);

// Chunked transfer framing for one non-empty chunk: "<hex length>\r\n".
size_t ai_chunk_prefix(char out[12], size_t length);

// Find the first string value for key after the optional scope key and decode
// JSON escapes into UTF-8. Long values are truncated at a character boundary.
bool ai_json_find_string(const char *json, size_t length, const char *scope,
                         const char *key, char *out, size_t capacity);

typedef enum {
    AI_WAV_NEED_MORE,
    AI_WAV_PCM,
    AI_WAV_INVALID,
} ai_wav_result_t;

typedef struct {
    uint8_t header[16];
    uint32_t want;
    uint32_t have;
    uint32_t skip;
    uint32_t data_left; // UINT32_MAX means unbounded.
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    uint8_t stage;
    bool fmt_seen;
} ai_wav_parser_t;

void ai_wav_init(ai_wav_parser_t *parser);

// Consume header bytes from *data. On AI_WAV_PCM, *data/*length describe PCM
// bytes in this buffer (possibly empty). Only 16-bit PCM with 1–2 channels is
// accepted. Bytes after a sized data chunk are discarded.
ai_wav_result_t ai_wav_feed(ai_wav_parser_t *parser, const uint8_t **data, size_t *length);

// Streaming extraction of "id" string values from GET /models, independent of
// how the response is split across reads. IDs are appended to out as a JSON
// array body ("a","b",...). Only IDs of [A-Za-z0-9._:/@+-] up to 64 bytes are
// kept, so no escaping is needed; others are skipped. When out fills, later
// IDs are dropped and truncated is set.
#define AI_MODEL_ID_MAX 64

typedef struct {
    char *out;
    size_t capacity;
    size_t used;
    unsigned count;
    bool truncated;
    // Scanner state.
    bool in_string;
    bool escape;
    bool expect_value;
    bool capturing;
    bool after_string;
    bool token_valid;
    uint8_t length;
    char token[AI_MODEL_ID_MAX + 1];
} ai_model_scanner_t;

void ai_model_scanner_init(ai_model_scanner_t *scanner, char *out, size_t capacity);
void ai_model_scanner_feed(ai_model_scanner_t *scanner, const char *data, size_t length);
