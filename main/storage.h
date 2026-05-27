#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define STORAGE_MOUNT_POINT "/sdcard"
#define STORAGE_ROM_NAME_MAX 32
#define STORAGE_ROM_TITLE_MAX 17

typedef struct {
    char title[STORAGE_ROM_TITLE_MAX];
    uint8_t cartridge_type;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    uint8_t cgb_flag;
    bool header_checksum_ok;
} storage_rom_info_t;

typedef struct {
    uint8_t *data;
    size_t size;
    storage_rom_info_t info;
} storage_loaded_rom_t;

esp_err_t storage_mount(void);
void storage_unmount(void);
bool storage_is_mounted(void);
esp_err_t storage_get_usage(size_t *total_kb, size_t *free_kb);
esp_err_t storage_list_roms(char names[][STORAGE_ROM_NAME_MAX], size_t max_names, size_t *out_count);
esp_err_t storage_read_rom_info(const char *name, storage_rom_info_t *info);
esp_err_t storage_load_rom(const char *name, storage_loaded_rom_t *rom);
void storage_free_rom(storage_loaded_rom_t *rom);
