// Short speaker previews run on the input worker, outside LVGL/button callbacks.
#pragma once

#include "esp_err.h"

esp_err_t app_audio_preview(void);
