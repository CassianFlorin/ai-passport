// main/app_settings.h - Small persistent application settings shared by pages.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_SETTINGS_DEFAULT_VOLUME 80
#define APP_SETTINGS_VOLUME_STEP 5

// Initialize at startup, then serialize setters on the input worker.
// Other tasks may call getters.
// Missing keys default to sound enabled at 80%; old sound-only settings migrate.
esp_err_t app_settings_init(void);

// The sound flag controls speaker playback; microphone capture remains available.
bool app_settings_sound_enabled(void);

// Remembered nonzero volume survives mute. Output volume is zero while muted.
uint8_t app_settings_volume(void);
uint8_t app_settings_output_volume(void);

// Set 0 to mute without losing the previous level; 1..100 also unmutes.
// Cache changes become visible only after the NVS transaction commits.
esp_err_t app_settings_set_volume(uint8_t percent);
esp_err_t app_settings_step_volume(bool increase);
esp_err_t app_settings_set_sound_enabled(bool enabled);
