// Public connection facts only; no credentials, SSIDs, URLs, or ESP-IDF types.
#pragma once

#include <stdbool.h>

typedef enum {
    USB_WIFI_UNCONFIGURED,
    USB_WIFI_CONNECTING,
    USB_WIFI_CONNECTED,
    USB_WIFI_RETRYING,
    USB_WIFI_ERROR,
} usb_config_wifi_state_t;

typedef struct {
    bool ready;
    bool transport_error;
    bool storage_error;
    bool radio_error;
    bool wifi_configured;
    bool service_configured;
    usb_config_wifi_state_t wifi_state;
} usb_config_status_t;
