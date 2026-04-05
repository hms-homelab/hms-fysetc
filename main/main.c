#include "config.h"
#include "pins_config.h"
#include "sd_manager.h"
#include "traffic_monitor.h"
#include "wifi_manager.h"
#include "web_server.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <time.h>

static const char *TAG = LOG_TAG;

void app_main(void)
{
    ESP_LOGI(TAG, "=== hms-fysetc v%s built %s %s ===",
             FIRMWARE_VERSION, __DATE__, __TIME__);
    ESP_LOGI(TAG, "HTTP file server for FYSETC SD WiFi Pro");

    // Release SD bus to host device FIRST, before anything else.
    sd_manager_init();

    // NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // WiFi
    wifi_manager_init();
    ESP_LOGI(TAG, "Connecting to WiFi (%s)...", WIFI_SSID);
    if (!wifi_manager_connect()) {
        ESP_LOGE(TAG, "WiFi connection failed, will retry");
    }

    // SNTP time sync (for correct timestamps in directory listings)
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    for (int i = 0; i < 100; i++) {
        time_t now = 0;
        time(&now);
        if (now > 1700000000) {
            struct tm *tm = localtime(&now);
            ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d",
                     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                     tm->tm_hour, tm->tm_min, tm->tm_sec);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Web server + file server endpoints
    web_server_start();

    // Traffic monitor (passive PCNT on GPIO 33)
    traffic_monitor_init();

    // Main loop: periodic status logging
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "[STATUS] wifi=%s",
                 wifi_manager_is_connected() ? "OK" : "DOWN");
    }
}
