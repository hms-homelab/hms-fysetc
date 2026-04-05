#pragma once

#include <stdbool.h>
#include <stdint.h>

// PCNT-based bus traffic monitor on GPIO 33 (CS_SENSE).
// Uses hardware pulse counter -- zero CPU overhead.

void traffic_monitor_init(void);
void traffic_monitor_start(void);
void traffic_monitor_stop(void);

// Returns consecutive idle time in milliseconds.
uint32_t traffic_monitor_idle_ms(void);

// Returns pulses counted in the last sample window.
uint16_t traffic_monitor_last_pulse_count(void);

// Returns the raw GPIO level of CS_SENSE (0 or 1).
int traffic_monitor_cs_raw_level(void);

// Returns pointer to pulse history ring buffer and its metadata.
const uint16_t *traffic_monitor_get_history(int *count, int *idx);
