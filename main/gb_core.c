#include "gb_core.h"

#include <string.h>

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

#define INT_VBLANK 0x01
#define INT_LCD    0x02
#define INT_TIMER  0x04
#define INT_JOYPAD 0x10

#define IO_JOYP 0x00
#define IO_DIV  0x04
#define IO_TIMA 0x05
#define IO_TMA  0x06
#define IO_TAC  0x07
#define IO_IF   0x0F
#define IO_LCDC 0x40
#define IO_STAT 0x41
#define IO_SCY  0x42
#define IO_SCX  0x43
#define IO_LY   0x44
#define IO_LYC  0x45
#define IO_DMA  0x46
#define IO_BGP  0x47
#define IO_OBP0 0x48
#define IO_OBP1 0x49
#define IO_WY   0x4A
#define IO_WX   0x4B
#define IO_VBK  0x4F
#define IO_HDMA1 0x51
#define IO_HDMA2 0x52
#define IO_HDMA3 0x53
#define IO_HDMA4 0x54
#define IO_HDMA5 0x55
#define IO_BCPS 0x68
#define IO_BCPD 0x69
#define IO_OCPS 0x6A
#define IO_OCPD 0x6B

static uint16_t make16(uint8_t hi, uint8_t lo)
{
    return ((uint16_t)hi << 8) | lo;
}

static void set16(uint8_t *hi, uint8_t *lo, uint16_t value)
{
    *hi = (uint8_t)(value >> 8);
    *lo = (uint8_t)value;
}

uint16_t gb_core_af(const gb_core_t *core)
{
    return make16(core->a, core->f);
}

uint16_t gb_core_bc(const gb_core_t *core)
{
    return make16(core->b, core->c);
}

uint16_t gb_core_de(const gb_core_t *core)
{
    return make16(core->d, core->e);
}

uint16_t gb_core_hl(const gb_core_t *core)
{
    return make16(core->h, core->l);
}

static void gb_core_set_hl(gb_core_t *core, uint16_t value)
{
    set16(&core->h, &core->l, value);
}

static void push16(gb_core_t *core, uint16_t value);
static void request_interrupt(gb_core_t *core, uint8_t interrupt);

static void init_cgb_palette(uint8_t *palette)
{
    static const uint16_t shades[4] = {
        0x7FFF,
        0x56B5,
        0x294A,
        0x0000,
    };

    for (int pal = 0; pal < 8; pal++) {
        for (int color = 0; color < 4; color++) {
            const uint16_t value = shades[color];
            const int index = pal * 8 + color * 2;
            palette[index] = (uint8_t)value;
            palette[index + 1] = (uint8_t)(value >> 8);
        }
    }
}

static uint8_t read_joyp(const gb_core_t *core)
{
    const uint8_t select = core->io[IO_JOYP] & 0x30;
    uint8_t lower = 0x0F;

    if ((select & 0x10) == 0) {
        lower &= (uint8_t)~(core->joypad_buttons & 0x0F);
    }
    if ((select & 0x20) == 0) {
        lower &= (uint8_t)~((core->joypad_buttons >> 4) & 0x0F);
    }

    return (uint8_t)(0xC0 | select | lower);
}

static size_t rom_bank_count(const gb_core_t *core)
{
    const size_t banks = core->rom_size / 0x4000;
    return banks == 0 ? 1 : banks;
}

static uint8_t read_rom_bank(const gb_core_t *core, uint16_t bank, uint16_t offset)
{
    const size_t banks = rom_bank_count(core);
    bank %= banks;
    const size_t addr = (size_t)bank * 0x4000 + offset;
    return addr < core->rom_size ? core->rom[addr] : 0xFF;
}

static bool is_mbc1(const gb_core_t *core)
{
    return core->cartridge_type == 0x01 || core->cartridge_type == 0x02 || core->cartridge_type == 0x03;
}

static bool is_mbc3(const gb_core_t *core)
{
    return core->cartridge_type >= 0x0F && core->cartridge_type <= 0x13;
}

static bool is_mbc5(const gb_core_t *core)
{
    return core->cartridge_type >= 0x19 && core->cartridge_type <= 0x1E;
}

static bool is_mbc5_rumble(const gb_core_t *core)
{
    return core->cartridge_type >= 0x1C && core->cartridge_type <= 0x1E;
}

static uint8_t external_ram_bank(const gb_core_t *core)
{
    if (is_mbc1(core)) {
        return core->banking_mode == 0 ? 0 : (core->ram_bank & 0x03);
    }
    if (is_mbc3(core)) {
        return core->ram_bank & 0x03;
    }
    if (is_mbc5(core)) {
        return core->ram_bank & 0x0F;
    }
    return 0;
}

static uint8_t read8(const gb_core_t *core, uint16_t addr)
{
    if (addr < 0x4000) {
        return addr < core->rom_size ? core->rom[addr] : 0xFF;
    }
    if (addr < 0x8000) {
        return read_rom_bank(core, core->rom_bank, addr - 0x4000);
    }
    if (addr < 0xA000) {
        return core->vram[core->vram_bank & 0x01][addr - 0x8000];
    }
    if (addr < 0xC000) {
        if (!core->ram_enabled && (is_mbc1(core) || is_mbc3(core) || is_mbc5(core))) {
            return 0xFF;
        }
        const uint32_t offset = (uint32_t)external_ram_bank(core) * 0x2000U + (addr - 0xA000U);
        return core->eram[offset % sizeof(core->eram)];
    }
    if (addr < 0xE000) {
        return core->wram[addr - 0xC000];
    }
    if (addr < 0xFE00) {
        return core->wram[addr - 0xE000];
    }
    if (addr < 0xFEA0) {
        return core->oam[addr - 0xFE00];
    }
    if (addr < 0xFF00) {
        return 0xFF;
    }
    if (addr < 0xFF80) {
        if (addr == 0xFF00) {
            return read_joyp(core);
        }
        if (addr == 0xFF0F) {
            return core->io[IO_IF] | 0xE0;
        }
        if (addr == 0xFF00 + IO_VBK) {
            return (uint8_t)(0xFE | (core->vram_bank & 0x01));
        }
        if (addr == 0xFF00 + IO_BCPS) {
            return core->bg_palette_index;
        }
        if (addr == 0xFF00 + IO_BCPD) {
            return core->bg_palette[core->bg_palette_index & 0x3F];
        }
        if (addr == 0xFF00 + IO_OCPS) {
            return core->obj_palette_index;
        }
        if (addr == 0xFF00 + IO_OCPD) {
            return core->obj_palette[core->obj_palette_index & 0x3F];
        }
        return core->io[addr - 0xFF00];
    }
    if (addr < 0xFFFF) {
        return core->hram[addr - 0xFF80];
    }
    return core->ie;
}

static void run_oam_dma(gb_core_t *core, uint8_t source_hi)
{
    const uint16_t source = (uint16_t)source_hi << 8;
    core->oam_dma_count++;
    for (uint16_t i = 0; i < 0xA0; i++) {
        core->oam[i] = read8(core, (uint16_t)(source + i));
    }
}

static void run_vram_dma(gb_core_t *core, uint8_t control)
{
    uint16_t source = (uint16_t)(((uint16_t)core->io[IO_HDMA1] << 8) | (core->io[IO_HDMA2] & 0xF0));
    uint16_t dest = (uint16_t)(0x8000 | (((uint16_t)(core->io[IO_HDMA3] & 0x1F) << 8) | (core->io[IO_HDMA4] & 0xF0)));
    const uint16_t length = (uint16_t)(((control & 0x7F) + 1) * 0x10);

    for (uint16_t i = 0; i < length; i++) {
        if (dest + i >= 0x8000 && dest + i < 0xA000) {
            core->vram[core->vram_bank & 0x01][(dest + i) - 0x8000] = read8(core, (uint16_t)(source + i));
        }
    }

    source = (uint16_t)(source + length);
    dest = (uint16_t)(dest + length);
    core->io[IO_HDMA1] = (uint8_t)(source >> 8);
    core->io[IO_HDMA2] = (uint8_t)(source & 0xF0);
    core->io[IO_HDMA3] = (uint8_t)((dest >> 8) & 0x1F);
    core->io[IO_HDMA4] = (uint8_t)(dest & 0xF0);
    core->io[IO_HDMA5] = 0xFF;
}

static void write8(gb_core_t *core, uint16_t addr, uint8_t value)
{
    if (addr < 0x8000) {
        if (is_mbc1(core)) {
            if (addr < 0x2000) {
                core->ram_enabled = (value & 0x0F) == 0x0A;
            } else if (addr < 0x4000) {
                core->rom_bank = (core->rom_bank & 0x60) | (value & 0x1F);
                if ((core->rom_bank & 0x1F) == 0) {
                    core->rom_bank++;
                }
            } else if (addr < 0x6000) {
                if (core->banking_mode == 0) {
                    core->rom_bank = (core->rom_bank & 0x1F) | ((value & 0x03) << 5);
                    if ((core->rom_bank & 0x1F) == 0) {
                        core->rom_bank++;
                    }
                } else {
                    core->ram_bank = value & 0x03;
                }
            } else {
                core->banking_mode = value & 0x01;
            }
        } else if (is_mbc3(core)) {
            if (addr < 0x2000) {
                core->ram_enabled = (value & 0x0F) == 0x0A;
            } else if (addr < 0x4000) {
                core->rom_bank = value & 0x7F;
                if (core->rom_bank == 0) {
                    core->rom_bank = 1;
                }
            } else if (addr < 0x6000) {
                core->ram_bank = value & 0x03;
            }
        } else if (is_mbc5(core)) {
            if (addr < 0x2000) {
                core->ram_enabled = (value & 0x0F) == 0x0A;
            } else if (addr < 0x3000) {
                core->rom_bank = (core->rom_bank & 0x100) | value;
            } else if (addr < 0x4000) {
                core->rom_bank = (core->rom_bank & 0x0FF) | ((uint16_t)(value & 0x01) << 8);
            } else if (addr < 0x6000) {
                core->ram_bank = value & (is_mbc5_rumble(core) ? 0x07 : 0x0F);
            }
        }
        return;
    }
    if (addr < 0xA000) {
        core->vram[core->vram_bank & 0x01][addr - 0x8000] = value;
        core->vram_write_count++;
        core->last_vram_addr = addr;
        core->last_vram_value = value;
        return;
    }
    if (addr < 0xC000) {
        if (!core->ram_enabled && (is_mbc1(core) || is_mbc3(core) || is_mbc5(core))) {
            return;
        }
        const uint32_t offset = (uint32_t)external_ram_bank(core) * 0x2000U + (addr - 0xA000U);
        core->eram[offset % sizeof(core->eram)] = value;
        return;
    }
    if (addr < 0xE000) {
        core->wram[addr - 0xC000] = value;
        return;
    }
    if (addr < 0xFE00) {
        core->wram[addr - 0xE000] = value;
        return;
    }
    if (addr < 0xFEA0) {
        core->oam[addr - 0xFE00] = value;
        return;
    }
    if (addr < 0xFF00) {
        return;
    }
    if (addr < 0xFF80) {
        const uint8_t reg = (uint8_t)(addr - 0xFF00);
        if (reg == IO_DIV) {
            core->io[IO_DIV] = 0;
            core->div_counter = 0;
            return;
        }
        if (reg == IO_LY) {
            core->io[IO_LY] = 0;
            core->lcd_counter = 0;
            return;
        }
        if (reg == IO_DMA) {
            core->io[IO_DMA] = value;
            run_oam_dma(core, value);
            return;
        }
        if (reg >= IO_HDMA1 && reg <= IO_HDMA4) {
            core->io[reg] = value;
            return;
        }
        if (reg == IO_HDMA5) {
            core->io[IO_HDMA5] = value;
            run_vram_dma(core, value);
            return;
        }
        if (reg == IO_STAT) {
            core->io[IO_STAT] = (uint8_t)((core->io[IO_STAT] & 0x07) | (value & 0x78));
            return;
        }
        if (reg == IO_IF) {
            core->io[IO_IF] = value | 0xE0;
            return;
        }
        if (reg == IO_VBK) {
            core->vram_bank = value & 0x01;
            core->io[IO_VBK] = (uint8_t)(0xFE | core->vram_bank);
            return;
        }
        if (reg == IO_BCPS) {
            core->bg_palette_index = value & 0xBF;
            core->io[IO_BCPS] = core->bg_palette_index;
            return;
        }
        if (reg == IO_BCPD) {
            core->bg_palette[core->bg_palette_index & 0x3F] = value;
            if ((core->bg_palette_index & 0x80) != 0) {
                core->bg_palette_index = (uint8_t)(0x80 | ((core->bg_palette_index + 1) & 0x3F));
                core->io[IO_BCPS] = core->bg_palette_index;
            }
            return;
        }
        if (reg == IO_OCPS) {
            core->obj_palette_index = value & 0xBF;
            core->io[IO_OCPS] = core->obj_palette_index;
            return;
        }
        if (reg == IO_OCPD) {
            core->obj_palette[core->obj_palette_index & 0x3F] = value;
            if ((core->obj_palette_index & 0x80) != 0) {
                core->obj_palette_index = (uint8_t)(0x80 | ((core->obj_palette_index + 1) & 0x3F));
                core->io[IO_OCPS] = core->obj_palette_index;
            }
            return;
        }
        if (reg == IO_JOYP) {
            core->io[IO_JOYP] = (uint8_t)(0xC0 | (value & 0x30) | 0x0F);
            return;
        }
        core->io[reg] = value;
        return;
    }
    if (addr < 0xFFFF) {
        core->hram[addr - 0xFF80] = value;
        return;
    }
    core->ie = value;
}

static uint8_t fetch8(gb_core_t *core)
{
    const uint8_t value = read8(core, core->pc);
    core->pc++;
    return value;
}

static uint16_t fetch16(gb_core_t *core)
{
    const uint8_t lo = fetch8(core);
    const uint8_t hi = fetch8(core);
    return make16(hi, lo);
}

static void request_interrupt(gb_core_t *core, uint8_t interrupt)
{
    core->io[IO_IF] = (core->io[IO_IF] | interrupt) | 0xE0;
}

static uint16_t timer_period_cycles(uint8_t tac)
{
    switch (tac & 0x03) {
    case 0: return 1024;
    case 1: return 16;
    case 2: return 64;
    default: return 256;
    }
}

static void tick_timer(gb_core_t *core, uint16_t cycles)
{
    core->div_counter = (uint16_t)(core->div_counter + cycles);
    while (core->div_counter >= 256) {
        core->div_counter = (uint16_t)(core->div_counter - 256);
        core->io[IO_DIV]++;
    }

    const uint8_t tac = core->io[IO_TAC];
    if ((tac & 0x04) == 0) {
        return;
    }

    const uint16_t period = timer_period_cycles(tac);
    core->timer_counter = (uint16_t)(core->timer_counter + cycles);
    while (core->timer_counter >= period) {
        core->timer_counter = (uint16_t)(core->timer_counter - period);
        if (core->io[IO_TIMA] == 0xFF) {
            core->io[IO_TIMA] = core->io[IO_TMA];
            request_interrupt(core, INT_TIMER);
        } else {
            core->io[IO_TIMA]++;
        }
    }
}

static void tick_lcd(gb_core_t *core, uint16_t cycles)
{
    if ((core->io[IO_LCDC] & 0x80) == 0) {
        core->io[IO_LY] = 0;
        core->lcd_counter = 0;
        core->io[IO_STAT] = (core->io[IO_STAT] & 0xFC) | 0x00;
        return;
    }

    core->lcd_counter = (uint16_t)(core->lcd_counter + cycles);
    while (core->lcd_counter >= 456) {
        core->lcd_counter = (uint16_t)(core->lcd_counter - 456);
        core->io[IO_LY]++;
        if (core->io[IO_LY] == 144) {
            core->vblank_count++;
            request_interrupt(core, INT_VBLANK);
        } else if (core->io[IO_LY] > 153) {
            core->io[IO_LY] = 0;
        }
    }

    uint8_t stat = core->io[IO_STAT] & 0xF8;
    uint8_t mode = 0;
    if (core->io[IO_LY] >= 144) {
        mode = 1;
    } else if (core->lcd_counter < 80) {
        mode = 2;
    } else if (core->lcd_counter < 252) {
        mode = 3;
    }

    if (core->io[IO_LY] == core->io[IO_LYC]) {
        stat |= 0x04;
        if ((core->io[IO_STAT] & 0x40) != 0) {
            request_interrupt(core, INT_LCD);
        }
    }
    stat |= mode;
    core->io[IO_STAT] = stat;
}

static void gb_core_tick(gb_core_t *core, uint16_t cycles)
{
    core->cycles += cycles;
    tick_timer(core, cycles);
    tick_lcd(core, cycles);
}

static uint16_t halted_cycles_until_event(const gb_core_t *core)
{
    uint16_t cycles = 252;

    if ((core->io[IO_LCDC] & 0x80) != 0) {
        uint16_t lcd_cycles = (uint16_t)(456 - core->lcd_counter);
        if (lcd_cycles == 0 || lcd_cycles > 456) {
            lcd_cycles = 456;
        }
        if (lcd_cycles < cycles) {
            cycles = lcd_cycles;
        }
    }

    const uint8_t tac = core->io[IO_TAC];
    if ((tac & 0x04) != 0) {
        const uint16_t period = timer_period_cycles(tac);
        uint16_t timer_cycles = (uint16_t)(period - core->timer_counter);
        if (timer_cycles == 0 || timer_cycles > period) {
            timer_cycles = period;
        }
        if (core->io[IO_TIMA] != 0xFF) {
            const uint16_t increments_until_irq = (uint16_t)(0x100U - core->io[IO_TIMA]);
            const uint32_t irq_cycles =
                (uint32_t)timer_cycles + (uint32_t)(increments_until_irq - 1U) * period;
            timer_cycles = irq_cycles > 252U ? 252U : (uint16_t)irq_cycles;
        }
        if (timer_cycles < cycles) {
            cycles = timer_cycles;
        }
    }

    cycles &= (uint16_t)~0x03U;
    return cycles >= 4 ? cycles : 4;
}

static void update_ime_delay(gb_core_t *core)
{
    if (core->ime_enable_delay == 0) {
        return;
    }

    core->ime_enable_delay--;
    if (core->ime_enable_delay == 0) {
        core->ime = true;
    }
}

static bool service_interrupt(gb_core_t *core)
{
    const uint8_t pending = core->ie & core->io[IO_IF] & 0x1F;
    if (pending == 0) {
        return false;
    }

    if (core->status == GB_CORE_HALTED) {
        core->status = GB_CORE_RUNNING;
        core->interrupt_wake_count++;
    }

    if (!core->ime) {
        return false;
    }

    static const uint16_t vectors[5] = {0x40, 0x48, 0x50, 0x58, 0x60};
    core->ime = false;
    core->ime_enable_delay = 0;
    for (uint8_t i = 0; i < 5; i++) {
        const uint8_t bit = (uint8_t)(1U << i);
        if ((pending & bit) != 0) {
            core->io[IO_IF] = (core->io[IO_IF] & (uint8_t)~bit) | 0xE0;
            push16(core, core->pc);
            core->pc = vectors[i];
            core->interrupt_service_count++;
            core->last_opcode = 0xFF;
            core->last_pc = core->pc;
            core->steps++;
            gb_core_tick(core, 20);
            update_ime_delay(core);
            return true;
        }
    }
    return false;
}

static void finish_instruction(gb_core_t *core, uint8_t cycles)
{
    core->steps++;
    gb_core_tick(core, cycles);
    update_ime_delay(core);
}

static void write16(gb_core_t *core, uint16_t addr, uint16_t value)
{
    write8(core, addr, (uint8_t)value);
    write8(core, (uint16_t)(addr + 1), (uint8_t)(value >> 8));
}

static void push16(gb_core_t *core, uint16_t value)
{
    core->sp--;
    write8(core, core->sp, (uint8_t)(value >> 8));
    core->sp--;
    write8(core, core->sp, (uint8_t)value);
}

static uint16_t pop16(gb_core_t *core)
{
    const uint8_t lo = read8(core, core->sp);
    core->sp++;
    const uint8_t hi = read8(core, core->sp);
    core->sp++;
    return make16(hi, lo);
}

static void set_flag(gb_core_t *core, uint8_t flag, bool enabled)
{
    if (enabled) {
        core->f |= flag;
    } else {
        core->f &= (uint8_t)~flag;
    }
    core->f &= 0xF0;
}

static bool flag_is_set(const gb_core_t *core, uint8_t flag)
{
    return (core->f & flag) != 0;
}

static uint8_t inc8(gb_core_t *core, uint8_t value)
{
    const uint8_t result = (uint8_t)(value + 1);
    set_flag(core, FLAG_Z, result == 0);
    set_flag(core, FLAG_N, false);
    set_flag(core, FLAG_H, (value & 0x0F) == 0x0F);
    return result;
}

static uint8_t dec8(gb_core_t *core, uint8_t value)
{
    const uint8_t result = (uint8_t)(value - 1);
    set_flag(core, FLAG_Z, result == 0);
    set_flag(core, FLAG_N, true);
    set_flag(core, FLAG_H, (value & 0x0F) == 0);
    return result;
}

static void add_a(gb_core_t *core, uint8_t value)
{
    const uint16_t result = (uint16_t)core->a + value;
    set_flag(core, FLAG_Z, (uint8_t)result == 0);
    set_flag(core, FLAG_N, false);
    set_flag(core, FLAG_H, ((core->a & 0x0F) + (value & 0x0F)) > 0x0F);
    set_flag(core, FLAG_C, result > 0xFF);
    core->a = (uint8_t)result;
}

static void adc_a(gb_core_t *core, uint8_t value)
{
    const uint8_t carry = flag_is_set(core, FLAG_C) ? 1 : 0;
    const uint16_t result = (uint16_t)core->a + value + carry;
    set_flag(core, FLAG_Z, (uint8_t)result == 0);
    set_flag(core, FLAG_N, false);
    set_flag(core, FLAG_H, ((core->a & 0x0F) + (value & 0x0F) + carry) > 0x0F);
    set_flag(core, FLAG_C, result > 0xFF);
    core->a = (uint8_t)result;
}

static void sub_a(gb_core_t *core, uint8_t value)
{
    const uint8_t old = core->a;
    core->a = (uint8_t)(core->a - value);
    set_flag(core, FLAG_Z, core->a == 0);
    set_flag(core, FLAG_N, true);
    set_flag(core, FLAG_H, (old & 0x0F) < (value & 0x0F));
    set_flag(core, FLAG_C, old < value);
}

static void sbc_a(gb_core_t *core, uint8_t value)
{
    const uint8_t carry = flag_is_set(core, FLAG_C) ? 1 : 0;
    const uint8_t old = core->a;
    const uint16_t result = (uint16_t)core->a - value - carry;
    core->a = (uint8_t)result;
    set_flag(core, FLAG_Z, core->a == 0);
    set_flag(core, FLAG_N, true);
    set_flag(core, FLAG_H, (old & 0x0F) < ((value & 0x0F) + carry));
    set_flag(core, FLAG_C, old < (uint16_t)value + carry);
}

static void add_hl(gb_core_t *core, uint16_t value)
{
    const uint16_t hl = gb_core_hl(core);
    const uint32_t result = (uint32_t)hl + value;
    set_flag(core, FLAG_N, false);
    set_flag(core, FLAG_H, ((hl & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF);
    set_flag(core, FLAG_C, result > 0xFFFF);
    gb_core_set_hl(core, (uint16_t)result);
}

static void daa(gb_core_t *core)
{
    uint8_t adjust = 0;
    bool carry = flag_is_set(core, FLAG_C);

    if (!flag_is_set(core, FLAG_N)) {
        if (flag_is_set(core, FLAG_H) || (core->a & 0x0F) > 9) {
            adjust |= 0x06;
        }
        if (carry || core->a > 0x99) {
            adjust |= 0x60;
            carry = true;
        }
        core->a = (uint8_t)(core->a + adjust);
    } else {
        if (flag_is_set(core, FLAG_H)) {
            adjust |= 0x06;
        }
        if (carry) {
            adjust |= 0x60;
        }
        core->a = (uint8_t)(core->a - adjust);
    }

    set_flag(core, FLAG_Z, core->a == 0);
    set_flag(core, FLAG_H, false);
    set_flag(core, FLAG_C, carry);
}

static void cp_a(gb_core_t *core, uint8_t value)
{
    const uint8_t old = core->a;
    const uint8_t result = (uint8_t)(old - value);
    set_flag(core, FLAG_Z, result == 0);
    set_flag(core, FLAG_N, true);
    set_flag(core, FLAG_H, (old & 0x0F) < (value & 0x0F));
    set_flag(core, FLAG_C, old < value);
}

static uint8_t *reg_by_index(gb_core_t *core, uint8_t index)
{
    switch (index) {
    case 0: return &core->b;
    case 1: return &core->c;
    case 2: return &core->d;
    case 3: return &core->e;
    case 4: return &core->h;
    case 5: return &core->l;
    case 7: return &core->a;
    default: return NULL;
    }
}

static uint8_t read_r(gb_core_t *core, uint8_t index)
{
    if (index == 6) {
        return read8(core, gb_core_hl(core));
    }
    uint8_t *reg = reg_by_index(core, index);
    return reg != NULL ? *reg : 0xFF;
}

static void write_r(gb_core_t *core, uint8_t index, uint8_t value)
{
    if (index == 6) {
        write8(core, gb_core_hl(core), value);
        return;
    }
    uint8_t *reg = reg_by_index(core, index);
    if (reg != NULL) {
        *reg = value;
    }
}

static uint8_t cb_rotate_shift(gb_core_t *core, uint8_t op, uint8_t value)
{
    uint8_t result = value;
    bool carry = false;

    switch ((op >> 3) & 0x07) {
    case 0:
        carry = (value & 0x80) != 0;
        result = (uint8_t)((value << 1) | (carry ? 1 : 0));
        break;
    case 1:
        carry = (value & 0x01) != 0;
        result = (uint8_t)((value >> 1) | (carry ? 0x80 : 0));
        break;
    case 2: {
        const bool old_carry = flag_is_set(core, FLAG_C);
        carry = (value & 0x80) != 0;
        result = (uint8_t)((value << 1) | (old_carry ? 1 : 0));
        break;
    }
    case 3: {
        const bool old_carry = flag_is_set(core, FLAG_C);
        carry = (value & 0x01) != 0;
        result = (uint8_t)((value >> 1) | (old_carry ? 0x80 : 0));
        break;
    }
    case 4:
        carry = (value & 0x80) != 0;
        result = (uint8_t)(value << 1);
        break;
    case 5:
        carry = (value & 0x01) != 0;
        result = (uint8_t)((value >> 1) | (value & 0x80));
        break;
    case 6:
        result = (uint8_t)((value << 4) | (value >> 4));
        carry = false;
        break;
    case 7:
        carry = (value & 0x01) != 0;
        result = (uint8_t)(value >> 1);
        break;
    }

    set_flag(core, FLAG_Z, result == 0);
    set_flag(core, FLAG_N, false);
    set_flag(core, FLAG_H, false);
    set_flag(core, FLAG_C, carry);
    return result;
}

static void execute_cb(gb_core_t *core)
{
    const uint8_t op = fetch8(core);
    core->last_opcode = op;
    const uint8_t reg_index = op & 0x07;
    const uint8_t bit = (op >> 3) & 0x07;
    uint8_t value = read_r(core, reg_index);

    if (op < 0x40) {
        write_r(core, reg_index, cb_rotate_shift(core, op, value));
        return;
    }

    if (op < 0x80) {
        set_flag(core, FLAG_Z, (value & (1U << bit)) == 0);
        set_flag(core, FLAG_N, false);
        set_flag(core, FLAG_H, true);
        return;
    }

    if (op < 0xC0) {
        value = (uint8_t)(value & ~(1U << bit));
    } else {
        value = (uint8_t)(value | (1U << bit));
    }
    write_r(core, reg_index, value);
}

void gb_core_init(gb_core_t *core, const uint8_t *rom, size_t rom_size)
{
    memset(core, 0, sizeof(*core));
    core->rom = rom;
    core->rom_size = rom_size;
    core->cartridge_type = rom_size > 0x147 ? rom[0x147] : 0;
    core->cgb_mode = rom_size > 0x143 && (rom[0x143] & 0x80) != 0;
    core->rom_bank = 1;

    if (core->cgb_mode) {
        core->a = 0x11;
        core->f = 0x80;
    } else {
        core->a = 0x01;
        core->f = 0xB0;
    }
    core->b = 0x00;
    core->c = 0x13;
    core->d = 0x00;
    core->e = 0xD8;
    core->h = 0x01;
    core->l = 0x4D;
    core->sp = 0xFFFE;
    core->pc = 0x0100;
    core->io[IO_JOYP] = 0xCF;
    core->io[IO_DIV] = 0xAB;
    core->io[IO_TIMA] = 0x00;
    core->io[IO_TMA] = 0x00;
    core->io[IO_TAC] = 0xF8;
    core->io[IO_IF] = 0xE1;
    core->io[IO_LCDC] = 0x91;
    core->io[IO_STAT] = 0x85;
    core->io[IO_SCY] = 0x00;
    core->io[IO_SCX] = 0x00;
    core->io[IO_LY] = 0x00;
    core->io[IO_LYC] = 0x00;
    core->io[IO_BGP] = 0xFC;
    core->io[IO_OBP0] = 0xFF;
    core->io[IO_OBP1] = 0xFF;
    core->io[IO_WY] = 0x00;
    core->io[IO_WX] = 0x00;
    core->io[IO_VBK] = 0xFE;
    init_cgb_palette(core->bg_palette);
    init_cgb_palette(core->obj_palette);
    core->status = GB_CORE_READY;
}

void gb_core_set_buttons(gb_core_t *core, uint8_t buttons)
{
    if (core == NULL) {
        return;
    }

    const uint8_t previous = core->joypad_buttons;
    core->joypad_buttons = buttons;

    if ((buttons & (uint8_t)~previous) != 0) {
        request_interrupt(core, INT_JOYPAD);
    }
}

void gb_core_step(gb_core_t *core)
{
    if (core == NULL || core->status == GB_CORE_UNSUPPORTED_OPCODE) {
        return;
    }

    if (service_interrupt(core)) {
        return;
    }

    if (core->status == GB_CORE_HALTED || core->status == GB_CORE_STOPPED) {
        const uint16_t cycles = halted_cycles_until_event(core);
        core->halt_ticks += cycles / 4;
        gb_core_tick(core, cycles);
        return;
    }

    core->status = GB_CORE_RUNNING;
    core->last_pc = core->pc;
    const uint8_t opcode = fetch8(core);
    core->last_opcode = opcode;

    if (opcode >= 0x40 && opcode <= 0x7F) {
        if (opcode == 0x76) {
            core->status = GB_CORE_HALTED;
        } else {
            write_r(core, (opcode >> 3) & 0x07, read_r(core, opcode & 0x07));
        }
        finish_instruction(core, 4);
        return;
    }

    if (opcode >= 0x80 && opcode <= 0x87) {
        add_a(core, read_r(core, opcode & 0x07));
        finish_instruction(core, 4);
        return;
    }

    if (opcode >= 0x88 && opcode <= 0x8F) {
        adc_a(core, read_r(core, opcode & 0x07));
        finish_instruction(core, 4);
        return;
    }

    if (opcode >= 0x90 && opcode <= 0x97) {
        sub_a(core, read_r(core, opcode & 0x07));
        finish_instruction(core, 4);
        return;
    }

    if (opcode >= 0x98 && opcode <= 0x9F) {
        sbc_a(core, read_r(core, opcode & 0x07));
        finish_instruction(core, 4);
        return;
    }

    if (opcode >= 0xA0 && opcode <= 0xA7) {
        core->a &= read_r(core, opcode & 0x07);
        core->f = core->a == 0 ? FLAG_Z | FLAG_H : FLAG_H;
        finish_instruction(core, 4);
        return;
    }

    if (opcode >= 0xA8 && opcode <= 0xAF) {
        core->a ^= read_r(core, opcode & 0x07);
        core->f = core->a == 0 ? FLAG_Z : 0;
        finish_instruction(core, 4);
        return;
    }

    if (opcode >= 0xB0 && opcode <= 0xB7) {
        core->a |= read_r(core, opcode & 0x07);
        core->f = core->a == 0 ? FLAG_Z : 0;
        finish_instruction(core, 4);
        return;
    }

    if (opcode >= 0xB8 && opcode <= 0xBF) {
        cp_a(core, read_r(core, opcode & 0x07));
        finish_instruction(core, 4);
        return;
    }

    switch (opcode) {
    case 0x00: break;
    case 0x01: set16(&core->b, &core->c, fetch16(core)); break;
    case 0x02: write8(core, gb_core_bc(core), core->a); break;
    case 0x03: set16(&core->b, &core->c, (uint16_t)(gb_core_bc(core) + 1)); break;
    case 0x04: core->b = inc8(core, core->b); break;
    case 0x05: core->b = dec8(core, core->b); break;
    case 0x06: core->b = fetch8(core); break;
    case 0x07: {
        const bool carry = (core->a & 0x80) != 0;
        core->a = (uint8_t)((core->a << 1) | (carry ? 1 : 0));
        core->f = carry ? FLAG_C : 0;
        break;
    }
    case 0x08: write16(core, fetch16(core), core->sp); break;
    case 0x09: add_hl(core, gb_core_bc(core)); break;
    case 0x0A: core->a = read8(core, gb_core_bc(core)); break;
    case 0x0B: set16(&core->b, &core->c, (uint16_t)(gb_core_bc(core) - 1)); break;
    case 0x0C: core->c = inc8(core, core->c); break;
    case 0x0D: core->c = dec8(core, core->c); break;
    case 0x0E: core->c = fetch8(core); break;
    case 0x0F: {
        const bool carry = (core->a & 0x01) != 0;
        core->a = (uint8_t)((core->a >> 1) | (carry ? 0x80 : 0));
        core->f = carry ? FLAG_C : 0;
        break;
    }
    case 0x10: (void)fetch8(core); break;
    case 0x11: set16(&core->d, &core->e, fetch16(core)); break;
    case 0x12: write8(core, gb_core_de(core), core->a); break;
    case 0x13: set16(&core->d, &core->e, (uint16_t)(gb_core_de(core) + 1)); break;
    case 0x14: core->d = inc8(core, core->d); break;
    case 0x15: core->d = dec8(core, core->d); break;
    case 0x16: core->d = fetch8(core); break;
    case 0x17: {
        const bool old_carry = flag_is_set(core, FLAG_C);
        const bool carry = (core->a & 0x80) != 0;
        core->a = (uint8_t)((core->a << 1) | (old_carry ? 1 : 0));
        core->f = carry ? FLAG_C : 0;
        break;
    }
    case 0x18: {
        const int8_t offset = (int8_t)fetch8(core);
        core->pc = (uint16_t)(core->pc + offset);
        break;
    }
    case 0x19: add_hl(core, gb_core_de(core)); break;
    case 0x1A: core->a = read8(core, gb_core_de(core)); break;
    case 0x1B: set16(&core->d, &core->e, (uint16_t)(gb_core_de(core) - 1)); break;
    case 0x1C: core->e = inc8(core, core->e); break;
    case 0x1D: core->e = dec8(core, core->e); break;
    case 0x1E: core->e = fetch8(core); break;
    case 0x1F: {
        const bool old_carry = flag_is_set(core, FLAG_C);
        const bool carry = (core->a & 0x01) != 0;
        core->a = (uint8_t)((core->a >> 1) | (old_carry ? 0x80 : 0));
        core->f = carry ? FLAG_C : 0;
        break;
    }
    case 0x20: {
        const int8_t offset = (int8_t)fetch8(core);
        if (!flag_is_set(core, FLAG_Z)) {
            core->pc = (uint16_t)(core->pc + offset);
        }
        break;
    }
    case 0x21: gb_core_set_hl(core, fetch16(core)); break;
    case 0x22: write8(core, gb_core_hl(core), core->a); gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) + 1)); break;
    case 0x23: gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) + 1)); break;
    case 0x24: core->h = inc8(core, core->h); break;
    case 0x25: core->h = dec8(core, core->h); break;
    case 0x26: core->h = fetch8(core); break;
    case 0x27: daa(core); break;
    case 0x28: {
        const int8_t offset = (int8_t)fetch8(core);
        if (flag_is_set(core, FLAG_Z)) {
            core->pc = (uint16_t)(core->pc + offset);
        }
        break;
    }
    case 0x29: add_hl(core, gb_core_hl(core)); break;
    case 0x2A: core->a = read8(core, gb_core_hl(core)); gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) + 1)); break;
    case 0x2B: gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) - 1)); break;
    case 0x2C: core->l = inc8(core, core->l); break;
    case 0x2D: core->l = dec8(core, core->l); break;
    case 0x2E: core->l = fetch8(core); break;
    case 0x2F: core->a = (uint8_t)~core->a; core->f = (core->f & (FLAG_Z | FLAG_C)) | FLAG_N | FLAG_H; break;
    case 0x30: {
        const int8_t offset = (int8_t)fetch8(core);
        if (!flag_is_set(core, FLAG_C)) {
            core->pc = (uint16_t)(core->pc + offset);
        }
        break;
    }
    case 0x31: core->sp = fetch16(core); break;
    case 0x32: write8(core, gb_core_hl(core), core->a); gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) - 1)); break;
    case 0x33: core->sp++; break;
    case 0x34: write8(core, gb_core_hl(core), inc8(core, read8(core, gb_core_hl(core)))); break;
    case 0x35: write8(core, gb_core_hl(core), dec8(core, read8(core, gb_core_hl(core)))); break;
    case 0x36: write8(core, gb_core_hl(core), fetch8(core)); break;
    case 0x37: core->f = (core->f & FLAG_Z) | FLAG_C; break;
    case 0x38: {
        const int8_t offset = (int8_t)fetch8(core);
        if (flag_is_set(core, FLAG_C)) {
            core->pc = (uint16_t)(core->pc + offset);
        }
        break;
    }
    case 0x39: add_hl(core, core->sp); break;
    case 0x3A: core->a = read8(core, gb_core_hl(core)); gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) - 1)); break;
    case 0x3B: core->sp--; break;
    case 0x3C: core->a = inc8(core, core->a); break;
    case 0x3D: core->a = dec8(core, core->a); break;
    case 0x3E: core->a = fetch8(core); break;
    case 0x3F: core->f = (core->f & FLAG_Z) | (flag_is_set(core, FLAG_C) ? 0 : FLAG_C); break;
    case 0xC0: if (!flag_is_set(core, FLAG_Z)) core->pc = pop16(core); break;
    case 0xC1: set16(&core->b, &core->c, pop16(core)); break;
    case 0xC2: {
        const uint16_t target = fetch16(core);
        if (!flag_is_set(core, FLAG_Z)) core->pc = target;
        break;
    }
    case 0xC3: core->pc = fetch16(core); break;
    case 0xC4: {
        const uint16_t target = fetch16(core);
        if (!flag_is_set(core, FLAG_Z)) {
            push16(core, core->pc);
            core->pc = target;
        }
        break;
    }
    case 0xC5: push16(core, gb_core_bc(core)); break;
    case 0xC6: add_a(core, fetch8(core)); break;
    case 0xC7: push16(core, core->pc); core->pc = 0x00; break;
    case 0xC8: if (flag_is_set(core, FLAG_Z)) core->pc = pop16(core); break;
    case 0xC9: core->pc = pop16(core); break;
    case 0xCA: {
        const uint16_t target = fetch16(core);
        if (flag_is_set(core, FLAG_Z)) core->pc = target;
        break;
    }
    case 0xCB: execute_cb(core); break;
    case 0xCC: {
        const uint16_t target = fetch16(core);
        if (flag_is_set(core, FLAG_Z)) {
            push16(core, core->pc);
            core->pc = target;
        }
        break;
    }
    case 0xCD: {
        const uint16_t target = fetch16(core);
        push16(core, core->pc);
        core->pc = target;
        break;
    }
    case 0xCE: adc_a(core, fetch8(core)); break;
    case 0xCF: push16(core, core->pc); core->pc = 0x08; break;
    case 0xD0: if (!flag_is_set(core, FLAG_C)) core->pc = pop16(core); break;
    case 0xD1: set16(&core->d, &core->e, pop16(core)); break;
    case 0xD2: {
        const uint16_t target = fetch16(core);
        if (!flag_is_set(core, FLAG_C)) core->pc = target;
        break;
    }
    case 0xD4: {
        const uint16_t target = fetch16(core);
        if (!flag_is_set(core, FLAG_C)) {
            push16(core, core->pc);
            core->pc = target;
        }
        break;
    }
    case 0xD5: push16(core, gb_core_de(core)); break;
    case 0xD6: sub_a(core, fetch8(core)); break;
    case 0xD7: push16(core, core->pc); core->pc = 0x10; break;
    case 0xD8: if (flag_is_set(core, FLAG_C)) core->pc = pop16(core); break;
    case 0xD9: core->pc = pop16(core); core->ime = true; break;
    case 0xDA: {
        const uint16_t target = fetch16(core);
        if (flag_is_set(core, FLAG_C)) core->pc = target;
        break;
    }
    case 0xDC: {
        const uint16_t target = fetch16(core);
        if (flag_is_set(core, FLAG_C)) {
            push16(core, core->pc);
            core->pc = target;
        }
        break;
    }
    case 0xDE: sbc_a(core, fetch8(core)); break;
    case 0xDF: push16(core, core->pc); core->pc = 0x18; break;
    case 0xE0: write8(core, (uint16_t)(0xFF00 + fetch8(core)), core->a); break;
    case 0xE1: gb_core_set_hl(core, pop16(core)); break;
    case 0xE2: write8(core, (uint16_t)(0xFF00 + core->c), core->a); break;
    case 0xE5: push16(core, gb_core_hl(core)); break;
    case 0xE6: core->a &= fetch8(core); core->f = core->a == 0 ? FLAG_Z | FLAG_H : FLAG_H; break;
    case 0xE7: push16(core, core->pc); core->pc = 0x20; break;
    case 0xE8: {
        const int8_t offset = (int8_t)fetch8(core);
        const uint16_t old_sp = core->sp;
        core->sp = (uint16_t)(core->sp + offset);
        set_flag(core, FLAG_Z, false);
        set_flag(core, FLAG_N, false);
        set_flag(core, FLAG_H, ((old_sp & 0x0F) + ((uint8_t)offset & 0x0F)) > 0x0F);
        set_flag(core, FLAG_C, ((old_sp & 0xFF) + (uint8_t)offset) > 0xFF);
        break;
    }
    case 0xE9: core->pc = gb_core_hl(core); break;
    case 0xEA: write8(core, fetch16(core), core->a); break;
    case 0xEE: core->a ^= fetch8(core); core->f = core->a == 0 ? FLAG_Z : 0; break;
    case 0xEF: push16(core, core->pc); core->pc = 0x28; break;
    case 0xF0: core->a = read8(core, (uint16_t)(0xFF00 + fetch8(core))); break;
    case 0xF1: {
        const uint16_t af = pop16(core);
        core->a = (uint8_t)(af >> 8);
        core->f = (uint8_t)(af & 0xF0);
        break;
    }
    case 0xF2: core->a = read8(core, (uint16_t)(0xFF00 + core->c)); break;
    case 0xF3: core->ime = false; core->ime_enable_delay = 0; break;
    case 0xF5: push16(core, gb_core_af(core)); break;
    case 0xF6: core->a |= fetch8(core); core->f = core->a == 0 ? FLAG_Z : 0; break;
    case 0xF7: push16(core, core->pc); core->pc = 0x30; break;
    case 0xF8: {
        const int8_t offset = (int8_t)fetch8(core);
        const uint16_t old_sp = core->sp;
        gb_core_set_hl(core, (uint16_t)(core->sp + offset));
        set_flag(core, FLAG_Z, false);
        set_flag(core, FLAG_N, false);
        set_flag(core, FLAG_H, ((old_sp & 0x0F) + ((uint8_t)offset & 0x0F)) > 0x0F);
        set_flag(core, FLAG_C, ((old_sp & 0xFF) + (uint8_t)offset) > 0xFF);
        break;
    }
    case 0xF9: core->sp = gb_core_hl(core); break;
    case 0xFA: core->a = read8(core, fetch16(core)); break;
    case 0xFB: core->ime_enable_delay = 2; break;
    case 0xFE: cp_a(core, fetch8(core)); break;
    case 0xFF: push16(core, core->pc); core->pc = 0x38; break;
    default:
        core->status = GB_CORE_UNSUPPORTED_OPCODE;
        finish_instruction(core, 4);
        return;
    }

    finish_instruction(core, 4);
}

void gb_core_run(gb_core_t *core, uint32_t max_steps)
{
    for (uint32_t i = 0; i < max_steps; i++) {
        if (core->status == GB_CORE_UNSUPPORTED_OPCODE || core->status == GB_CORE_STOPPED) {
            break;
        }
        gb_core_step(core);
    }
}
