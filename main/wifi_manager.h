#pragma once

#include <stdbool.h>

// WiFi station mode manager.
// Connects to home network using SSID/password from Kconfig.

void wifi_manager_init(void);
bool wifi_manager_connect(void);
bool wifi_manager_is_connected(void);
void wifi_manager_disconnect(void);
