// AI page state that can be tested without ESP-IDF, LVGL or a network.
#pragma once

#include <stdbool.h>

#include "usb_config_status.h"

typedef enum {
    AI_GUIDANCE_STARTING,
    AI_GUIDANCE_USB_UNAVAILABLE,
    AI_GUIDANCE_STORAGE_ERROR,
    AI_GUIDANCE_WIFI_REQUIRED,
    AI_GUIDANCE_WIFI_ERROR,
    AI_GUIDANCE_WIFI_CONNECTING,
    AI_GUIDANCE_WIFI_RETRYING,
    AI_GUIDANCE_SERVICE_REQUIRED,
    AI_GUIDANCE_READY,
} ai_conversation_guidance_t;

// READY means a router connection and a saved endpoint; the service itself is
// only proven by a completed conversation.
ai_conversation_guidance_t ai_conversation_guidance(const usb_config_status_t *status);

typedef enum {
    AI_STAGE_IDLE,
    AI_STAGE_CONNECTING,
    AI_STAGE_LISTENING,
    AI_STAGE_TRANSCRIBING,
    AI_STAGE_THINKING,
    AI_STAGE_SPEAKING,
    AI_STAGE_DONE,
    AI_STAGE_ERROR,
} ai_stage_t;

typedef enum {
    AI_ERROR_NONE,
    AI_ERROR_NOT_READY,
    AI_ERROR_CONFIG,
    AI_ERROR_NETWORK,
    AI_ERROR_AUTH,
    AI_ERROR_NOT_FOUND,
    AI_ERROR_RATE_LIMIT,
    AI_ERROR_SERVER,
    AI_ERROR_HTTP,
    AI_ERROR_RESPONSE,
    AI_ERROR_NO_SPEECH,
    AI_ERROR_MEMORY,
    AI_ERROR_AUDIO,
    AI_ERROR_AUDIO_BUSY,
} ai_error_t;

typedef enum {
    AI_COMMAND_NONE,
    AI_COMMAND_START,
    AI_COMMAND_SEND,
    AI_COMMAND_CANCEL,
    AI_COMMAND_STOP_SPEAKING,
} ai_command_t;

// One OK click advances the conversation: start, send the recording, cancel a
// pending request, or stop speech while keeping the reply on screen.
ai_command_t ai_talk_command(ai_stage_t stage, bool ready);

ai_error_t ai_http_error(int status);
