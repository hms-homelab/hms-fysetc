#include "sd_manager.h"
#include "pins_config.h"
#include "config.h"

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "sd_mgr";

static sd_owner_t s_owner = SD_OWNER_HOST;
static bool s_mounted = false;
static bool s_host_initialized = false;
static sdmmc_card_t *s_card = NULL;

#define MOUNT_POINT "/sdcard"

void sd_manager_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SD_SWITCH_PIN) | (1ULL << SD_POWER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Power on the NAND
    gpio_set_level(SD_POWER_PIN, SD_POWER_ON);

    // Give bus to host
    gpio_set_level(SD_SWITCH_PIN, SD_SWITCH_HOST);
    s_owner = SD_OWNER_HOST;

    // CS_SENSE as floating input (no internal pullup).
    // The SD bus has external pullups on the board.
    gpio_config_t cs_conf = {
        .pin_bit_mask = (1ULL << CS_SENSE_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_conf);

    ESP_LOGI(TAG, "SD manager init: bus -> HOST, power ON");
}

bool sd_manager_take_control(void)
{
    if (s_owner == SD_OWNER_ESP) {
        ESP_LOGW(TAG, "Already have control");
        return true;
    }

    ESP_LOGI(TAG, "Taking bus control (GPIO26 -> LOW)");
    gpio_set_level(SD_SWITCH_PIN, SD_SWITCH_ESP);
    s_owner = SD_OWNER_ESP;

    // Brief stabilization delay for MUX switching
    vTaskDelay(pdMS_TO_TICKS(50));

    return true;
}

void sd_manager_release_control(void)
{
    if (s_owner == SD_OWNER_HOST) {
        return;
    }

    if (s_mounted) {
        sd_manager_unmount();
    }

    // Float SDIO data pins before releasing to avoid driving the bus
    gpio_config_t float_conf = {
        .pin_bit_mask = (1ULL << SD_D0_PIN) | (1ULL << SD_D1_PIN) |
                        (1ULL << SD_D2_PIN) | (1ULL << SD_D3_PIN) |
                        (1ULL << SD_CMD_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&float_conf);

    gpio_set_level(SD_SWITCH_PIN, SD_SWITCH_HOST);
    s_owner = SD_OWNER_HOST;

    ESP_LOGI(TAG, "Bus released -> HOST");
}

sd_owner_t sd_manager_owner(void)
{
    return s_owner;
}

bool sd_manager_mount(void)
{
    if (s_mounted) return true;
    if (s_owner != SD_OWNER_ESP) {
        ESP_LOGE(TAG, "Cannot mount: bus not owned by ESP");
        return false;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = SDIO_BUS_WIDTH;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 0,
    };

    if (s_host_initialized) {
        sdmmc_host_deinit();
        s_host_initialized = false;
    }

    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot,
                                             &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return false;
    }

    s_host_initialized = true;
    s_mounted = true;
    ESP_LOGI(TAG, "Mounted %s (%.2f GB)",
             MOUNT_POINT,
             (float)((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) / (1024*1024*1024));
    return true;
}

void sd_manager_unmount(void)
{
    if (!s_mounted) return;

    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;
    ESP_LOGI(TAG, "Unmounted %s", MOUNT_POINT);
}

bool sd_manager_is_mounted(void)
{
    return s_mounted;
}
