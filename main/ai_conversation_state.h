// Connection guidance for the AI page; this increment has no conversation client.
#pragma once

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
    AI_GUIDANCE_NOT_IMPLEMENTED,
} ai_conversation_guidance_t;

// A stored endpoint and a router connection never imply a verified AI service.
ai_conversation_guidance_t ai_conversation_guidance(const usb_config_status_t *status);
