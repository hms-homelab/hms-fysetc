#pragma once
#include "sdkconfig.h"

// =============================================================================
// hms-fysetc -- Configuration (from Kconfig / idf.py menuconfig)
// =============================================================================

// -- WiFi --
#define WIFI_SSID               CONFIG_FYSETC_WIFI_SSID
#define WIFI_PASSWORD           CONFIG_FYSETC_WIFI_PASSWORD
#define WIFI_MAX_RETRY          CONFIG_FYSETC_WIFI_MAX_RETRY

// -- SD Bus Monitor --
#define PCNT_UNIT               PCNT_UNIT_0
#define PCNT_SAMPLE_MS          CONFIG_FYSETC_PCNT_SAMPLE_MS
#define PCNT_GLITCH_FILTER      CONFIG_FYSETC_PCNT_GLITCH_FILTER

// -- Device Identity --
#define LOG_TAG                 CONFIG_FYSETC_LOG_TAG
#define DEVICE_NAME             CONFIG_FYSETC_DEVICE_NAME
#define FIRMWARE_VERSION        CONFIG_FYSETC_FIRMWARE_VERSION
