// Short speaker previews run on the input worker, outside LVGL/button callbacks.
// The AI worker streams speech; one lock serializes codec format and PCM use.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Call once at startup, before either worker can use audio.
esp_err_t app_audio_init(void);
bool app_audio_acquire(uint32_t timeout_ms);
void app_audio_release(void);

// Returns ESP_ERR_TIMEOUT without sound while a conversation owns the codec.
esp_err_t app_audio_preview(void);
