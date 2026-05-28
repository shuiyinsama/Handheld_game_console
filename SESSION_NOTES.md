# Session Notes

## Summary

This session brought the project from board/display bring-up to an early Game Boy ROM loader and emulator debug view.

The current physical result:

- LCD no longer flickers during auto-run after adopting a double-framebuffer approach.
- ROM loads from TF card.
- `GB PLAYER` screen shows CPU registers and PPU diagnostics.
- Pressing `KEY0 AUTO` runs the core and eventually shows visible background tile patterns on the right preview area.
- The latest observed screen showed `OP 76` / `HALTED` with visible tile-map output and nonzero `BG` values.

## Hardware Confirmed

- Board: ATK_DNESP32S3 V1.3.
- Display: ALIENTEK 4.3 inch RGBLCD, used as 800x480 RGB panel.
- The screen is connected through the 40-pin RGB ribbon. The yellow SPI header on the LCD is not needed for this RGB mode.
- USB-C powers the board for current testing.
- TF card / ROM loading works.

## Important Fixes Already Made

1. LCD init was corrected for this panel and board:
   - 800x480
   - DE mode
   - correct RGB GPIO order
   - backlight through XL9555 IO1_3

2. LCD test patterns worked:
   - first color cycling
   - then stable UI

3. Button mapping was corrected:
   - KEY3 up
   - KEY1 down
   - KEY2 left
   - KEY0 right

4. TF card support was added:
   - FAT32
   - `.gb` / `.gbc` ROM list in root
   - header read and checksum display

5. GB core and ROM loading were added:
   - basic MBC1/MBC3/MBC5 bank switching
   - basic memory map
   - early CPU opcode coverage
   - timer and LCD line counters

6. PPU preview was added:
   - reads LCDC, SCX, SCY, BGP, VRAM tile data and tile map
   - draws 160x144 background preview scaled 2x

7. LCD flicker was fixed:
   - direct tiny draw calls were replaced with back-buffer drawing
   - RGB panel configured with two framebuffers
   - full frame submitted through `board_present`
   - rendering screens wrapped in `board_begin_frame` / `board_end_frame`

8. CPU gaps fixed during testing:
   - added `0x3B DEC SP`
   - added `0xF2 LD A,(FF00+C)`
   - improved `EI` to enable interrupts after a delay rather than immediately

## Latest User Observation

The user said:

```text
halted
```

The previous screenshot showed:

- `PC 4138 OP 76`
- status `HALTED`
- `STP 679936`
- visible right-side tile pattern
- `LCD 89 BG 16 352`

This is not automatically a failure. `OP 76` is the Game Boy `HALT` instruction. Games often execute HALT while waiting for VBlank/timer/input interrupts.

The newest firmware now also displays:

```text
IE xx IF xx IME x
```

Use that line to decide whether HALT is normal or whether interrupts are stuck.

## What To Check Next

Ask the user for a photo after flashing the latest build if needed. In the `GB PLAYER` screen, look at:

- Is `STP` still increasing?
- Is `CY` still increasing?
- Is `LY` still changing?
- What are `IE`, `IF`, and `IME`?
- Does status stay `HALTED` forever?

Interpretation:

- If `HALTED`, `IME 1`, and `IE & IF` has a pending bit, interrupt service may be wrong.
- If `HALTED`, `IME 0`, and no enabled pending interrupts exist, it may be waiting for something else such as input/timer behavior.
- If `STP/CY/LY` keep moving, it is not frozen in the simple sense.
- If `BG` values are nonzero and preview has patterns, PPU background extraction is working at least partially.

## Recommended Next Development Step

Stay aligned with examples and work incrementally:

1. Validate HALT interrupt wake-up behavior using on-screen `IE/IF/IME`.
2. Add JOYP input register behavior so the ROM can see buttons.
3. Improve PPU:
   - render window layer
   - render sprites/OAM
   - support CGB palette/attributes later
4. Reduce debug UI update rate once the emulator is stable enough, because full-frame debug redraw is useful now but not final-game behavior.

## Build Status

The latest build completed successfully with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 build
```

No final commit was made during this pause.
