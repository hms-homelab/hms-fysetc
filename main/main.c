#include "config.h"
#include "pins_config.h"
#include "sd_manager.h"
#include "traffic_monitor.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "nvs_store.h"
#include "captive_portal.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <time.h>

static const char *TAG = LOG_TAG;

static void init_sntp(void)
{
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
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== hms-fysetc v%s (build %s %s) ===",
             FIRMWARE_VERSION, __DATE__, __TIME__);

    // Release SD bus to host device FIRST, before anything else.
    sd_manager_init();

    // NVS (required for WiFi and credential storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // WiFi: try NVS credentials first, fall back to Kconfig defaults
    char nvs_ssid[33] = {0};
    char nvs_pass[65] = {0};
    bool wifi_ok = false;

    if (nvs_store_has_wifi()) {
        // Runtime credentials from captive portal
        nvs_store_get_wifi(nvs_ssid, sizeof(nvs_ssid), nvs_pass, sizeof(nvs_pass));
        ESP_LOGI(TAG, "Using NVS WiFi credentials (SSID: %s)", nvs_ssid);

        wifi_ok = wifi_manager_connect_dynamic(nvs_ssid, nvs_pass);

        if (!wifi_ok) {
            // Bad credentials — clear NVS so next boot goes to portal
            ESP_LOGW(TAG, "NVS WiFi failed, clearing credentials and rebooting");
            nvs_store_clear_wifi();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            return;
        }
    } else if (strlen(WIFI_SSID) > 0 &&
               strcmp(WIFI_SSID, "your_wifi_ssid") != 0) {
        // Kconfig defaults are set — use compile-time credentials
        ESP_LOGI(TAG, "Using Kconfig WiFi credentials (SSID: %s)", WIFI_SSID);
        wifi_manager_init();
        wifi_ok = wifi_manager_connect();

        if (!wifi_ok) {
            ESP_LOGW(TAG, "Kconfig WiFi failed, starting captive portal");
            captive_portal_start();  // blocks forever, reboots after save
            return;
        }
    } else {
        // No credentials anywhere — start captive portal
        ESP_LOGI(TAG, "No WiFi credentials, starting captive portal");
        captive_portal_start();  // blocks forever, reboots after save
        return;
    }

    // WiFi connected — continue with file server
    init_sntp();

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
