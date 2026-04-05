#include "traffic_monitor.h"
#include "pins_config.h"
#include "config.h"

#include "driver/gpio.h"
#include "driver/pcnt.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "traffic";

#define HISTORY_SIZE 300  // 300 * 100ms = 30 seconds of history
static uint16_t s_pulse_history[HISTORY_SIZE];
static int s_history_idx = 0;

static uint32_t s_consecutive_idle_ms = 0;
static uint16_t s_last_count = 0;
static esp_timer_handle_t s_sample_timer = NULL;
static bool s_running = false;

static void sample_timer_cb(void *arg)
{
    int16_t count = 0;
    pcnt_get_counter_value(PCNT_UNIT, &count);
    pcnt_counter_clear(PCNT_UNIT);

    uint16_t abs_count = (count < 0) ? (uint16_t)(-count) : (uint16_t)count;

    s_pulse_history[s_history_idx] = abs_count;
    s_history_idx = (s_history_idx + 1) % HISTORY_SIZE;
    s_last_count = abs_count;

    if (abs_count == 0) {
        s_consecutive_idle_ms += PCNT_SAMPLE_MS;
    } else {
        s_consecutive_idle_ms = 0;
    }
}

void traffic_monitor_init(void)
{
    pcnt_config_t pcnt_cfg = {
        .pulse_gpio_num = CS_SENSE_PIN,
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .channel = PCNT_CHANNEL_0,
        .unit = PCNT_UNIT,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_INC,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .counter_h_lim = 32767,
        .counter_l_lim = -32768,
    };
    pcnt_unit_config(&pcnt_cfg);

    pcnt_set_filter_value(PCNT_UNIT, PCNT_GLITCH_FILTER);
    pcnt_filter_enable(PCNT_UNIT);
    pcnt_counter_pause(PCNT_UNIT);
    pcnt_counter_clear(PCNT_UNIT);

    memset(s_pulse_history, 0, sizeof(s_pulse_history));
    s_consecutive_idle_ms = 0;
    s_history_idx = 0;

    // Auto-start monitoring
    pcnt_counter_resume(PCNT_UNIT);

    esp_timer_create_args_t timer_args = {
        .callback = sample_timer_cb,
        .name = "pcnt_sample",
    };
    esp_timer_create(&timer_args, &s_sample_timer);
    esp_timer_start_periodic(s_sample_timer, PCNT_SAMPLE_MS * 1000);
    s_running = true;

    ESP_LOGI(TAG, "PCNT initialized on GPIO %d (sample every %d ms)",
             CS_SENSE_PIN, PCNT_SAMPLE_MS);
}

void traffic_monitor_start(void)
{
    if (s_running) return;

    pcnt_counter_resume(PCNT_UNIT);

    esp_timer_create_args_t timer_args = {
        .callback = sample_timer_cb,
        .name = "pcnt_sample",
    };
    esp_timer_create(&timer_args, &s_sample_timer);
    esp_timer_start_periodic(s_sample_timer, PCNT_SAMPLE_MS * 1000);

    s_running = true;
}

void traffic_monitor_stop(void)
{
    if (!s_running) return;

    esp_timer_stop(s_sample_timer);
    esp_timer_delete(s_sample_timer);
    s_sample_timer = NULL;
    pcnt_counter_pause(PCNT_UNIT);

    s_running = false;
}

uint32_t traffic_monitor_idle_ms(void)
{
    return s_consecutive_idle_ms;
}

uint16_t traffic_monitor_last_pulse_count(void)
{
    return s_last_count;
}

int traffic_monitor_cs_raw_level(void)
{
    return gpio_get_level(CS_SENSE_PIN);
}

const uint16_t *traffic_monitor_get_history(int *count, int *idx)
{
    *count = HISTORY_SIZE;
    *idx = s_history_idx;
    return s_pulse_history;
}
