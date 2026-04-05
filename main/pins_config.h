#pragma once

// =============================================================================
// FYSETC SD WiFi Pro (SWP) -- GPIO Pin Assignments
// Hardware: ESP32-PICO-D4, 8GB SD NAND, MUX bus switch
// Reference: FYSETC schematic + amanuense/CPAP_data_uploader
// =============================================================================

// -- MUX Control --
// GPIO 26 controls the analog MUX that switches the SD bus between
// the host device and the ESP32. This is THE critical pin.
#define SD_SWITCH_PIN       GPIO_NUM_26
#define SD_SWITCH_ESP       0   // LOW  = ESP32 owns the SD bus
#define SD_SWITCH_HOST      1   // HIGH = Host device owns the SD bus

// -- CS Sense (Host Activity Detection) --
// GPIO 33 is tapped on the HOST side of the MUX (upstream of the switch).
// Even when ESP32 owns the bus, GPIO 33 still sees host CS assertions.
#define CS_SENSE_PIN        GPIO_NUM_33

// -- SD Card Power --
// GPIO 27 controls power to the onboard 8GB NAND flash
#define SD_POWER_PIN        GPIO_NUM_27
#define SD_POWER_ON         1   // HIGH = powered
#define SD_POWER_OFF        0   // LOW  = off

// -- SDIO Bus (4-bit mode, fixed by ESP32-PICO-D4 wiring) --
#define SD_CMD_PIN          GPIO_NUM_15
#define SD_CLK_PIN          GPIO_NUM_14
#define SD_D0_PIN           GPIO_NUM_2
#define SD_D1_PIN           GPIO_NUM_4
#define SD_D2_PIN           GPIO_NUM_12
#define SD_D3_PIN           GPIO_NUM_13

// -- SDIO Mode --
#define SDIO_BUS_WIDTH      4
