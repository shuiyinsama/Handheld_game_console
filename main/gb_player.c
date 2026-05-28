#include "gb_player.h"

#include "esp_heap_caps.h"
#include <string.h>

esp_err_t gb_player_load(gb_player_t *player, const char *rom_name)
{
    if (player == NULL || rom_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    gb_player_unload(player);

    esp_err_t ret = storage_load_rom(rom_name, &player->rom);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gb_player_reset_core(player);
    if (ret != ESP_OK) {
        gb_player_unload(player);
        return ret;
    }

    strlcpy(player->name, rom_name, sizeof(player->name));
    return ESP_OK;
}

void gb_player_unload(gb_player_t *player)
{
    if (player == NULL) {
        return;
    }

    storage_free_rom(&player->rom);
    if (player->core != NULL) {
        heap_caps_free(player->core);
        player->core = NULL;
    }
    player->name[0] = '\0';
}

bool gb_player_has_rom(const gb_player_t *player)
{
    return player != NULL && player->rom.data != NULL && player->rom.size > 0;
}

const gb_core_t *gb_player_core(const gb_player_t *player)
{
    return player != NULL ? player->core : NULL;
}

esp_err_t gb_player_reset_core(gb_player_t *player)
{
    if (!gb_player_has_rom(player)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (player->core == NULL) {
        player->core = heap_caps_calloc(1, sizeof(gb_core_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (player->core == NULL) {
            player->core = heap_caps_calloc(1, sizeof(gb_core_t), MALLOC_CAP_8BIT);
        }
    }
    if (player->core == NULL) {
        return ESP_ERR_NO_MEM;
    }

    gb_core_init(player->core, player->rom.data, player->rom.size);
    return ESP_OK;
}

void gb_player_step(gb_player_t *player)
{
    if (player != NULL && player->core != NULL) {
        gb_core_step(player->core);
    }
}

void gb_player_run_steps(gb_player_t *player, uint32_t steps)
{
    if (player != NULL && player->core != NULL) {
        gb_core_run(player->core, steps);
    }
}

void gb_player_set_buttons(gb_player_t *player, uint8_t buttons)
{
    if (player != NULL && player->core != NULL) {
        gb_core_set_buttons(player->core, buttons);
    }
}
