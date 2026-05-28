#include "gb_ppu.h"

#include "board.h"
#include "esp_heap_caps.h"
#include <string.h>

#define IO_BGP  0x47
#define IO_LCDC 0x40
#define IO_OBP0 0x48
#define IO_OBP1 0x49
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

static uint8_t read_tile_pixel(const gb_core_t *core, uint16_t tile_addr, uint8_t x, uint8_t y)
{
    const uint8_t lo = core->vram[(tile_addr + y * 2) & 0x1FFF];
    const uint8_t hi = core->vram[(tile_addr + y * 2 + 1) & 0x1FFF];
    const uint8_t bit = 7 - (x & 0x07);
    return (uint8_t)(((hi >> bit) & 1) << 1 | ((lo >> bit) & 1));
}

static void put_scaled_pixel(uint16_t *frame, int x, int y, uint16_t color)
{
    const int px = x * GB_PPU_PREVIEW_SCALE;
    const int py = y * GB_PPU_PREVIEW_SCALE;
    for (int sy = 0; sy < GB_PPU_PREVIEW_SCALE; sy++) {
        for (int sx = 0; sx < GB_PPU_PREVIEW_SCALE; sx++) {
            frame[(py + sy) * GB_PPU_PREVIEW_W + px + sx] = color;
        }
    }
}

static void draw_sprites(const gb_core_t *core, uint16_t *frame, const uint8_t *bg_ids)
{
    const uint8_t lcdc = core->io[IO_LCDC];
    if ((lcdc & 0x02) == 0) {
        return;
    }

    const bool tall_sprites = (lcdc & 0x04) != 0;
    const int sprite_h = tall_sprites ? 16 : 8;

    for (int sprite = 39; sprite >= 0; sprite--) {
        const uint8_t *oam = &core->oam[sprite * 4];
        const int y0 = (int)oam[0] - 16;
        const int x0 = (int)oam[1] - 8;
        uint8_t tile = oam[2];
        const uint8_t attr = oam[3];
        const bool behind_bg = (attr & 0x80) != 0;
        const bool flip_y = (attr & 0x40) != 0;
        const bool flip_x = (attr & 0x20) != 0;
        const uint8_t obp = (attr & 0x10) ? core->io[IO_OBP1] : core->io[IO_OBP0];

        if (tall_sprites) {
            tile &= 0xFE;
        }

        for (int sy = 0; sy < sprite_h; sy++) {
            const int dst_y = y0 + sy;
            if (dst_y < 0 || dst_y >= GB_PPU_SCREEN_H) {
                continue;
            }

            const int tile_y = flip_y ? (sprite_h - 1 - sy) : sy;
            const uint16_t tile_addr = (uint16_t)tile * 16 + (uint16_t)(tile_y / 8) * 16;
            const uint8_t row = (uint8_t)(tile_y & 0x07);

            for (int sx = 0; sx < 8; sx++) {
                const int dst_x = x0 + sx;
                if (dst_x < 0 || dst_x >= GB_PPU_SCREEN_W) {
                    continue;
                }

                const uint8_t tile_x = (uint8_t)(flip_x ? (7 - sx) : sx);
                const uint8_t color_id = read_tile_pixel(core, tile_addr, tile_x, row);
                if (color_id == 0) {
                    continue;
                }
                if (behind_bg && bg_ids[dst_y * GB_PPU_SCREEN_W + dst_x] != 0) {
                    continue;
                }

                put_scaled_pixel(frame, dst_x, dst_y, dmg_color(map_palette(obp, color_id)));
            }
        }
    }
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
    static uint8_t bg_ids[GB_PPU_SCREEN_W * GB_PPU_SCREEN_H];
    for (int gy = 0; gy < GB_PPU_SCREEN_H; gy++) {
        for (int gx = 0; gx < GB_PPU_SCREEN_W; gx++) {
            const uint8_t color_id = read_bg_pixel(core, (uint8_t)gx, (uint8_t)gy);
            bg_ids[gy * GB_PPU_SCREEN_W + gx] = color_id;
            const uint16_t color = dmg_color(map_palette(bgp, color_id));
            put_scaled_pixel(s_frame, gx, gy, color);
        }
    }

    draw_sprites(core, s_frame, bg_ids);

    board_fill_rect(x - 3, y - 3, GB_PPU_PREVIEW_W + 6, GB_PPU_PREVIEW_H + 6, board_rgb565(92, 108, 92));
    board_draw_rgb565_bitmap(x, y, GB_PPU_PREVIEW_W, GB_PPU_PREVIEW_H, s_frame);
}
