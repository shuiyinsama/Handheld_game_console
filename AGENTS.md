# Agent Notes

## Working Style

This project is being developed incrementally on real hardware. Prefer small, testable steps. The user will often flash and report what appears on the physical LCD.

When changing hardware-facing code, follow the ALIENTEK / 正点原子 examples in `D:\1workandstudy\doc` whenever possible. Do not guess pin mappings or RGB LCD behavior if an example exists.

## Environment

Project root:

```text
D:\0CodexData\worktrees\762a\Handheld_game_console
```

ESP-IDF install used by project scripts:

```text
D:\1workandstudy\Espressif\frameworks\esp-idf-v5.5.1
```

Build command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 build
```

Flash command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 -p COM6 flash
```

If flashing reports COM port access denied, the monitor or another serial tool is probably still holding the port.

## Coding Guidelines

- Use C and ESP-IDF conventions already present in the project.
- Keep board-specific values in `main/board_config.h`.
- Keep LCD/input primitives in `main/board.c/.h`.
- Keep emulator logic out of board code.
- Use `apply_patch` for edits.
- Build after each meaningful change.
- Do not replace the current LCD driver with a new stack unless the user asks for a larger refactor.
- Avoid web dependencies; this is a local ESP-IDF project.

## LCD Guidance

The RGB LCD should follow the ALIENTEK examples rather than direct tiny-rectangle immediate drawing.

Current approach:

- `board_fill_rect`, `board_fill_screen`, and `board_draw_rgb565_bitmap` draw into the back framebuffer.
- `board_begin_frame` / `board_end_frame` batches drawing.
- `board_present` submits the full 800x480 frame and waits for frame completion.

When adding a new screen or frequently updated UI:

- Wrap multi-operation rendering in `board_begin_frame()` and `board_end_frame()`.
- Avoid repeated full-screen clears inside tight loops.
- For fast animation, update a fixed region, then present once per visual frame.
- If flicker returns, check against the RGBLCD/LVGL examples in the local docs before inventing a new method.

## Game Boy Emulator Guidance

Current emulator is a teaching/prototype core, not a complete emulator.

Do next steps in this order:

1. Stabilize CPU/interrupt/HALT behavior.
2. Add useful on-screen diagnostics before deeper changes.
3. Improve PPU background/window/sprite rendering.
4. Add JOYP input mapping.
5. Add save RAM persistence.
6. Consider audio only after video/input are stable.

Important current debug fields shown in `GB PLAYER`:

- `PC`, `OP`
- `AF`, `BC`, `DE`, `HL`
- `BNK`, CPU status
- `STP`, `LY`, `CY`
- `LCD`, `BG`
- `IE`, `IF`, `IME`

If the user reports `HALTED`, do not immediately treat it as a fatal error. On Game Boy, many games halt while waiting for VBlank/timer/input interrupts. Check whether `IE & IF` has pending enabled interrupts and whether `IME` is set.

## Git / Generated Files

Do not commit build output. Ignore or remove temporary extracted video frame folders if created during debugging.

The user previously pushed an earlier version to git. There are currently uncommitted emulator/LCD changes. Ask before committing or pushing.
