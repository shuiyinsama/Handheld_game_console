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

typedef struct {
    uint32_t bg_us;
    uint32_t obj_us;
    uint32_t misc_us;
    uint32_t total_us;
    uint8_t bg_cache_hit;
    uint8_t bg_miss_reason;
} gb_ppu_perf_t;

typedef enum {
    GB_PPU_BG_MISS_NONE = 0,
    GB_PPU_BG_MISS_INIT,
    GB_PPU_BG_MISS_SCROLL,
    GB_PPU_BG_MISS_VRAM,
    GB_PPU_BG_MISS_REG,
} gb_ppu_bg_miss_reason_t;

void gb_ppu_draw_preview(const gb_core_t *core, int x, int y);
void gb_ppu_draw_screen_scaled(const gb_core_t *core, int x, int y, int scale);
void gb_ppu_get_stats(const gb_core_t *core, gb_ppu_stats_t *stats);
void gb_ppu_get_last_perf(gb_ppu_perf_t *perf);
