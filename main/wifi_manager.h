#pragma once

#include <stdbool.h>

// WiFi station mode manager.

void wifi_manager_init(void);

// Connect using compile-time Kconfig credentials.
bool wifi_manager_connect(void);

// Connect using runtime credentials (from NVS / captive portal).
bool wifi_manager_connect_dynamic(const char *ssid, const char *pass);

bool wifi_manager_is_connected(void);
void wifi_manager_disconnect(void);
