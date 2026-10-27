// Versioned connection settings share one NVS transaction, separate from audio.
#pragma once

#include "esp_err.h"
#include "usb_config_protocol.h"

// Missing data leaves the caller's defaults intact; malformed data returns error.
esp_err_t usb_config_storage_load(usb_config_values_t *values);
esp_err_t usb_config_storage_save(const usb_config_values_t *values);
