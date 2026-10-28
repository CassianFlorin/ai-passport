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
    return AI_GUIDANCE_NOT_IMPLEMENTED;
}
