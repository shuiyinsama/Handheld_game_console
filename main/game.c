#include "game.h"

#include "board.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gb_player.h"
#include "gb_ppu.h"
#include "storage.h"
#include <stdio.h>

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
#define MENU_COUNT         4
#define ROM_LIST_MAX       8
#define GB_STATS_X         42
#define GB_STATS_Y         166
#define GB_STATS_W         350
#define GB_STATS_H         230
#define GB_PREVIEW_X       420
#define GB_PREVIEW_Y       82
#define GB_PLAY_SCALE      3
#define GB_PLAY_W          (GB_PPU_SCREEN_W * GB_PLAY_SCALE)
#define GB_PLAY_H          (GB_PPU_SCREEN_H * GB_PLAY_SCALE)
#define GB_PLAY_X          ((BOARD_LCD_H_RES - GB_PLAY_W) / 2)
#define GB_PLAY_Y          ((BOARD_LCD_V_RES - GB_PLAY_H) / 2)
#define GB_DEBUG_RUN_STEPS      4096
#define GB_PLAY_RUN_CHUNK       8192
#define GB_PLAY_MAX_FRAME_STEPS 65536

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

typedef enum {
    SCREEN_MENU = 0,
    SCREEN_ROM_BROWSER,
    SCREEN_ROM_INFO,
    SCREEN_GB_PLAYER,
    SCREEN_COLLECT,
    SCREEN_INPUT_TEST,
    SCREEN_ABOUT,
} screen_t;

typedef struct {
    screen_t screen;
    uint8_t selected;
    uint8_t rom_selected;
    size_t rom_count;
    esp_err_t rom_status;
    storage_rom_info_t rom_info;
    esp_err_t rom_info_status;
    gb_player_t gb_player;
    esp_err_t gb_load_status;
    bool gb_autorun;
    bool gb_play_mode;
    bool gb_play_static_drawn;
    char rom_names[ROM_LIST_MAX][STORAGE_ROM_NAME_MAX];
    game_state_t collect;
} app_state_t;

static const char *s_menu_items[MENU_COUNT] = {
    "ROM BROWSER",
    "COLLECT DEMO",
    "INPUT TEST",
    "ABOUT",
};

static const uint8_t *glyph_rows(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }

    static const uint8_t space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t a[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t b[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t glyph_c[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t d[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    static const uint8_t e[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t f[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
    static const uint8_t h[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t i[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t j[7] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E};
    static const uint8_t k[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t m[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t n[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t o[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t p[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t q[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static const uint8_t r[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t s[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t t[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t u[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t v[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static const uint8_t w[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    static const uint8_t x[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t z[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    static const uint8_t plus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    static const uint8_t dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    static const uint8_t dash[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const uint8_t zero[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t one[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t two[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t three[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t four[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t five[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    static const uint8_t six[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const uint8_t seven[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t eight[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t nine[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};

    switch (ch) {
    case 'A': return a;
    case 'B': return b;
    case 'C': return glyph_c;
    case 'D': return d;
    case 'E': return e;
    case 'F': return f;
    case 'G': return g;
    case 'H': return h;
    case 'I': return i;
    case 'J': return j;
    case 'K': return k;
    case 'L': return l;
    case 'M': return m;
    case 'N': return n;
    case 'O': return o;
    case 'P': return p;
    case 'Q': return q;
    case 'R': return r;
    case 'S': return s;
    case 'T': return t;
    case 'U': return u;
    case 'V': return v;
    case 'W': return w;
    case 'X': return x;
    case 'Y': return y;
    case 'Z': return z;
    case '+': return plus;
    case '.': return dot;
    case '-':
    case '_': return dash;
    case '0': return zero;
    case '1': return one;
    case '2': return two;
    case '3': return three;
    case '4': return four;
    case '5': return five;
    case '6': return six;
    case '7': return seven;
    case '8': return eight;
    case '9': return nine;
    default: return space;
    }
}

static void draw_text(int x, int y, const char *text, uint16_t color, int scale)
{
    for (const char *p = text; *p; p++) {
        const uint8_t *rows = glyph_rows(*p);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (rows[row] & (1 << (4 - col))) {
                    board_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

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

static void draw_footer_buttons(const board_input_t *input)
{
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

static void draw_menu(const app_state_t *app)
{
    board_begin_frame();
    board_fill_screen(board_rgb565(18, 23, 30));
    draw_text(54, 44, "HANDHELD", board_rgb565(235, 220, 92), 5);
    draw_text(56, 94, "CONSOLE", board_rgb565(130, 190, 230), 4);

    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        const int x = 88;
        const int y = 142 + i * 64;
        const bool selected = i == app->selected;
        const uint16_t border = selected ? board_rgb565(235, 220, 92) : board_rgb565(68, 78, 88);
        const uint16_t fill = selected ? board_rgb565(48, 58, 68) : board_rgb565(28, 34, 40);

        board_fill_rect(x - 5, y - 5, 420, 48, border);
        board_fill_rect(x, y, 410, 38, fill);
        draw_text(x + 18, y + 9, s_menu_items[i], selected ? board_rgb565(255, 255, 255) : board_rgb565(158, 172, 184), 3);
    }

    draw_text(540, 360, "KEY3 UP", board_rgb565(110, 124, 136), 2);
    draw_text(540, 384, "KEY1 DOWN", board_rgb565(110, 124, 136), 2);
    draw_text(540, 408, "BOOT OK", board_rgb565(110, 124, 136), 2);
    board_end_frame();
}

static void refresh_rom_list(app_state_t *app)
{
    app->rom_count = 0;
    app->rom_selected = 0;
    app->rom_status = storage_list_roms(app->rom_names, ROM_LIST_MAX, &app->rom_count);
}

static const char *cartridge_type_name(uint8_t type)
{
    switch (type) {
    case 0x00: return "ROM ONLY";
    case 0x01: return "MBC1";
    case 0x02: return "MBC1 RAM";
    case 0x03: return "MBC1 BAT";
    case 0x05: return "MBC2";
    case 0x06: return "MBC2 BAT";
    case 0x08: return "ROM RAM";
    case 0x09: return "ROM BAT";
    case 0x0F: return "MBC3 TIMER";
    case 0x10: return "MBC3 BAT";
    case 0x11: return "MBC3";
    case 0x12: return "MBC3 RAM";
    case 0x13: return "MBC3 BAT";
    case 0x19: return "MBC5";
    case 0x1A: return "MBC5 RAM";
    case 0x1B: return "MBC5 BAT";
    default: return "UNKNOWN";
    }
}

static const char *rom_size_name(uint8_t code)
{
    switch (code) {
    case 0x00: return "32KB";
    case 0x01: return "64KB";
    case 0x02: return "128KB";
    case 0x03: return "256KB";
    case 0x04: return "512KB";
    case 0x05: return "1MB";
    case 0x06: return "2MB";
    case 0x07: return "4MB";
    case 0x08: return "8MB";
    default: return "UNKNOWN";
    }
}

static const char *ram_size_name(uint8_t code)
{
    switch (code) {
    case 0x00: return "NO RAM";
    case 0x01: return "2KB";
    case 0x02: return "8KB";
    case 0x03: return "32KB";
    case 0x04: return "128KB";
    case 0x05: return "64KB";
    default: return "UNKNOWN";
    }
}

static const char *cgb_mode_name(uint8_t flag)
{
    if (flag == 0x80) {
        return "GB CGB";
    }
    if (flag == 0xC0) {
        return "CGB ONLY";
    }
    return "DMG";
}

static bool rom_requires_cgb(uint8_t flag)
{
    return flag == 0xC0;
}

static const char *gb_core_status_name(gb_core_status_t status)
{
    switch (status) {
    case GB_CORE_READY: return "READY";
    case GB_CORE_RUNNING: return "RUNNING";
    case GB_CORE_HALTED: return "HALTED";
    case GB_CORE_STOPPED: return "STOPPED";
    case GB_CORE_UNSUPPORTED_OPCODE: return "BAD OP";
    default: return "UNKNOWN";
    }
}

static uint8_t gb_buttons_from_input(const board_input_t *input)
{
    uint8_t buttons = 0;

    const bool boot = input->pressed[BOARD_BUTTON_BOOT];
    const bool boot_combo = boot &&
        (input->pressed[BOARD_BUTTON_KEY1] ||
         input->pressed[BOARD_BUTTON_KEY2] ||
         input->pressed[BOARD_BUTTON_KEY3]);

    if (input->pressed[BOARD_BUTTON_KEY0]) {
        buttons |= GB_BUTTON_RIGHT;
    }
    if (input->pressed[BOARD_BUTTON_KEY2] && !boot_combo) {
        buttons |= GB_BUTTON_LEFT;
    }
    if (input->pressed[BOARD_BUTTON_KEY3] && !boot_combo) {
        buttons |= GB_BUTTON_UP;
    }
    if (input->pressed[BOARD_BUTTON_KEY1] && !boot_combo) {
        buttons |= GB_BUTTON_DOWN;
    }
    if (boot && input->pressed[BOARD_BUTTON_KEY3]) {
        buttons |= GB_BUTTON_START;
    } else if (boot && input->pressed[BOARD_BUTTON_KEY1]) {
        buttons |= GB_BUTTON_SELECT;
    } else if (boot && input->pressed[BOARD_BUTTON_KEY2]) {
        buttons |= GB_BUTTON_B;
    } else if (boot) {
        buttons |= GB_BUTTON_A;
    }

    return buttons;
}

static void draw_gb_logo_preview(const uint8_t *rom)
{
    const int scale = 4;
    const int x0 = 92;
    const int y0 = 142;
    const uint16_t bg = board_rgb565(210, 222, 176);
    const uint16_t fg = board_rgb565(42, 52, 36);

    board_fill_rect(x0 - 12, y0 - 12, 216, 56, board_rgb565(112, 128, 92));
    board_fill_rect(x0 - 8, y0 - 8, 208, 48, bg);

    if (rom == NULL) {
        return;
    }

    for (int byte_index = 0; byte_index < 48; byte_index++) {
        const uint8_t value = rom[0x104 + byte_index];
        const int row = byte_index / 6;
        const int col_byte = byte_index % 6;
        for (int bit = 0; bit < 8; bit++) {
            if ((value & (0x80 >> bit)) != 0) {
                const int x = x0 + (col_byte * 8 + bit) * scale;
                const int y = y0 + row * scale;
                board_fill_rect(x, y, scale, scale, fg);
            }
        }
    }
}

static void draw_rom_browser(const app_state_t *app)
{
    board_begin_frame();
    board_fill_screen(board_rgb565(18, 23, 30));
    draw_text(54, 42, "ROM BROWSER", board_rgb565(235, 220, 92), 4);

    if (app->rom_status != ESP_OK) {
        draw_text(58, 132, "SD MOUNT FAIL", board_rgb565(236, 92, 92), 3);
        draw_text(58, 184, "CHECK TF CARD", board_rgb565(160, 178, 190), 3);
        draw_text(58, 248, "FAT32 ONLY", board_rgb565(160, 178, 190), 3);
    } else if (app->rom_count == 0) {
        draw_text(58, 132, "NO GB ROMS", board_rgb565(236, 164, 92), 3);
        draw_text(58, 184, "COPY .GB FILES", board_rgb565(160, 178, 190), 3);
        draw_text(58, 224, "TO TF ROOT", board_rgb565(160, 178, 190), 3);
    } else {
        for (size_t i = 0; i < app->rom_count; i++) {
            const int x = 68;
            const int y = 120 + (int)i * 38;
            const bool selected = i == app->rom_selected;
            board_fill_rect(x - 8, y - 6, 520, 32, selected ? board_rgb565(58, 70, 82) : board_rgb565(24, 30, 36));
            draw_text(x, y, app->rom_names[i], selected ? board_rgb565(255, 255, 255) : board_rgb565(158, 172, 184), 2);
        }
    }

    draw_text(560, 354, "BOOT OPEN", board_rgb565(110, 124, 136), 2);
    draw_text(560, 380, "KEY2 BACK", board_rgb565(110, 124, 136), 2);
    draw_text(560, 406, "KEY0 REFRESH", board_rgb565(110, 124, 136), 2);
    board_end_frame();
}

static void open_selected_rom(app_state_t *app)
{
    if (app->rom_status != ESP_OK || app->rom_count == 0 || app->rom_selected >= app->rom_count) {
        return;
    }
    app->rom_info_status = storage_read_rom_info(app->rom_names[app->rom_selected], &app->rom_info);
    app->screen = SCREEN_ROM_INFO;
}

static void draw_rom_info(const app_state_t *app)
{
    char line[48] = {0};

    board_begin_frame();
    board_fill_screen(board_rgb565(18, 23, 30));
    draw_text(54, 42, "ROM INFO", board_rgb565(235, 220, 92), 4);

    if (app->rom_info_status != ESP_OK) {
        draw_text(58, 132, "READ FAIL", board_rgb565(236, 92, 92), 3);
        snprintf(line, sizeof(line), "ERR 0x%X", (unsigned int)app->rom_info_status);
        draw_text(58, 184, line, board_rgb565(160, 178, 190), 3);
    } else {
        snprintf(line, sizeof(line), "FILE %s", app->rom_names[app->rom_selected]);
        draw_text(58, 112, line, board_rgb565(160, 178, 190), 2);

        snprintf(line, sizeof(line), "TITLE %s", app->rom_info.title);
        draw_text(58, 160, line, board_rgb565(255, 255, 255), 3);

        snprintf(line, sizeof(line), "TYPE %s", cartridge_type_name(app->rom_info.cartridge_type));
        draw_text(58, 214, line, board_rgb565(160, 178, 190), 2);

        snprintf(line, sizeof(line), "ROM %s RAM %s", rom_size_name(app->rom_info.rom_size_code), ram_size_name(app->rom_info.ram_size_code));
        draw_text(58, 248, line, board_rgb565(160, 178, 190), 2);

        snprintf(line, sizeof(line), "MODE %s", cgb_mode_name(app->rom_info.cgb_flag));
        draw_text(58, 282, line, board_rgb565(160, 178, 190), 2);

        draw_text(
            58,
            334,
            app->rom_info.header_checksum_ok ? "HEADER OK" : "HEADER BAD",
            app->rom_info.header_checksum_ok ? board_rgb565(80, 220, 120) : board_rgb565(236, 92, 92),
            3);

        if (rom_requires_cgb(app->rom_info.cgb_flag)) {
            draw_text(58, 380, "CGB CORE TODO", board_rgb565(236, 164, 92), 2);
        }
    }

    draw_text(536, 380, rom_requires_cgb(app->rom_info.cgb_flag) ? "BOOT DEBUG" : "BOOT LOAD", board_rgb565(110, 124, 136), 2);
    draw_text(536, 406, "KEY2 BACK", board_rgb565(110, 124, 136), 2);
    board_end_frame();
}

static void load_selected_rom(app_state_t *app)
{
    if (app->rom_count == 0 || app->rom_selected >= app->rom_count) {
        return;
    }
    app->gb_load_status = gb_player_load(&app->gb_player, app->rom_names[app->rom_selected]);
    app->gb_autorun = false;
    app->gb_play_mode = false;
    app->gb_play_static_drawn = false;
    app->screen = SCREEN_GB_PLAYER;
}

static void draw_gb_player(const app_state_t *app)
{
    char line[48] = {0};

    board_begin_frame();
    board_fill_screen(board_rgb565(20, 24, 28));
    draw_text(54, 42, "GB PLAYER", board_rgb565(235, 220, 92), 4);

    if (app->gb_load_status != ESP_OK || !gb_player_has_rom(&app->gb_player)) {
        draw_text(58, 136, "ROM LOAD FAIL", board_rgb565(236, 92, 92), 3);
        snprintf(line, sizeof(line), "ERR 0x%X", (unsigned int)app->gb_load_status);
        draw_text(58, 190, line, board_rgb565(160, 178, 190), 3);
    } else {
        const storage_loaded_rom_t *rom = &app->gb_player.rom;
        const gb_core_t *core = gb_player_core(&app->gb_player);
        const bool cgb_only = rom_requires_cgb(rom->info.cgb_flag);
        draw_text(42, 92, rom->info.title, board_rgb565(255, 255, 255), 2);

        snprintf(line, sizeof(line), "LOADED %u KB", (unsigned int)(rom->size / 1024));
        draw_text(42, 126, line, board_rgb565(80, 220, 120), 2);

        if (cgb_only) {
            draw_text(42, 150, "CGB ONLY ROM", board_rgb565(236, 164, 92), 2);
        }

        if (core != NULL) {
            gb_ppu_stats_t ppu_stats = {0};
            gb_ppu_get_stats(core, &ppu_stats);

            snprintf(line, sizeof(line), "PC %04X OP %02X", core->pc, core->last_opcode);
            draw_text(42, 166, line, board_rgb565(235, 220, 92), 2);

            snprintf(line, sizeof(line), "AF %04X BC %04X", gb_core_af(core), gb_core_bc(core));
            draw_text(42, 198, line, board_rgb565(160, 178, 190), 2);

            snprintf(line, sizeof(line), "DE %04X HL %04X", gb_core_de(core), gb_core_hl(core));
            draw_text(42, 230, line, board_rgb565(160, 178, 190), 2);

            snprintf(line, sizeof(line), "BNK %03X %s", core->rom_bank, gb_core_status_name(core->status));
            draw_text(42, 262, line, core->status == GB_CORE_UNSUPPORTED_OPCODE ? board_rgb565(236, 92, 92) : board_rgb565(130, 190, 230), 2);

            snprintf(line, sizeof(line), "INS %u HLT %u", (unsigned int)core->steps, (unsigned int)core->halt_ticks);
            draw_text(42, 294, line, board_rgb565(130, 190, 230), 2);

            snprintf(line, sizeof(line), "LY %03u CY %u", core->io[0x44], (unsigned int)core->cycles);
            draw_text(42, 326, line, board_rgb565(130, 190, 230), 2);

            snprintf(line, sizeof(line), "LCD %02X BG %u/%u", ppu_stats.lcdc, ppu_stats.tile_data_nonzero, ppu_stats.bg_map_nonzero);
            draw_text(42, 358, line, board_rgb565(110, 124, 136), 2);
            snprintf(line, sizeof(line), "VW %u VA %04X/%02X DMA %u", (unsigned int)core->vram_write_count, core->last_vram_addr, core->last_vram_value, (unsigned int)core->oam_dma_count);
            draw_text(42, 382, line, board_rgb565(110, 124, 136), 2);
            gb_ppu_draw_preview(core, 420, 82);
        } else {
            draw_gb_logo_preview(rom->data);
        }
    }

    if (app->gb_play_mode) {
        draw_text(42, 396, "PLAY K0R K2L K3U K1D BOOT A", board_rgb565(110, 124, 136), 2);
        draw_text(520, 396, "K0+K2 DEBUG", board_rgb565(110, 124, 136), 2);
    } else {
        const storage_loaded_rom_t *rom = &app->gb_player.rom;
        const bool cgb_only = gb_player_has_rom(&app->gb_player) && rom_requires_cgb(rom->info.cgb_flag);
        draw_text(42, 396, cgb_only ? "KEY0 AUTO KEY1 STEP KEY2 RESET CGB TODO" : (app->gb_autorun ? "KEY0 PAUSE KEY1 STEP KEY2 RESET KEY3 PLAY" : "KEY0 AUTO KEY1 STEP KEY2 RESET KEY3 PLAY"), board_rgb565(110, 124, 136), 2);
        draw_text(612, 396, "BOOT BACK", board_rgb565(110, 124, 136), 2);
    }
    board_end_frame();
}

static void draw_gb_player_dynamic(const app_state_t *app)
{
    if (app->gb_load_status != ESP_OK || !gb_player_has_rom(&app->gb_player)) {
        draw_gb_player(app);
        return;
    }

    const gb_core_t *core = gb_player_core(&app->gb_player);
    if (core == NULL) {
        draw_gb_player(app);
        return;
    }
    const storage_loaded_rom_t *rom = &app->gb_player.rom;
    const bool cgb_only = rom_requires_cgb(rom->info.cgb_flag);

    char line[48] = {0};
    gb_ppu_stats_t ppu_stats = {0};
    gb_ppu_get_stats(core, &ppu_stats);

    board_begin_frame();
    board_fill_rect(GB_STATS_X - 2, GB_STATS_Y - 2, GB_STATS_W, GB_STATS_H, board_rgb565(20, 24, 28));

    snprintf(line, sizeof(line), "PC %04X OP %02X", core->pc, core->last_opcode);
    draw_text(GB_STATS_X, GB_STATS_Y, line, board_rgb565(235, 220, 92), 2);

    snprintf(line, sizeof(line), "AF %04X BC %04X", gb_core_af(core), gb_core_bc(core));
    draw_text(GB_STATS_X, GB_STATS_Y + 32, line, board_rgb565(160, 178, 190), 2);

    snprintf(line, sizeof(line), "DE %04X HL %04X", gb_core_de(core), gb_core_hl(core));
    draw_text(GB_STATS_X, GB_STATS_Y + 64, line, board_rgb565(160, 178, 190), 2);

    snprintf(line, sizeof(line), "BNK %03X %s", core->rom_bank, gb_core_status_name(core->status));
    draw_text(
        GB_STATS_X,
        GB_STATS_Y + 96,
        line,
        core->status == GB_CORE_UNSUPPORTED_OPCODE ? board_rgb565(236, 92, 92) : board_rgb565(130, 190, 230),
        2);

    snprintf(line, sizeof(line), "INS %u HLT %u", (unsigned int)core->steps, (unsigned int)core->halt_ticks);
    draw_text(GB_STATS_X, GB_STATS_Y + 128, line, board_rgb565(130, 190, 230), 2);

    snprintf(line, sizeof(line), "LY %03u CY %u", core->io[0x44], (unsigned int)core->cycles);
    draw_text(GB_STATS_X, GB_STATS_Y + 160, line, board_rgb565(130, 190, 230), 2);

    snprintf(line, sizeof(line), "LCD %02X BG %u/%u", ppu_stats.lcdc, ppu_stats.tile_data_nonzero, ppu_stats.bg_map_nonzero);
    draw_text(GB_STATS_X, GB_STATS_Y + 192, line, board_rgb565(110, 124, 136), 2);

    snprintf(line, sizeof(line), "VW %u VA %04X/%02X DMA %u", (unsigned int)core->vram_write_count, core->last_vram_addr, core->last_vram_value, (unsigned int)core->oam_dma_count);
    draw_text(GB_STATS_X, GB_STATS_Y + 216, line, board_rgb565(110, 124, 136), 2);

    board_fill_rect(42, 396, 710, 18, board_rgb565(20, 24, 28));
    if (app->gb_play_mode) {
        draw_text(42, 396, "PLAY K0R K2L K3U K1D BOOT A", board_rgb565(110, 124, 136), 2);
        draw_text(520, 396, "K0+K2 DEBUG", board_rgb565(110, 124, 136), 2);
    } else {
        draw_text(42, 396, cgb_only ? "KEY0 AUTO KEY1 STEP KEY2 RESET CGB TODO" : (app->gb_autorun ? "KEY0 PAUSE KEY1 STEP KEY2 RESET KEY3 PLAY" : "KEY0 AUTO KEY1 STEP KEY2 RESET KEY3 PLAY"), board_rgb565(110, 124, 136), 2);
        draw_text(612, 396, "BOOT BACK", board_rgb565(110, 124, 136), 2);
    }
    gb_ppu_draw_preview(core, GB_PREVIEW_X, GB_PREVIEW_Y);
    board_end_frame();
}

static void draw_gb_play_screen(app_state_t *app)
{
    const gb_core_t *core = gb_player_core(&app->gb_player);
    if (app->gb_load_status != ESP_OK || core == NULL) {
        draw_gb_player(app);
        return;
    }

    board_begin_frame();
    if (!app->gb_play_static_drawn) {
        board_fill_screen(board_rgb565(10, 13, 16));
        draw_text(34, 28, "PLAY", board_rgb565(235, 220, 92), 2);
        draw_text(34, 54, "K0+K2", board_rgb565(110, 124, 136), 2);
        draw_text(34, 78, "DEBUG", board_rgb565(110, 124, 136), 2);
        draw_text(34, 430, "BT=A BT+U=START BT+D=SELECT BT+L=B", board_rgb565(110, 124, 136), 1);
        app->gb_play_static_drawn = true;
    }
    gb_ppu_draw_screen_scaled(core, GB_PLAY_X, GB_PLAY_Y, GB_PLAY_SCALE);
    board_end_frame();
}

static void run_gb_play_frame(app_state_t *app)
{
    const gb_core_t *core = gb_player_core(&app->gb_player);
    if (core == NULL) {
        return;
    }

    const uint32_t start_vblank = core->vblank_count;
    uint32_t ran_steps = 0;
    do {
        gb_player_run_steps(&app->gb_player, GB_PLAY_RUN_CHUNK);
        ran_steps += GB_PLAY_RUN_CHUNK;
        core = gb_player_core(&app->gb_player);
    } while (core != NULL &&
             core->vblank_count == start_vblank &&
             ran_steps < GB_PLAY_MAX_FRAME_STEPS);
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

static void draw_collect_screen(const board_input_t *input, const game_state_t *state)
{
    board_begin_frame();
    board_fill_screen(board_rgb565(20, 24, 28));

    board_fill_rect(PLAYFIELD_X - 4, PLAYFIELD_Y - 4, PLAYFIELD_W + 8, PLAYFIELD_H + 8, board_rgb565(76, 86, 96));
    board_fill_rect(PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H, board_rgb565(28, 34, 40));

    draw_score(state->score);
    draw_target(&state->target);
    draw_player(input, &state->player);
    draw_footer_buttons(input);
    draw_text(560, 314, "L R U D", board_rgb565(110, 124, 136), 2);
    draw_text(560, 340, "B RESET", board_rgb565(110, 124, 136), 2);
    draw_text(560, 366, "L+R MENU", board_rgb565(110, 124, 136), 2);
    board_end_frame();
}

static void draw_input_test(const board_input_t *input)
{
    board_begin_frame();
    board_fill_screen(board_rgb565(20, 24, 28));
    draw_text(54, 48, "INPUT TEST", board_rgb565(235, 220, 92), 4);
    draw_footer_buttons(input);
    draw_text(54, 144, "BOOT KEY0 KEY1 KEY2 KEY3", board_rgb565(160, 178, 190), 3);
    draw_text(54, 210, "BOOT BACK", board_rgb565(110, 124, 136), 2);
    board_end_frame();
}

static void draw_about(void)
{
    board_begin_frame();
    board_fill_screen(board_rgb565(20, 24, 28));
    draw_text(54, 54, "ABOUT", board_rgb565(235, 220, 92), 5);
    draw_text(58, 132, "ESP32S3", board_rgb565(130, 190, 230), 4);
    draw_text(58, 184, "RGBLCD OK", board_rgb565(160, 178, 190), 3);
    draw_text(58, 224, "INPUT OK", board_rgb565(160, 178, 190), 3);
    draw_text(58, 286, "GB EMU NEXT", board_rgb565(235, 220, 92), 3);
    draw_text(58, 374, "BOOT BACK", board_rgb565(110, 124, 136), 2);
    board_end_frame();
}

void game_run(void)
{
    board_input_t input = {0};
    app_state_t app = {
        .screen = SCREEN_MENU,
        .selected = 0,
        .collect = {.rng = 0x1234ABCD},
    };
    reset_game(&app.collect);

    board_input_scan(&input);
    draw_menu(&app);
    ESP_LOGI(TAG, "Menu started");

    while (true) {
        const bool input_changed = board_input_scan(&input);

        if (app.screen == SCREEN_MENU) {
            if (input_changed) {
                if (input.changed[BOARD_BUTTON_KEY3] && input.pressed[BOARD_BUTTON_KEY3] && app.selected > 0) {
                    app.selected--;
                    draw_menu(&app);
                }
                if (input.changed[BOARD_BUTTON_KEY1] && input.pressed[BOARD_BUTTON_KEY1] && app.selected < MENU_COUNT - 1) {
                    app.selected++;
                    draw_menu(&app);
                }
                if (input.changed[BOARD_BUTTON_BOOT] && input.pressed[BOARD_BUTTON_BOOT]) {
                    if (app.selected == 0) {
                        app.screen = SCREEN_ROM_BROWSER;
                        refresh_rom_list(&app);
                        draw_rom_browser(&app);
                    } else if (app.selected == 1) {
                        app.screen = SCREEN_COLLECT;
                        reset_game(&app.collect);
                        draw_collect_screen(&input, &app.collect);
                    } else if (app.selected == 2) {
                        app.screen = SCREEN_INPUT_TEST;
                        draw_input_test(&input);
                    } else {
                        app.screen = SCREEN_ABOUT;
                        draw_about();
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }

        if (app.screen == SCREEN_ROM_BROWSER) {
            if (input_changed) {
                if (input.changed[BOARD_BUTTON_KEY2] && input.pressed[BOARD_BUTTON_KEY2]) {
                    app.screen = SCREEN_MENU;
                    draw_menu(&app);
                } else if (input.changed[BOARD_BUTTON_BOOT] && input.pressed[BOARD_BUTTON_BOOT]) {
                    open_selected_rom(&app);
                    if (app.screen == SCREEN_ROM_INFO) {
                        draw_rom_info(&app);
                    }
                } else if (input.changed[BOARD_BUTTON_KEY0] && input.pressed[BOARD_BUTTON_KEY0]) {
                    refresh_rom_list(&app);
                    draw_rom_browser(&app);
                } else if (input.changed[BOARD_BUTTON_KEY3] && input.pressed[BOARD_BUTTON_KEY3] && app.rom_selected > 0) {
                    app.rom_selected--;
                    draw_rom_browser(&app);
                } else if (input.changed[BOARD_BUTTON_KEY1] &&
                           input.pressed[BOARD_BUTTON_KEY1] &&
                           app.rom_selected + 1 < app.rom_count) {
                    app.rom_selected++;
                    draw_rom_browser(&app);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }

        if (app.screen == SCREEN_ROM_INFO) {
            if (input.changed[BOARD_BUTTON_KEY2] && input.pressed[BOARD_BUTTON_KEY2]) {
                app.screen = SCREEN_ROM_BROWSER;
                draw_rom_browser(&app);
            } else if (input.changed[BOARD_BUTTON_BOOT] && input.pressed[BOARD_BUTTON_BOOT]) {
                load_selected_rom(&app);
                draw_gb_player(&app);
            }
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }

        if (app.screen == SCREEN_GB_PLAYER) {
            if (app.gb_play_mode) {
                if (input.pressed[BOARD_BUTTON_KEY0] && input.pressed[BOARD_BUTTON_KEY2]) {
                    app.gb_play_mode = false;
                    app.gb_play_static_drawn = false;
                    app.gb_autorun = true;
                    gb_player_set_buttons(&app.gb_player, 0);
                    draw_gb_player(&app);
                } else {
                    gb_player_set_buttons(&app.gb_player, gb_buttons_from_input(&input));
                }
            } else if (input.changed[BOARD_BUTTON_BOOT] && input.pressed[BOARD_BUTTON_BOOT]) {
                app.gb_autorun = false;
                app.gb_play_mode = false;
                app.gb_play_static_drawn = false;
                gb_player_set_buttons(&app.gb_player, 0);
                gb_player_unload(&app.gb_player);
                app.screen = SCREEN_ROM_INFO;
                draw_rom_info(&app);
            } else if (input.changed[BOARD_BUTTON_KEY0] && input.pressed[BOARD_BUTTON_KEY0]) {
                app.gb_autorun = !app.gb_autorun;
                draw_gb_player_dynamic(&app);
            } else if (input.changed[BOARD_BUTTON_KEY1] && input.pressed[BOARD_BUTTON_KEY1]) {
                gb_player_step(&app.gb_player);
                draw_gb_player_dynamic(&app);
            } else if (input.changed[BOARD_BUTTON_KEY2] && input.pressed[BOARD_BUTTON_KEY2]) {
                app.gb_autorun = false;
                app.gb_play_mode = false;
                app.gb_play_static_drawn = false;
                gb_player_set_buttons(&app.gb_player, 0);
                app.gb_load_status = gb_player_reset_core(&app.gb_player);
                draw_gb_player_dynamic(&app);
            } else if (input.changed[BOARD_BUTTON_KEY3] && input.pressed[BOARD_BUTTON_KEY3]) {
                const bool cgb_only =
                    gb_player_has_rom(&app.gb_player) &&
                    rom_requires_cgb(app.gb_player.rom.info.cgb_flag);
                if (!cgb_only) {
                    app.gb_play_mode = true;
                    app.gb_play_static_drawn = false;
                    app.gb_autorun = true;
                    gb_player_set_buttons(&app.gb_player, gb_buttons_from_input(&input));
                    draw_gb_play_screen(&app);
                }
            }

            if (app.screen == SCREEN_GB_PLAYER && app.gb_autorun) {
                if (app.gb_play_mode) {
                    run_gb_play_frame(&app);
                } else {
                    gb_player_run_steps(&app.gb_player, GB_DEBUG_RUN_STEPS);
                }
                if (app.gb_play_mode) {
                    draw_gb_play_screen(&app);
                } else {
                    draw_gb_player_dynamic(&app);
                }
            }
            vTaskDelay(app.gb_play_mode ? 1 : pdMS_TO_TICKS(33));
            continue;
        }

        if (app.screen == SCREEN_INPUT_TEST) {
            if (input_changed) {
                if (input.changed[BOARD_BUTTON_BOOT] && input.pressed[BOARD_BUTTON_BOOT]) {
                    app.screen = SCREEN_MENU;
                    draw_menu(&app);
                } else {
                    draw_input_test(&input);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }

        if (app.screen == SCREEN_ABOUT) {
            if (input.changed[BOARD_BUTTON_BOOT] && input.pressed[BOARD_BUTTON_BOOT]) {
                app.screen = SCREEN_MENU;
                draw_menu(&app);
            }
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }

        if (app.screen == SCREEN_COLLECT) {
            if (input_changed) {
                if (input.pressed[BOARD_BUTTON_KEY0] && input.pressed[BOARD_BUTTON_KEY2]) {
                    app.screen = SCREEN_MENU;
                    draw_menu(&app);
                    vTaskDelay(pdMS_TO_TICKS(33));
                    continue;
                }

                if (input.changed[BOARD_BUTTON_BOOT] && input.pressed[BOARD_BUTTON_BOOT]) {
                    reset_game(&app.collect);
                    draw_collect_screen(&input, &app.collect);
                    vTaskDelay(pdMS_TO_TICKS(33));
                    continue;
                }

                for (size_t i = 0; i < BOARD_BUTTON_COUNT; i++) {
                    if (input.changed[i]) {
                        draw_button_slot(&input, i);
                    }
                }
            }

            if (update_player(&input, &app.collect.player)) {
                if (player_hits_target(&app.collect.player, &app.collect.target)) {
                    if (app.collect.score < SCORE_MAX) {
                        app.collect.score++;
                    }
                    place_target(&app.collect);
                    draw_collect_screen(&input, &app.collect);
                } else {
                    erase_player(&app.collect.player);
                    draw_target(&app.collect.target);
                    draw_player(&input, &app.collect.player);
                }
            }

            vTaskDelay(pdMS_TO_TICKS(33));
        }
    }
}
