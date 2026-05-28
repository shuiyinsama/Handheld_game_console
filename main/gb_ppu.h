#pragma once

#include "gb_core.h"

#define GB_PPU_SCREEN_W 160
#define GB_PPU_SCREEN_H 144
#define GB_PPU_PREVIEW_SCALE 2
#define GB_PPU_PREVIEW_W (GB_PPU_SCREEN_W * GB_PPU_PREVIEW_SCALE)
#define GB_PPU_PREVIEW_H (GB_PPU_SCREEN_H * GB_PPU_PREVIEW_SCALE)

typedef struct {
    uint8_t lcdc;
    uint8_t bgp;
    uint8_t scx;
    uint8_t scy;
    uint16_t tile_data_nonzero;
    uint16_t bg_map_nonzero;
    uint16_t shade_counts[4];
} gb_ppu_stats_t;

void gb_ppu_draw_preview(const gb_core_t *core, int x, int y);
void gb_ppu_get_stats(const gb_core_t *core, gb_ppu_stats_t *stats);
