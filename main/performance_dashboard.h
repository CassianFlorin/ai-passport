// Standalone dashboard UI. Slow operations belong to the input worker.
#pragma once

#include <stdbool.h>

#include "bsp_button.h"

// Call once with the LVGL lock held; this screen lives for the application's life.
void performance_dashboard_create(bool audio_ready, bool settings_ready);

// Call on the input worker without an LVGL lock. Sampling/UI locking is internal.
void performance_dashboard_refresh(void);
void performance_dashboard_key(bsp_btn_t btn, bsp_btn_ev_t event);
