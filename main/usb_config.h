// USB configuration is an application-lifetime service, with one worker owner.
#pragma once

#include "esp_err.h"

// Starts configuration transport even when stored settings or Wi-Fi are unavailable.
// Never erases NVS. The worker serializes Flash writes, radio changes and USB RPCs.
esp_err_t usb_config_start(void);
