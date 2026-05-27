#include "game.h"

#include "board.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "game";

#define BUTTON_TEST_MARGIN 48
#define BUTTON_TEST_GAP    16
#define BUTTON_TEST_COUNT  8
#define BUTTON_TEST_BOX_H  92

#define PLAYFIELD_X        48
#define PLAYFIELD_Y        36
#define PLAYFIELD_W        (BOARD_LCD_H_RES - PLAYFIELD_X * 2)
#define PLAYFIELD_H        260
#define PLAYER_SIZE        28
#define PLAYER_SPEED       6
#define TARGET_SIZE        20
#define SCORE_MAX          10

typedef struct {
    int x;
    int y;
    int last_x;
    int last_y;
    bool last_action;
} player_t;

typedef struct {
    int x;
    int y;
} target_t;

typedef struct {
    player_t player;
    target_t target;
    uint8_t score;
    uint32_t rng;
} game_state_t;

static int clamp_int(int value, int min, int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static uint32_t next_random(game_state_t *state)
{
    state->rng = state->rng * 1664525U + 1013904223U;
    return state->rng;
}

static void place_target(game_state_t *state)
{
    const int x_range = PLAYFIELD_W - TARGET_SIZE;
    const int y_range = PLAYFIELD_H - TARGET_SIZE;

    state->target.x = PLAYFIELD_X + (int)(next_random(state) % x_range);
    state->target.y = PLAYFIELD_Y + (int)(next_random(state) % y_range);
}

static void reset_game(game_state_t *state)
{
    state->player.x = PLAYFIELD_X + (PLAYFIELD_W - PLAYER_SIZE) / 2;
    state->player.y = PLAYFIELD_Y + (PLAYFIELD_H - PLAYER_SIZE) / 2;
    state->player.last_x = state->player.x;
    state->player.last_y = state->player.y;
    state->player.last_action = false;
    state->score = 0;
    place_target(state);
}

static int button_test_box_w(void)
{
    return (BOARD_LCD_H_RES - BUTTON_TEST_MARGIN * 2 - BUTTON_TEST_GAP * (BUTTON_TEST_COUNT - 1)) /
           BUTTON_TEST_COUNT;
}

static int button_test_y(void)
{
    return BOARD_LCD_V_RES - BUTTON_TEST_BOX_H - 36;
}

static void draw_button_slot(const board_input_t *input, size_t index)
{
    const int box_w = button_test_box_w();
    const int y = button_test_y();
    const int x = BUTTON_TEST_MARGIN + (int)index * (box_w + BUTTON_TEST_GAP);

    const uint16_t color = input->pressed[index] ? board_rgb565(80, 220, 120) : board_rgb565(82, 96, 108);
    board_fill_rect(x + 4, y + 4, box_w - 8, BUTTON_TEST_BOX_H - 8, color);

    board_fill_rect(x + 18, y + 18, box_w - 36, 10, board_rgb565(10, 14, 18));
    board_fill_rect(x + 18, y + BUTTON_TEST_BOX_H - 28, box_w - 36, 10, board_rgb565(10, 14, 18));
}

static void draw_player(const board_input_t *input, const player_t *player)
{
    const uint16_t body = input->pressed[BOARD_BUTTON_BOOT] ? board_rgb565(235, 220, 92) : board_rgb565(76, 170, 235);
    board_fill_rect(player->x, player->y, PLAYER_SIZE, PLAYER_SIZE, body);
    board_fill_rect(player->x + 6, player->y + 7, 5, 8, board_rgb565(8, 12, 16));
    board_fill_rect(player->x + PLAYER_SIZE - 11, player->y + 7, 5, 8, board_rgb565(8, 12, 16));
}

static void erase_player(const player_t *player)
{
    board_fill_rect(player->last_x, player->last_y, PLAYER_SIZE, PLAYER_SIZE, board_rgb565(28, 34, 40));
}

static void draw_target(const target_t *target)
{
    board_fill_rect(target->x, target->y, TARGET_SIZE, TARGET_SIZE, board_rgb565(236, 92, 92));
    board_fill_rect(target->x + 5, target->y + 5, TARGET_SIZE - 10, TARGET_SIZE - 10, board_rgb565(255, 210, 96));
}

static void draw_score(uint8_t score)
{
    const int pip_size = 18;
    const int gap = 8;
    const int x0 = PLAYFIELD_X;
    const int y0 = PLAYFIELD_Y + PLAYFIELD_H + 14;

    board_fill_rect(x0, y0, SCORE_MAX * (pip_size + gap), pip_size, board_rgb565(20, 24, 28));

    for (uint8_t i = 0; i < SCORE_MAX; i++) {
        const uint16_t color = i < score ? board_rgb565(235, 220, 92) : board_rgb565(58, 66, 74);
        board_fill_rect(x0 + i * (pip_size + gap), y0, pip_size, pip_size, color);
    }
}

static bool update_player(const board_input_t *input, player_t *player)
{
    player->last_x = player->x;
    player->last_y = player->y;

    if (input->pressed[BOARD_BUTTON_KEY2]) {
        player->x -= PLAYER_SPEED;
    }
    if (input->pressed[BOARD_BUTTON_KEY0]) {
        player->x += PLAYER_SPEED;
    }
    if (input->pressed[BOARD_BUTTON_KEY3]) {
        player->y -= PLAYER_SPEED;
    }
    if (input->pressed[BOARD_BUTTON_KEY1]) {
        player->y += PLAYER_SPEED;
    }

    player->x = clamp_int(player->x, PLAYFIELD_X, PLAYFIELD_X + PLAYFIELD_W - PLAYER_SIZE);
    player->y = clamp_int(player->y, PLAYFIELD_Y, PLAYFIELD_Y + PLAYFIELD_H - PLAYER_SIZE);

    const bool action_changed = input->pressed[BOARD_BUTTON_BOOT] != player->last_action;
    player->last_action = input->pressed[BOARD_BUTTON_BOOT];

    return player->x != player->last_x || player->y != player->last_y || action_changed;
}

static bool player_hits_target(const player_t *player, const target_t *target)
{
    return player->x < target->x + TARGET_SIZE &&
           player->x + PLAYER_SIZE > target->x &&
           player->y < target->y + TARGET_SIZE &&
           player->y + PLAYER_SIZE > target->y;
}

static void draw_game_screen(const board_input_t *input, const game_state_t *state)
{
    board_fill_screen(board_rgb565(20, 24, 28));

    board_fill_rect(PLAYFIELD_X - 4, PLAYFIELD_Y - 4, PLAYFIELD_W + 8, PLAYFIELD_H + 8, board_rgb565(76, 86, 96));
    board_fill_rect(PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H, board_rgb565(28, 34, 40));

    const int box_w = button_test_box_w();
    const int y = button_test_y();
    for (int i = 0; i < BUTTON_TEST_COUNT; i++) {
        const int x = BUTTON_TEST_MARGIN + i * (box_w + BUTTON_TEST_GAP);
        board_fill_rect(x, y, box_w, BUTTON_TEST_BOX_H, board_rgb565(48, 56, 64));
        board_fill_rect(x + 4, y + 4, box_w - 8, BUTTON_TEST_BOX_H - 8, board_rgb565(32, 38, 44));
    }

    for (size_t i = 0; i < BOARD_BUTTON_COUNT; i++) {
        draw_button_slot(input, i);
    }

    draw_score(state->score);
    draw_target(&state->target);
    draw_player(input, &state->player);
}

void game_run(void)
{
    board_input_t input = {0};
    game_state_t state = {
        .rng = 0x1234ABCD,
    };
    reset_game(&state);

    board_input_scan(&input);
    draw_game_screen(&input, &state);
    ESP_LOGI(TAG, "Collect game loop started");

    while (true) {
        const bool input_changed = board_input_scan(&input);
        if (input_changed) {
            if (input.changed[BOARD_BUTTON_BOOT] && input.pressed[BOARD_BUTTON_BOOT]) {
                reset_game(&state);
                draw_game_screen(&input, &state);
                vTaskDelay(pdMS_TO_TICKS(33));
                continue;
            }

            for (size_t i = 0; i < BOARD_BUTTON_COUNT; i++) {
                if (input.changed[i]) {
                    draw_button_slot(&input, i);
                }
            }
        }

        if (update_player(&input, &state.player)) {
            if (player_hits_target(&state.player, &state.target)) {
                if (state.score < SCORE_MAX) {
                    state.score++;
                }
                place_target(&state);
                draw_game_screen(&input, &state);
            } else {
                erase_player(&state.player);
                draw_target(&state.target);
                draw_player(&input, &state.player);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
