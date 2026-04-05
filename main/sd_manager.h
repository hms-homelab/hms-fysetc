#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// SD bus MUX manager for FYSETC SD WiFi Pro.
// Controls GPIO 26 (bus switch) and GPIO 27 (power).
// Handles mount/unmount of the onboard SD NAND via SDMMC.

typedef enum {
    SD_OWNER_HOST,      // Host device owns the bus (GPIO 26 HIGH)
    SD_OWNER_ESP,       // ESP32 owns the bus (GPIO 26 LOW)
} sd_owner_t;

// Initialize GPIO pins and set bus to HOST mode.
// MUST be called as early as possible in app_main().
void sd_manager_init(void);

// Switch bus to ESP32. Returns true on success.
bool sd_manager_take_control(void);

// Release bus back to host device. Unmounts if mounted.
void sd_manager_release_control(void);

// Current owner of the bus.
sd_owner_t sd_manager_owner(void);

// Mount/unmount SD filesystem (bus must be owned by ESP).
bool sd_manager_mount(void);
void sd_manager_unmount(void);
bool sd_manager_is_mounted(void);
