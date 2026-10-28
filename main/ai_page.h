// AI conversation page content. Built inside the personal homepage body and
// called only with the LVGL lock held, on the input worker.
#pragma once

#include "lvgl.h"
#include "usb_config_status.h"

void ai_page_build(lv_obj_t *parent);
// Redraw from the client snapshot; unchanged text is not reassigned.
void ai_page_refresh(const usb_config_status_t *connection);
// Scroll the exchange; positive steps move toward earlier text.
void ai_page_scroll(int steps);
