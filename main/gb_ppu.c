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
#define IO_WY   0x4A
#define IO_WX   0x4B

typedef struct {
    uint8_t color_id;
    uint8_t attr;
    uint16_t color;
} gb_pixel_t;

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

static uint16_t cgb_color(uint8_t lo, uint8_t hi)
{
    const uint16_t value = (uint16_t)lo | ((uint16_t)hi << 8);
    const uint8_t r = (uint8_t)((value & 0x1F) * 255 / 31);
    const uint8_t g = (uint8_t)(((value >> 5) & 0x1F) * 255 / 31);
    const uint8_t b = (uint8_t)(((value >> 10) & 0x1F) * 255 / 31);
    return board_rgb565(r, g, b);
}

static uint16_t cgb_palette_color(const gb_core_t *core, bool object, uint8_t palette, uint8_t color_id)
{
    const uint8_t *data = object ? core->obj_palette : core->bg_palette;
    const uint8_t index = (uint8_t)(((palette & 0x07) * 8 + (color_id & 0x03) * 2) & 0x3F);
    return cgb_color(data[index], data[(index + 1) & 0x3F]);
}

static uint8_t read_tile_pixel(const gb_core_t *core, uint8_t bank, uint16_t tile_addr, uint8_t x, uint8_t y)
{
    const uint8_t lo = core->vram[bank & 0x01][(tile_addr + y * 2) & 0x1FFF];
    const uint8_t hi = core->vram[bank & 0x01][(tile_addr + y * 2 + 1) & 0x1FFF];
    const uint8_t bit = 7 - (x & 0x07);
    return (uint8_t)(((hi >> bit) & 1) << 1 | ((lo >> bit) & 1));
}

static gb_pixel_t read_bg_or_window_pixel(const gb_core_t *core, uint8_t x, uint8_t y)
{
    const uint8_t lcdc = core->io[IO_LCDC];
    if ((lcdc & 0x01) == 0) {
        return (gb_pixel_t){.color_id = 0, .attr = 0, .color = core->cgb_mode ? cgb_palette_color(core, false, 0, 0) : dmg_color(0)};
    }

    const int wx = (int)core->io[IO_WX] - 7;
    const int wy = core->io[IO_WY];
    const bool window =
        (lcdc & 0x20) != 0 &&
        (int)x >= wx &&
        (int)y >= wy &&
        wx < GB_PPU_SCREEN_W &&
        wy < GB_PPU_SCREEN_H;

    const uint8_t scx = core->io[IO_SCX];
    const uint8_t scy = core->io[IO_SCY];
    const uint8_t map_x = window ? (uint8_t)((int)x - wx) : (uint8_t)(x + scx);
    const uint8_t map_y = window ? (uint8_t)((int)y - wy) : (uint8_t)(y + scy);
    const uint16_t map_base = window ? ((lcdc & 0x40) ? 0x1C00 : 0x1800) : ((lcdc & 0x08) ? 0x1C00 : 0x1800);
    const uint16_t tile_map_offset = map_base + (map_y / 8) * 32 + (map_x / 8);
    const uint8_t tile_id = core->vram[0][tile_map_offset & 0x1FFF];
    const uint8_t attr = core->cgb_mode ? core->vram[1][tile_map_offset & 0x1FFF] : 0;
    const bool flip_x = (attr & 0x20) != 0;
    const bool flip_y = (attr & 0x40) != 0;
    const uint8_t tile_x = flip_x ? (uint8_t)(7 - (map_x & 0x07)) : (uint8_t)(map_x & 0x07);
    const uint8_t tile_row = flip_y ? (uint8_t)(7 - (map_y & 0x07)) : (uint8_t)(map_y & 0x07);
    const uint8_t bank = core->cgb_mode ? (uint8_t)((attr >> 3) & 0x01) : 0;

    uint16_t tile_addr = 0;
    if ((lcdc & 0x10) != 0) {
        tile_addr = (uint16_t)tile_id * 16 + tile_row * 2;
    } else {
        tile_addr = (uint16_t)(0x1000 + (int16_t)(int8_t)tile_id * 16 + tile_row * 2);
    }

    const uint8_t color_id = read_tile_pixel(core, bank, tile_addr, tile_x, 0);
    const uint16_t color = core->cgb_mode ? cgb_palette_color(core, false, attr & 0x07, color_id) : dmg_color(map_palette(core->io[IO_BGP], color_id));
    return (gb_pixel_t){.color_id = color_id, .attr = attr, .color = color};
}

static void put_scaled_pixel(uint16_t *frame, int frame_w, int scale, int x, int y, uint16_t color)
{
    const int px = x * scale;
    const int py = y * scale;
    for (int sy = 0; sy < scale; sy++) {
        for (int sx = 0; sx < scale; sx++) {
            frame[(py + sy) * frame_w + px + sx] = color;
        }
    }
}

static void put_scaled_pixel3(uint16_t *frame, int frame_w, int x, int y, uint16_t color)
{
    const int px = x * 3;
    const int py = y * 3;
    uint16_t *row0 = &frame[py * frame_w + px];
    uint16_t *row1 = row0 + frame_w;
    uint16_t *row2 = row1 + frame_w;

    row0[0] = color;
    row0[1] = color;
    row0[2] = color;
    row1[0] = color;
    row1[1] = color;
    row1[2] = color;
    row2[0] = color;
    row2[1] = color;
    row2[2] = color;
}

static void draw_dmg_background_scaled3(const gb_core_t *core, uint16_t *frame, int frame_w, uint8_t *bg_ids)
{
    const uint8_t bgp = core->io[IO_BGP];
    const uint8_t lcdc = core->io[IO_LCDC];
    const uint16_t palette[4] = {
        dmg_color(map_palette(bgp, 0)),
        dmg_color(map_palette(bgp, 1)),
        dmg_color(map_palette(bgp, 2)),
        dmg_color(map_palette(bgp, 3)),
    };

    if ((lcdc & 0x01) == 0) {
        const uint16_t color = palette[0];
        memset(bg_ids, 0, GB_PPU_SCREEN_W * GB_PPU_SCREEN_H);
        for (int gy = 0; gy < GB_PPU_SCREEN_H; gy++) {
            uint16_t *row0 = &frame[(gy * 3) * frame_w];
            uint16_t *row1 = row0 + frame_w;
            uint16_t *row2 = row1 + frame_w;
            for (int px = 0; px < frame_w; px++) {
                row0[px] = color;
                row1[px] = color;
                row2[px] = color;
            }
        }
        return;
    }

    const uint16_t bg_map_base = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    const uint16_t win_map_base = (lcdc & 0x40) ? 0x1C00 : 0x1800;
    const bool unsigned_tiles = (lcdc & 0x10) != 0;
    const int wx = (int)core->io[IO_WX] - 7;
    const int wy = core->io[IO_WY];
    const bool window_enabled =
        (lcdc & 0x20) != 0 &&
        wx < GB_PPU_SCREEN_W &&
        wy < GB_PPU_SCREEN_H;

    for (int gy = 0; gy < GB_PPU_SCREEN_H; gy++) {
        uint16_t *row0 = &frame[(gy * 3) * frame_w];
        uint16_t *row1 = row0 + frame_w;
        uint16_t *row2 = row1 + frame_w;
        uint8_t *bg_row = &bg_ids[gy * GB_PPU_SCREEN_W];
        const bool window_row = window_enabled && gy >= wy;
        uint8_t lo = 0;
        uint8_t hi = 0;
        uint16_t last_tile_key = 0xFFFF;

        for (int gx = 0; gx < GB_PPU_SCREEN_W; gx++) {
            const bool window_pixel = window_row && gx >= wx;
            const uint8_t map_x = window_pixel ? (uint8_t)(gx - wx) : (uint8_t)(gx + core->io[IO_SCX]);
            const uint8_t map_y = window_pixel ? (uint8_t)(gy - wy) : (uint8_t)(gy + core->io[IO_SCY]);
            const uint16_t map_base = window_pixel ? win_map_base : bg_map_base;
            const uint16_t tile_key = (uint16_t)(map_base + (map_y / 8) * 32 + (map_x / 8));

            if (tile_key != last_tile_key) {
                const uint8_t tile_id = core->vram[0][tile_key & 0x1FFF];
                const uint8_t tile_row = map_y & 0x07;
                uint16_t tile_addr = 0;
                if (unsigned_tiles) {
                    tile_addr = (uint16_t)tile_id * 16 + tile_row * 2;
                } else {
                    tile_addr = (uint16_t)(0x1000 + (int16_t)(int8_t)tile_id * 16 + tile_row * 2);
                }
                lo = core->vram[0][tile_addr & 0x1FFF];
                hi = core->vram[0][(tile_addr + 1) & 0x1FFF];
                last_tile_key = tile_key;
            }

            const uint8_t bit = 7 - (map_x & 0x07);
            const uint8_t color_id = (uint8_t)(((hi >> bit) & 1) << 1 | ((lo >> bit) & 1));
            const uint16_t color = palette[color_id & 0x03];
            const int px = gx * 3;
            bg_row[gx] = color_id;

            row0[px] = color;
            row0[px + 1] = color;
            row0[px + 2] = color;
            row1[px] = color;
            row1[px + 1] = color;
            row1[px + 2] = color;
            row2[px] = color;
            row2[px + 1] = color;
            row2[px + 2] = color;
        }
    }
}

static void draw_sprites(const gb_core_t *core, uint16_t *frame, int frame_w, int scale, const uint8_t *bg_ids)
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
        const uint8_t cgb_palette = attr & 0x07;
        const uint8_t bank = core->cgb_mode ? (uint8_t)((attr >> 3) & 0x01) : 0;

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
                const uint8_t color_id = read_tile_pixel(core, bank, tile_addr, tile_x, row);
                if (color_id == 0) {
                    continue;
                }
                if (behind_bg && bg_ids[dst_y * GB_PPU_SCREEN_W + dst_x] != 0) {
                    continue;
                }

                const uint16_t color = core->cgb_mode ? cgb_palette_color(core, true, cgb_palette, color_id) : dmg_color(map_palette(obp, color_id));
                if (!core->cgb_mode && scale == 3) {
                    put_scaled_pixel3(frame, frame_w, dst_x, dst_y, color);
                } else {
                    put_scaled_pixel(frame, frame_w, scale, dst_x, dst_y, color);
                }
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
        if (core->vram[0][i] != 0 || core->vram[1][i] != 0) {
            stats->tile_data_nonzero++;
        }
    }

    for (uint16_t i = 0x1800; i < 0x2000; i++) {
        if (core->vram[0][i] != 0 || core->vram[1][i] != 0) {
            stats->bg_map_nonzero++;
        }
    }

    for (int gy = 0; gy < GB_PPU_SCREEN_H; gy++) {
        for (int gx = 0; gx < GB_PPU_SCREEN_W; gx++) {
            const uint8_t color_id = read_bg_or_window_pixel(core, (uint8_t)gx, (uint8_t)gy).color_id;
            stats->shade_counts[color_id]++;
        }
    }
}

void gb_ppu_draw_screen_scaled(const gb_core_t *core, int x, int y, int scale)
{
    if (core == NULL || scale <= 0 || scale > 4) {
        return;
    }

    const int frame_w = GB_PPU_SCREEN_W * scale;
    const int frame_h = GB_PPU_SCREEN_H * scale;
    const size_t pixel_count = (size_t)frame_w * frame_h;
    static uint16_t *s_frame = NULL;
    static size_t s_frame_capacity = 0;
    if (s_frame == NULL || s_frame_capacity < pixel_count) {
        if (s_frame != NULL) {
            heap_caps_free(s_frame);
            s_frame = NULL;
            s_frame_capacity = 0;
        }
        s_frame = heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_frame != NULL) {
            s_frame_capacity = pixel_count;
        }
    }
    if (s_frame == NULL) {
        s_frame = heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_8BIT);
        if (s_frame != NULL) {
            s_frame_capacity = pixel_count;
        }
    }
    if (s_frame == NULL) {
        return;
    }

    static uint8_t bg_ids[GB_PPU_SCREEN_W * GB_PPU_SCREEN_H];
    if (!core->cgb_mode && scale == 3) {
        draw_dmg_background_scaled3(core, s_frame, frame_w, bg_ids);
    } else {
        for (int gy = 0; gy < GB_PPU_SCREEN_H; gy++) {
            for (int gx = 0; gx < GB_PPU_SCREEN_W; gx++) {
                const gb_pixel_t pixel = read_bg_or_window_pixel(core, (uint8_t)gx, (uint8_t)gy);
                bg_ids[gy * GB_PPU_SCREEN_W + gx] = pixel.color_id;
                put_scaled_pixel(s_frame, frame_w, scale, gx, gy, pixel.color);
            }
        }
    }

    draw_sprites(core, s_frame, frame_w, scale, bg_ids);

    const uint16_t border = board_rgb565(92, 108, 92);
    board_fill_rect(x - 3, y - 3, frame_w + 6, 3, border);
    board_fill_rect(x - 3, y + frame_h, frame_w + 6, 3, border);
    board_fill_rect(x - 3, y, 3, frame_h, border);
    board_fill_rect(x + frame_w, y, 3, frame_h, border);
    board_draw_rgb565_bitmap(x, y, frame_w, frame_h, s_frame);
}

void gb_ppu_draw_preview(const gb_core_t *core, int x, int y)
{
    gb_ppu_draw_screen_scaled(core, x, y, GB_PPU_PREVIEW_SCALE);
}
