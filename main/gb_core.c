#include "gb_core.h"

#include <string.h>

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

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

static uint8_t read8(const gb_core_t *core, uint16_t addr)
{
    if (addr < 0x8000) {
        return addr < core->rom_size ? core->rom[addr] : 0xFF;
    }
    if (addr < 0xA000) {
        return core->vram[addr - 0x8000];
    }
    if (addr < 0xC000) {
        return core->eram[addr - 0xA000];
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
            return 0xCF;
        }
        return core->io[addr - 0xFF00];
    }
    if (addr < 0xFFFF) {
        return core->hram[addr - 0xFF80];
    }
    return core->ie;
}

static void write8(gb_core_t *core, uint16_t addr, uint8_t value)
{
    if (addr < 0x8000) {
        return;
    }
    if (addr < 0xA000) {
        core->vram[addr - 0x8000] = value;
        return;
    }
    if (addr < 0xC000) {
        core->eram[addr - 0xA000] = value;
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
        core->io[addr - 0xFF00] = value;
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

static void sub_a(gb_core_t *core, uint8_t value)
{
    const uint8_t old = core->a;
    core->a = (uint8_t)(core->a - value);
    set_flag(core, FLAG_Z, core->a == 0);
    set_flag(core, FLAG_N, true);
    set_flag(core, FLAG_H, (old & 0x0F) < (value & 0x0F));
    set_flag(core, FLAG_C, old < value);
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

void gb_core_init(gb_core_t *core, const uint8_t *rom, size_t rom_size)
{
    memset(core, 0, sizeof(*core));
    core->rom = rom;
    core->rom_size = rom_size;

    core->a = 0x11;
    core->f = 0x80;
    core->b = 0x00;
    core->c = 0x13;
    core->d = 0x00;
    core->e = 0xD8;
    core->h = 0x01;
    core->l = 0x4D;
    core->sp = 0xFFFE;
    core->pc = 0x0100;
    core->io[0x40] = 0x91;
    core->io[0x44] = 0x00;
    core->status = GB_CORE_READY;
}

void gb_core_step(gb_core_t *core)
{
    if (core == NULL || core->status == GB_CORE_UNSUPPORTED_OPCODE) {
        return;
    }

    if (core->status == GB_CORE_HALTED || core->status == GB_CORE_STOPPED) {
        core->steps++;
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
        core->steps++;
        return;
    }

    if (opcode >= 0x80 && opcode <= 0x87) {
        add_a(core, read_r(core, opcode & 0x07));
        core->steps++;
        return;
    }

    if (opcode >= 0x90 && opcode <= 0x97) {
        sub_a(core, read_r(core, opcode & 0x07));
        core->steps++;
        return;
    }

    if (opcode >= 0xA0 && opcode <= 0xA7) {
        core->a &= read_r(core, opcode & 0x07);
        core->f = core->a == 0 ? FLAG_Z | FLAG_H : FLAG_H;
        core->steps++;
        return;
    }

    if (opcode >= 0xA8 && opcode <= 0xAF) {
        core->a ^= read_r(core, opcode & 0x07);
        core->f = core->a == 0 ? FLAG_Z : 0;
        core->steps++;
        return;
    }

    if (opcode >= 0xB0 && opcode <= 0xB7) {
        core->a |= read_r(core, opcode & 0x07);
        core->f = core->a == 0 ? FLAG_Z : 0;
        core->steps++;
        return;
    }

    if (opcode >= 0xB8 && opcode <= 0xBF) {
        cp_a(core, read_r(core, opcode & 0x07));
        core->steps++;
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
    case 0x0A: core->a = read8(core, gb_core_bc(core)); break;
    case 0x0C: core->c = inc8(core, core->c); break;
    case 0x0D: core->c = dec8(core, core->c); break;
    case 0x0E: core->c = fetch8(core); break;
    case 0x11: set16(&core->d, &core->e, fetch16(core)); break;
    case 0x12: write8(core, gb_core_de(core), core->a); break;
    case 0x13: set16(&core->d, &core->e, (uint16_t)(gb_core_de(core) + 1)); break;
    case 0x14: core->d = inc8(core, core->d); break;
    case 0x15: core->d = dec8(core, core->d); break;
    case 0x16: core->d = fetch8(core); break;
    case 0x18: core->pc = (uint16_t)(core->pc + (int8_t)fetch8(core)); break;
    case 0x1A: core->a = read8(core, gb_core_de(core)); break;
    case 0x1C: core->e = inc8(core, core->e); break;
    case 0x1D: core->e = dec8(core, core->e); break;
    case 0x1E: core->e = fetch8(core); break;
    case 0x20: {
        const int8_t offset = (int8_t)fetch8(core);
        if (!flag_is_set(core, FLAG_Z)) core->pc = (uint16_t)(core->pc + offset);
        break;
    }
    case 0x21: gb_core_set_hl(core, fetch16(core)); break;
    case 0x22: write8(core, gb_core_hl(core), core->a); gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) + 1)); break;
    case 0x23: gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) + 1)); break;
    case 0x24: core->h = inc8(core, core->h); break;
    case 0x25: core->h = dec8(core, core->h); break;
    case 0x26: core->h = fetch8(core); break;
    case 0x28: {
        const int8_t offset = (int8_t)fetch8(core);
        if (flag_is_set(core, FLAG_Z)) core->pc = (uint16_t)(core->pc + offset);
        break;
    }
    case 0x2A: core->a = read8(core, gb_core_hl(core)); gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) + 1)); break;
    case 0x2B: gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) - 1)); break;
    case 0x2C: core->l = inc8(core, core->l); break;
    case 0x2D: core->l = dec8(core, core->l); break;
    case 0x2E: core->l = fetch8(core); break;
    case 0x30: {
        const int8_t offset = (int8_t)fetch8(core);
        if (!flag_is_set(core, FLAG_C)) core->pc = (uint16_t)(core->pc + offset);
        break;
    }
    case 0x31: core->sp = fetch16(core); break;
    case 0x32: write8(core, gb_core_hl(core), core->a); gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) - 1)); break;
    case 0x33: core->sp++; break;
    case 0x34: write8(core, gb_core_hl(core), inc8(core, read8(core, gb_core_hl(core)))); break;
    case 0x35: write8(core, gb_core_hl(core), dec8(core, read8(core, gb_core_hl(core)))); break;
    case 0x36: write8(core, gb_core_hl(core), fetch8(core)); break;
    case 0x38: {
        const int8_t offset = (int8_t)fetch8(core);
        if (flag_is_set(core, FLAG_C)) core->pc = (uint16_t)(core->pc + offset);
        break;
    }
    case 0x3A: core->a = read8(core, gb_core_hl(core)); gb_core_set_hl(core, (uint16_t)(gb_core_hl(core) - 1)); break;
    case 0x3C: core->a = inc8(core, core->a); break;
    case 0x3D: core->a = dec8(core, core->a); break;
    case 0x3E: core->a = fetch8(core); break;
    case 0xC1: set16(&core->b, &core->c, pop16(core)); break;
    case 0xC3: core->pc = fetch16(core); break;
    case 0xC5: push16(core, gb_core_bc(core)); break;
    case 0xC9: core->pc = pop16(core); break;
    case 0xCD: {
        const uint16_t target = fetch16(core);
        push16(core, core->pc);
        core->pc = target;
        break;
    }
    case 0xD1: set16(&core->d, &core->e, pop16(core)); break;
    case 0xD5: push16(core, gb_core_de(core)); break;
    case 0xE0: write8(core, (uint16_t)(0xFF00 + fetch8(core)), core->a); break;
    case 0xE1: gb_core_set_hl(core, pop16(core)); break;
    case 0xE2: write8(core, (uint16_t)(0xFF00 + core->c), core->a); break;
    case 0xE5: push16(core, gb_core_hl(core)); break;
    case 0xEA: write8(core, fetch16(core), core->a); break;
    case 0xF0: core->a = read8(core, (uint16_t)(0xFF00 + fetch8(core))); break;
    case 0xF1: {
        const uint16_t af = pop16(core);
        core->a = (uint8_t)(af >> 8);
        core->f = (uint8_t)(af & 0xF0);
        break;
    }
    case 0xF3: core->ime = false; break;
    case 0xF5: push16(core, gb_core_af(core)); break;
    case 0xF9: core->sp = gb_core_hl(core); break;
    case 0xFA: core->a = read8(core, fetch16(core)); break;
    case 0xFB: core->ime = true; break;
    case 0xFE: cp_a(core, fetch8(core)); break;
    default:
        core->status = GB_CORE_UNSUPPORTED_OPCODE;
        core->steps++;
        return;
    }

    core->steps++;
}

void gb_core_run(gb_core_t *core, uint32_t max_steps)
{
    for (uint32_t i = 0; i < max_steps; i++) {
        if (core->status == GB_CORE_UNSUPPORTED_OPCODE || core->status == GB_CORE_HALTED || core->status == GB_CORE_STOPPED) {
            break;
        }
        gb_core_step(core);
    }
}
