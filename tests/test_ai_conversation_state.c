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
    assert(ai_conversation_guidance(&status) == AI_GUIDANCE_READY);
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

    // OK starts only when ready, and every busy stage has a way out.
    assert(ai_talk_command(AI_STAGE_IDLE, false) == AI_COMMAND_NONE);
    assert(ai_talk_command(AI_STAGE_IDLE, true) == AI_COMMAND_START);
    assert(ai_talk_command(AI_STAGE_DONE, true) == AI_COMMAND_START);
    assert(ai_talk_command(AI_STAGE_ERROR, true) == AI_COMMAND_START);
    assert(ai_talk_command(AI_STAGE_ERROR, false) == AI_COMMAND_NONE);
    assert(ai_talk_command(AI_STAGE_CONNECTING, true) == AI_COMMAND_CANCEL);
    assert(ai_talk_command(AI_STAGE_LISTENING, false) == AI_COMMAND_SEND);
    assert(ai_talk_command(AI_STAGE_TRANSCRIBING, true) == AI_COMMAND_CANCEL);
    assert(ai_talk_command(AI_STAGE_THINKING, true) == AI_COMMAND_CANCEL);
    assert(ai_talk_command(AI_STAGE_SPEAKING, true) == AI_COMMAND_STOP_SPEAKING);
    assert(ai_talk_command((ai_stage_t)99, true) == AI_COMMAND_NONE);

    assert(ai_http_error(200) == AI_ERROR_NONE);
    assert(ai_http_error(401) == AI_ERROR_AUTH && ai_http_error(403) == AI_ERROR_AUTH);
    assert(ai_http_error(404) == AI_ERROR_NOT_FOUND);
    assert(ai_http_error(429) == AI_ERROR_RATE_LIMIT);
    assert(ai_http_error(503) == AI_ERROR_SERVER);
    assert(ai_http_error(400) == AI_ERROR_HTTP && ai_http_error(0) == AI_ERROR_HTTP);
    puts("AI conversation connection guidance: PASS");
    return 0;
}
