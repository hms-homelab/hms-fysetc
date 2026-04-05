#pragma once

// Start captive portal AP mode with DNS hijack.
// Serves a WiFi setup form at 192.168.4.1.
// Blocks until user saves credentials and device reboots.
void captive_portal_start(void);
