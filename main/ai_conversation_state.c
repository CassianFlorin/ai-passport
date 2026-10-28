#include "ai_conversation_state.h"

ai_conversation_guidance_t ai_conversation_guidance(const usb_config_status_t *status)
{
    if (!status || status->transport_error) return AI_GUIDANCE_USB_UNAVAILABLE;
    if (!status->ready) return AI_GUIDANCE_STARTING;
    if (status->storage_error) return AI_GUIDANCE_STORAGE_ERROR;
    if (status->radio_error || status->wifi_state == USB_WIFI_ERROR) {
        return AI_GUIDANCE_WIFI_ERROR;
    }
    if (!status->wifi_configured) return AI_GUIDANCE_WIFI_REQUIRED;
    if (status->wifi_state == USB_WIFI_CONNECTING) return AI_GUIDANCE_WIFI_CONNECTING;
    if (status->wifi_state != USB_WIFI_CONNECTED) return AI_GUIDANCE_WIFI_RETRYING;
    if (!status->service_configured) return AI_GUIDANCE_SERVICE_REQUIRED;
    return AI_GUIDANCE_READY;
}

ai_command_t ai_talk_command(ai_stage_t stage, bool ready)
{
    switch (stage) {
    case AI_STAGE_IDLE:
    case AI_STAGE_DONE:
    case AI_STAGE_ERROR:
        return ready ? AI_COMMAND_START : AI_COMMAND_NONE;
    case AI_STAGE_LISTENING:
        return AI_COMMAND_SEND;
    case AI_STAGE_SPEAKING:
        return AI_COMMAND_STOP_SPEAKING;
    case AI_STAGE_CONNECTING:
    case AI_STAGE_TRANSCRIBING:
    case AI_STAGE_THINKING:
        return AI_COMMAND_CANCEL;
    default:
        return AI_COMMAND_NONE;
    }
}

ai_error_t ai_http_error(int status)
{
    if (status >= 200 && status < 300) return AI_ERROR_NONE;
    if (status == 401 || status == 403) return AI_ERROR_AUTH;
    if (status == 404) return AI_ERROR_NOT_FOUND;
    if (status == 429) return AI_ERROR_RATE_LIMIT;
    if (status >= 500 && status < 600) return AI_ERROR_SERVER;
    return AI_ERROR_HTTP;
}
