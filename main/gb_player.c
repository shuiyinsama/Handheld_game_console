#include "gb_player.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "gb_player";

static bool cart_has_battery(uint8_t type)
{
    switch (type) {
    case 0x03: /* MBC1 + RAM + BATTERY */
    case 0x06: /* MBC2 + BATTERY */
    case 0x09: /* ROM + RAM + BATTERY */
    case 0x0F: /* MBC3 + TIMER + BATTERY */
    case 0x10: /* MBC3 + TIMER + RAM + BATTERY */
    case 0x13: /* MBC3 + RAM + BATTERY */
    case 0x1B: /* MBC5 + RAM + BATTERY */
    case 0x1E: /* MBC5 + RUMBLE + RAM + BATTERY */
    case 0x22: /* MBC7 + SENSOR + RUMBLE + RAM + BATTERY */
    case 0xFF: /* HuC1 + RAM + BATTERY */
        return true;
    default:
        return false;
    }
}

static size_t cart_ram_size(uint8_t ram_size_code)
{
    switch (ram_size_code) {
    case 0x01: return 2 * 1024;
    case 0x02: return 8 * 1024;
    case 0x03: return 32 * 1024;
    case 0x04: return 128 * 1024;
    case 0x05: return 64 * 1024;
    default: return 0;
    }
}

static size_t player_save_size(const gb_player_t *player)
{
    if (player == NULL || player->core == NULL || !cart_has_battery(player->rom.info.cartridge_type)) {
        return 0;
    }

    size_t size = cart_ram_size(player->rom.info.ram_size_code);
    if (size > sizeof(player->core->eram)) {
        size = sizeof(player->core->eram);
    }
    return size;
}

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

    strlcpy(player->name, rom_name, sizeof(player->name));

    ret = gb_player_reset_core(player);
    if (ret != ESP_OK) {
        gb_player_unload(player);
        return ret;
    }

    return ESP_OK;
}

void gb_player_unload(gb_player_t *player)
{
    if (player == NULL) {
        return;
    }

    (void)gb_player_save_ram(player);
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

    const bool had_core = player->core != NULL;
    if (player->core == NULL) {
        player->core = heap_caps_calloc(1, sizeof(gb_core_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (player->core == NULL) {
            player->core = heap_caps_calloc(1, sizeof(gb_core_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
    }
    if (player->core == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (had_core && player->name[0] != '\0' && player_save_size(player) > 0) {
        (void)gb_player_save_ram(player);
    }

    gb_core_init(player->core, player->rom.data, player->rom.size);
    const size_t save_size = player_save_size(player);
    if (player->name[0] != '\0' && save_size > 0) {
        const esp_err_t load_ret = storage_load_save_ram(player->name, player->core->eram, save_size);
        if (load_ret == ESP_OK) {
            ESP_LOGI(TAG, "Loaded save RAM for %s (%u bytes)", player->name, (unsigned int)save_size);
        } else if (load_ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Save RAM load failed for %s: 0x%x", player->name, load_ret);
        }
    }
    return ESP_OK;
}

esp_err_t gb_player_save_ram(gb_player_t *player)
{
    const size_t save_size = player_save_size(player);
    if (player == NULL || player->name[0] == '\0' || save_size == 0) {
        return ESP_OK;
    }

    const esp_err_t ret = storage_save_ram(player->name, player->core->eram, save_size);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved RAM for %s (%u bytes)", player->name, (unsigned int)save_size);
    } else {
        ESP_LOGW(TAG, "Save RAM write failed for %s: 0x%x", player->name, ret);
    }
    return ret;
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
