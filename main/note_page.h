// Voice-note page content. Built inside the personal homepage body and called
// only with the LVGL lock held, on the input worker.
#pragma once

#include "lvgl.h"
#include "usb_config_status.h"

#include <stdbool.h>
#include <stddef.h>

// Call on the input worker before taking the LVGL lock: it reads Flash only
// when the store changed, so building and refreshing never touch Flash while
// the lock is held.
void note_page_prepare(void);
// Snapshot taken by note_page_prepare, for the menu row.
size_t note_page_pending(void);
bool note_page_store_ready(void);

void note_page_build(lv_obj_t *parent);
// Redraw the recording state; the saved-note list is rebuilt only when the
// store changed.
void note_page_refresh(const usb_config_status_t *connection);
// Scroll the list; positive steps move toward newer notes.
void note_page_scroll(int steps);
