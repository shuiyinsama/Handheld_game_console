#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    BOARD_BUTTON_BOOT = 0,
    BOARD_BUTTON_KEY0,
    BOARD_BUTTON_KEY1,
    BOARD_BUTTON_KEY2,
    BOARD_BUTTON_KEY3,
    BOARD_BUTTON_COUNT,
} board_button_t;

typedef struct {
    bool pressed[BOARD_BUTTON_COUNT];
    bool changed[BOARD_BUTTON_COUNT];
} board_input_t;

esp_err_t board_init(void);
bool board_input_scan(board_input_t *input);
void board_fill_screen(uint16_t color);
void board_fill_rect(int x0, int y0, int w, int h, uint16_t color);
void board_draw_rgb565_bitmap(int x0, int y0, int w, int h, const uint16_t *pixels);
uint16_t *board_get_back_buffer(void);
void board_mark_dirty_rect(int x0, int y0, int w, int h);
void board_set_frame_sync_enabled(bool enabled);
void board_sync_frame_buffers(void);
void board_begin_frame(void);
void board_end_frame(void);
void board_present(void);
uint16_t board_rgb565(uint8_t r, uint8_t g, uint8_t b);
