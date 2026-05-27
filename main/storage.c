#include "storage.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include "board_config.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage";

static sdmmc_card_t *s_card;
static bool s_bus_ready;
static bool s_mounted;

static bool ends_with_ignore_case(const char *name, const char *suffix)
{
    const size_t name_len = strlen(name);
    const size_t suffix_len = strlen(suffix);
    if (name_len < suffix_len) {
        return false;
    }

    const char *tail = name + name_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        char a = tail[i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static bool is_rom_file(const char *name)
{
    return ends_with_ignore_case(name, ".gb") || ends_with_ignore_case(name, ".gbc");
}

esp_err_t storage_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    if (!s_bus_ready) {
        spi_bus_config_t bus_config = {
            .mosi_io_num = BOARD_SD_SPI_MOSI_GPIO,
            .miso_io_num = BOARD_SD_SPI_MISO_GPIO,
            .sclk_io_num = BOARD_SD_SPI_CLK_GPIO,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };

        esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(ret, TAG, "SPI bus init failed");
        }
        s_bus_ready = true;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 4 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = SPI2_HOST;
    slot_config.gpio_cs = BOARD_SD_SPI_CS_GPIO;

    esp_err_t ret = esp_vfs_fat_sdspi_mount(STORAGE_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret == ESP_OK) {
        s_mounted = true;
    }
    return ret;
}

void storage_unmount(void)
{
    if (s_mounted) {
        esp_vfs_fat_sdcard_unmount(STORAGE_MOUNT_POINT, s_card);
        s_card = NULL;
        s_mounted = false;
    }
}

bool storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t storage_get_usage(size_t *total_kb, size_t *free_kb)
{
    ESP_RETURN_ON_ERROR(storage_mount(), TAG, "SD mount failed");

    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    FRESULT res = f_getfree(STORAGE_MOUNT_POINT, &free_clusters, &fs);
    if (res != FR_OK || fs == NULL) {
        return ESP_FAIL;
    }

    const size_t total_sectors = (size_t)(fs->n_fatent - 2) * fs->csize;
    const size_t free_sectors = (size_t)free_clusters * fs->csize;
    if (total_kb) {
        *total_kb = total_sectors * fs->ssize / 1024;
    }
    if (free_kb) {
        *free_kb = free_sectors * fs->ssize / 1024;
    }
    return ESP_OK;
}

esp_err_t storage_list_roms(char names[][STORAGE_ROM_NAME_MAX], size_t max_names, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }

    ESP_RETURN_ON_ERROR(storage_mount(), TAG, "SD mount failed");

    DIR *dir = opendir(STORAGE_MOUNT_POINT);
    if (dir == NULL) {
        return ESP_FAIL;
    }

    size_t count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL && count < max_names) {
        if (!is_rom_file(entry->d_name)) {
            continue;
        }
        strlcpy(names[count], entry->d_name, STORAGE_ROM_NAME_MAX);
        count++;
    }

    closedir(dir);
    if (out_count) {
        *out_count = count;
    }
    return ESP_OK;
}

esp_err_t storage_read_rom_info(const char *name, storage_rom_info_t *info)
{
    if (name == NULL || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(storage_mount(), TAG, "SD mount failed");

    char path[sizeof(STORAGE_MOUNT_POINT) + STORAGE_ROM_NAME_MAX + 2] = {0};
    snprintf(path, sizeof(path), "%s/%s", STORAGE_MOUNT_POINT, name);

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_FAIL;
    }

    uint8_t header[0x150] = {0};
    const size_t read_len = fread(header, 1, sizeof(header), file);
    fclose(file);

    if (read_len < sizeof(header)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(info, 0, sizeof(*info));
    for (size_t i = 0; i < STORAGE_ROM_TITLE_MAX - 1; i++) {
        const uint8_t ch = header[0x134 + i];
        if (ch == 0 || ch < 32 || ch > 126) {
            break;
        }
        info->title[i] = (char)ch;
    }
    if (info->title[0] == '\0') {
        strlcpy(info->title, "UNKNOWN", sizeof(info->title));
    }

    info->cartridge_type = header[0x147];
    info->rom_size_code = header[0x148];
    info->ram_size_code = header[0x149];
    info->cgb_flag = header[0x143];

    uint8_t checksum = 0;
    for (size_t addr = 0x134; addr <= 0x14C; addr++) {
        checksum = (uint8_t)(checksum - header[addr] - 1);
    }
    info->header_checksum_ok = checksum == header[0x14D];

    return ESP_OK;
}
