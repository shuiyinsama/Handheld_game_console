#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    GB_CORE_READY = 0,
    GB_CORE_RUNNING,
    GB_CORE_HALTED,
    GB_CORE_STOPPED,
    GB_CORE_UNSUPPORTED_OPCODE,
} gb_core_status_t;

typedef enum {
    GB_BUTTON_RIGHT  = 1 << 0,
    GB_BUTTON_LEFT   = 1 << 1,
    GB_BUTTON_UP     = 1 << 2,
    GB_BUTTON_DOWN   = 1 << 3,
    GB_BUTTON_A      = 1 << 4,
    GB_BUTTON_B      = 1 << 5,
    GB_BUTTON_SELECT = 1 << 6,
    GB_BUTTON_START  = 1 << 7,
} gb_button_t;

typedef struct {
    uint8_t a;
    uint8_t f;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t e;
    uint8_t h;
    uint8_t l;
    uint16_t sp;
    uint16_t pc;
    bool ime;
    uint8_t ime_enable_delay;
    uint8_t ie;
    uint32_t cycles;
    uint16_t div_counter;
    uint16_t timer_counter;
    uint16_t lcd_counter;
    uint8_t last_opcode;
    uint16_t last_pc;
    uint32_t steps;
    uint32_t halt_ticks;
    uint32_t vblank_count;
    uint32_t interrupt_wake_count;
    uint32_t interrupt_service_count;
    uint32_t vram_write_count;
    uint32_t oam_dma_count;
    uint16_t last_vram_addr;
    uint8_t last_vram_value;
    uint8_t joypad_buttons;
    gb_core_status_t status;
    const uint8_t *rom;
    size_t rom_size;
    uint8_t cartridge_type;
    uint16_t rom_bank;
    uint8_t ram_bank;
    bool ram_enabled;
    uint8_t banking_mode;
    bool cgb_mode;
    uint8_t vram_bank;
    uint8_t bg_palette_index;
    uint8_t obj_palette_index;
    uint8_t bg_palette[0x40];
    uint8_t obj_palette[0x40];
    uint8_t vram[2][0x2000];
    uint8_t eram[0x8000];
    uint8_t wram[0x2000];
    uint8_t oam[0xA0];
    uint8_t io[0x80];
    uint8_t hram[0x7F];
} gb_core_t;

void gb_core_init(gb_core_t *core, const uint8_t *rom, size_t rom_size);
void gb_core_step(gb_core_t *core);
void gb_core_run(gb_core_t *core, uint32_t max_steps);
void gb_core_set_buttons(gb_core_t *core, uint8_t buttons);
uint16_t gb_core_af(const gb_core_t *core);
uint16_t gb_core_bc(const gb_core_t *core);
uint16_t gb_core_de(const gb_core_t *core);
uint16_t gb_core_hl(const gb_core_t *core);
