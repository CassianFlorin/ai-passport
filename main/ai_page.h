// AI conversation page content. Built inside the personal homepage body and
// called only with the LVGL lock held, on the input worker.
#pragma once

#include "ai_conversation_state.h"
#include "lvgl.h"
#include "usb_config_status.h"

void ai_page_build(lv_obj_t *parent);
// Redraw from the client snapshot; unchanged text is not reassigned.
void ai_page_refresh(const usb_config_status_t *connection);
// Scroll the exchange; positive steps move toward earlier text.
void ai_page_scroll(int steps);
// Short user-facing cause for a failed turn; shared with the voice-note page.
const char *ai_error_text(ai_error_t error);
// What the user must fix before a turn can start; empty when ready.
const char *ai_guidance_text(ai_conversation_guidance_t guidance);
