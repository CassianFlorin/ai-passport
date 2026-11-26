// main/app_clock.c - Start SNTP once a station connection exists.
#include "app_clock.h"

#include "esp_log.h"
#include "esp_netif_sntp.h"

#include <time.h>

static const char *TAG = "app_clock";

// Reachable from mainland China and elsewhere; one server fits the default
// CONFIG_LWIP_SNTP_MAX_SERVERS=1.
#define SNTP_SERVER "ntp.aliyun.com"

static bool s_started;

void app_clock_on_link(bool connected)
{
    if (s_started || !connected) return;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER);
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        // Retried on the next call; notes are saved undated meanwhile.
        ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(err));
        return;
    }
    s_started = true;
    ESP_LOGI(TAG, "SNTP started (%s)", SNTP_SERVER);
}

uint32_t app_clock_epoch(void)
{
    return app_clock_valid_or_zero((int64_t)time(NULL));
}
