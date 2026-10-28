// USB configuration is an application-lifetime service, with one worker owner.
#pragma once

#include "esp_err.h"
#include "usb_config_status.h"

// Starts configuration transport even when stored settings or Wi-Fi are unavailable.
// Never erases NVS. The worker serializes Flash writes, radio changes and USB RPCs.
esp_err_t usb_config_start(void);

// Copy a synchronized, secret-free snapshot. No Flash, radio, or USB I/O occurs.
void usb_config_get_status(usb_config_status_t *status);
