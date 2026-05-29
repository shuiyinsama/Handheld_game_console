#pragma once

#include "esp_err.h"
#include "gb_core.h"
#include "storage.h"

typedef struct {
    char name[STORAGE_ROM_NAME_MAX];
    storage_loaded_rom_t rom;
    gb_core_t *core;
} gb_player_t;

esp_err_t gb_player_load(gb_player_t *player, const char *rom_name);
void gb_player_unload(gb_player_t *player);
bool gb_player_has_rom(const gb_player_t *player);
const gb_core_t *gb_player_core(const gb_player_t *player);
esp_err_t gb_player_reset_core(gb_player_t *player);
esp_err_t gb_player_save_ram(gb_player_t *player);
void gb_player_step(gb_player_t *player);
void gb_player_run_steps(gb_player_t *player, uint32_t steps);
void gb_player_set_buttons(gb_player_t *player, uint8_t buttons);
