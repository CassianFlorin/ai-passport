// One push-to-talk turn against an OpenAI-compatible cloud service.
// A short-lived task owns HTTP, the microphone and speech playback; the UI only
// sends commands and reads a published snapshot.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ai_conversation_state.h"
#include "esp_err.h"

#define AI_CLIENT_USER_MAX 256
#define AI_CLIENT_REPLY_MAX 768
#define AI_CLIENT_RECORD_SECONDS 15

typedef struct {
    ai_stage_t stage;
    ai_error_t error;
    ai_stage_t failed_stage;
    ai_error_t speech_error; // Speech failed after the text reply succeeded.
    bool speech_skipped;     // Muted output shows the text only.
    bool busy;               // A cancelled turn is still closing its request.
    int http_status;
    uint8_t seconds;         // Recording time while listening.
    uint32_t revision;
} ai_client_status_t;

// Call once at startup before the input worker can send commands.
esp_err_t ai_client_init(void);

// Call on the input worker. Commands never block on network or audio work.
void ai_client_command(ai_command_t command);
// Stop any turn, for example when leaving the AI page.
void ai_client_cancel(void);
// Forget history and the shown exchange. Ignored while a turn is running.
bool ai_client_reset(void);

void ai_client_get_status(ai_client_status_t *status);
uint32_t ai_client_revision(void);

// Call reader with the last transcript and reply while their lock is held.
// The reader must not block; it may hold the LVGL lock (never the reverse).
bool ai_client_read_text(void (*reader)(const char *user, const char *reply, void *arg),
                         void *arg);

// Completion for ai_client_fetch_models, called on the fetch task. models is a
// JSON array body ("a","b") or NULL with error set; status is the HTTP status.
typedef void (*ai_models_done_t)(uint32_t id, const char *models, bool truncated,
                                 const char *error, int status);

// GET {url}/models from the device, so API keys never pass through a website.
// Copies url/token. Returns false while a conversation or another fetch runs.
bool ai_client_fetch_models(uint32_t id, const char *url, const char *token,
                            ai_models_done_t done);
