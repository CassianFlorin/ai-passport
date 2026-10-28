// Cassian's homepage, system metrics, sound settings and AI conversation.
#pragma once

#include <stdbool.h>

#include "bsp_button.h"

// Call once with the LVGL lock held; this screen lives for the application's life.
void personal_home_create(bool audio_ready, bool settings_ready);

// Call on the input worker without an LVGL lock. Sampling/UI locking is internal.
void personal_home_refresh(void);
// Call every input-worker iteration; redraws only when the AI turn changed.
void personal_home_poll(void);
void personal_home_key(bsp_btn_t btn, bsp_btn_ev_t event);
