#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "game.h"

static const char *TAG = "handheld";

void app_main(void)
{
    ESP_LOGI(TAG, "Booting handheld game console");
    ESP_ERROR_CHECK(board_init());
    game_run();
}
