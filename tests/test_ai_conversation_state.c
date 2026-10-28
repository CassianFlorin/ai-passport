#include <assert.h>
#include <stdio.h>

#include "ai_conversation_state.h"

int main(void)
{
    usb_config_status_t status = {0};
    assert(ai_conversation_guidance(NULL) == AI_GUIDANCE_USB_UNAVAILABLE);
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_STARTING);
    status.transport_error = true;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_USB_UNAVAILABLE);
    status.transport_error = false;
    status.ready = true;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_WIFI_REQUIRED);
    status.storage_error = true;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_STORAGE_ERROR);
    status.storage_error = false;
    status.wifi_configured = true;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_WIFI_RETRYING);
    status.wifi_state = USB_WIFI_CONNECTING;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_WIFI_CONNECTING);
    status.wifi_state = USB_WIFI_RETRYING;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_WIFI_RETRYING);
    status.wifi_state = USB_WIFI_ERROR;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_WIFI_ERROR);
    status.wifi_state = USB_WIFI_CONNECTED;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_SERVICE_REQUIRED);
    status.service_configured = true;
    // Even the best connection facts must never advertise a working AI client.
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_NOT_IMPLEMENTED);
    status.radio_error = true;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_WIFI_ERROR);
    status.storage_error = true;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_STORAGE_ERROR);
    status.transport_error = true;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_USB_UNAVAILABLE);

    // Storing an endpoint alone cannot skip network setup or a failed connection.
    status = (usb_config_status_t){ .ready = true, .service_configured = true };
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_WIFI_REQUIRED);
    status.wifi_configured = true;
    status.wifi_state = USB_WIFI_CONNECTING;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_WIFI_CONNECTING);
    status.wifi_state = USB_WIFI_RETRYING;
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_WIFI_RETRYING);
    puts("AI conversation connection guidance: PASS");
    return 0;
}
