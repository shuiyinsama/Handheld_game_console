#include "gb_ppu.h"

#include "board.h"
#include "esp_heap_caps.h"
#include <string.h>

#define IO_BGP  0x47
#define IO_LCDC 0x40
#define IO_SCY  0x42
#define IO_SCX  0x43

static uint16_t dmg_color(uint8_t shade)
{
    switch (shade & 0x03) {
    case 0: return board_rgb565(224, 232, 184);
    case 1: return board_rgb565(140, 160, 112);
    case 2: return board_rgb565(72, 88, 72);
    default: return board_rgb565(24, 32, 28);
    }
}

static uint8_t map_palette(uint8_t bgp, uint8_t color_id)
{
    return (bgp >> (color_id * 2)) & 0x03;
}

static uint8_t read_bg_pixel(const gb_core_t *core, uint8_t x, uint8_t y)
{
    const uint8_t lcdc = core->io[IO_LCDC];
    if ((lcdc & 0x01) == 0) {
        return 0;
    }

    const uint8_t scx = core->io[IO_SCX];
    const uint8_t scy = core->io[IO_SCY];
    const uint8_t bg_x = (uint8_t)(x + scx);
    const uint8_t bg_y = (uint8_t)(y + scy);
    const uint16_t map_base = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    const uint16_t tile_map_offset = map_base + (bg_y / 8) * 32 + (bg_x / 8);
    const uint8_t tile_id = core->vram[tile_map_offset & 0x1FFF];
    const uint8_t tile_row = bg_y & 0x07;

    uint16_t tile_addr = 0;
    if ((lcdc & 0x10) != 0) {
        tile_addr = (uint16_t)tile_id * 16 + tile_row * 2;
    } else {
        tile_addr = (uint16_t)(0x1000 + (int16_t)(int8_t)tile_id * 16 + tile_row * 2);
    }

    const uint8_t lo = core->vram[tile_addr & 0x1FFF];
    const uint8_t hi = core->vram[(tile_addr + 1) & 0x1FFF];
    const uint8_t bit = 7 - (bg_x & 0x07);
    return (uint8_t)(((hi >> bit) & 1) << 1 | ((lo >> bit) & 1));
}

void gb_ppu_get_stats(const gb_core_t *core, gb_ppu_stats_t *stats)
{
    if (core == NULL || stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    stats->lcdc = core->io[IO_LCDC];
    stats->bgp = core->io[IO_BGP];
    stats->scx = core->io[IO_SCX];
    stats->scy = core->io[IO_SCY];

    for (uint16_t i = 0; i < 0x1800; i++) {
        if (core->vram[i] != 0) {
            stats->tile_data_nonzero++;
        }
    }

    for (uint16_t i = 0x1800; i < 0x2000; i++) {
        if (core->vram[i] != 0) {
            stats->bg_map_nonzero++;
        }
    }

    for (int gy = 0; gy < GB_PPU_SCREEN_H; gy++) {
        for (int gx = 0; gx < GB_PPU_SCREEN_W; gx++) {
            const uint8_t color_id = read_bg_pixel(core, (uint8_t)gx, (uint8_t)gy);
            stats->shade_counts[color_id]++;
        }
    }
}

void gb_ppu_draw_preview(const gb_core_t *core, int x, int y)
{
    if (core == NULL) {
        return;
    }

    const size_t pixel_count = GB_PPU_PREVIEW_W * GB_PPU_PREVIEW_H;
    static uint16_t *s_frame = NULL;
    if (s_frame == NULL) {
        s_frame = heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_frame == NULL) {
        s_frame = heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_8BIT);
    }
    if (s_frame == NULL) {
        return;
    }

    const uint8_t bgp = core->io[IO_BGP];
    for (int gy = 0; gy < GB_PPU_SCREEN_H; gy++) {
        for (int gx = 0; gx < GB_PPU_SCREEN_W; gx++) {
            const uint8_t color_id = read_bg_pixel(core, (uint8_t)gx, (uint8_t)gy);
            const uint16_t color = dmg_color(map_palette(bgp, color_id));
            const int px = gx * GB_PPU_PREVIEW_SCALE;
            const int py = gy * GB_PPU_PREVIEW_SCALE;
            for (int sy = 0; sy < GB_PPU_PREVIEW_SCALE; sy++) {
                for (int sx = 0; sx < GB_PPU_PREVIEW_SCALE; sx++) {
                    s_frame[(py + sy) * GB_PPU_PREVIEW_W + px + sx] = color;
                }
            }
        }
    }

    board_fill_rect(x - 3, y - 3, GB_PPU_PREVIEW_W + 6, GB_PPU_PREVIEW_H + 6, board_rgb565(92, 108, 92));
    board_draw_rgb565_bitmap(x, y, GB_PPU_PREVIEW_W, GB_PPU_PREVIEW_H, s_frame);
}
