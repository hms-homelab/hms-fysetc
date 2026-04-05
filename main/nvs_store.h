#pragma once

#include <stdbool.h>
#include <stddef.h>

// NVS storage for WiFi credentials.
// Saved via captive portal, persists across reboots.

bool nvs_store_has_wifi(void);
bool nvs_store_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
bool nvs_store_set_wifi(const char *ssid, const char *pass);
void nvs_store_clear_wifi(void);
