# Agent Notes

## Working Style

This project is being developed incrementally on real hardware. Prefer small, testable steps. The user will often flash the firmware and report what appears on the physical LCD.

As of 2026-05-31, this project is at a stage-end/prototype closure point. Do not assume the next task is to keep forcing full-speed Game Boy emulation on ESP32-S3 unless the user explicitly reopens that direction.

When changing hardware-facing code, follow the ALIENTEK / Zhengdian Atom examples in `D:\1workandstudy\doc` whenever possible. Do not guess pin mappings, RGB LCD timing, or framebuffer behavior if an example exists.

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

If flashing reports COM port access denied, the monitor, GUI, or another serial tool is probably still holding the port.

## Coding Guidelines

- Use C and ESP-IDF conventions already present in the project.
- Keep board-specific values in `main/board_config.h`.
- Keep LCD/input/SD primitives in `main/board.c/.h`.
- Keep emulator logic out of board code.
- Use `apply_patch` for edits.
- Build after each meaningful firmware change.
- Do not replace the current LCD driver with a new stack unless the user asks for a larger refactor.
- Avoid web dependencies; this is a local ESP-IDF project.

## LCD Guidance

The RGB LCD path should follow the ALIENTEK examples rather than direct tiny-rectangle immediate drawing.

Current approach:

- `board_fill_rect`, `board_fill_screen`, and `board_draw_rgb565_bitmap` draw into a back framebuffer.
- `board_begin_frame` / `board_end_frame` batch drawing.
- `board_present` submits the frame and waits for frame completion.
- The current RGB panel configuration uses multiple frame buffers and a bounce buffer, matching the stable ALIENTEK-style approach reached during hardware testing.

When adding a new screen or frequently updated UI:

- Wrap multi-operation rendering in `board_begin_frame()` and `board_end_frame()`.
- Avoid repeated full-screen clears inside tight loops.
- For fast animation, update a fixed region, then present once per visual frame.
- If flicker returns, check against the RGBLCD/LVGL examples in the local docs before inventing a new method.

## Game Boy Emulator Guidance

Current emulator is a teaching/prototype core, not a complete emulator.

Implemented and tested pieces include:

- ROM scanning/loading from TF card.
- Header parsing and ROM loading into PSRAM.
- MBC1, MBC3, and MBC5 basics.
- Save RAM persistence.
- JOYP input mapping.
- Partial CPU/timer/LCD interrupt behavior.
- Partial PPU background/window/sprite rendering.
- 2X and 3X display modes, with 2X used as the practical default.

Important performance/debug fields shown in play/debug screens include:

- `FPS`
- `RUN`
- `DRAW`
- `PPU`
- `BG`, `OBJ`, `OTH`
- `LCD`
- Cache/reason indicators such as `H`, `S`, `V`, `R`, and `I`

Game Boy display facts:

- Original Game Boy resolution is 160x144.
- 2X output is 320x288.
- 3X output is 480x432.

Current performance conclusion:

- ESP32-S3 CPU execution is no longer the main bottleneck.
- The limiting cost is the scaled PPU/framebuffer drawing path, especially background rendering during scrolling scenes.
- Representative 2X data reported by the user: about 37 FPS, `RUN` around 2 ms, `DRAW` around 24 ms, `PPU` around 15 ms, `BG` around 13 ms.
- Representative 3X data reached roughly the mid-20s to low-30s FPS depending on scene.
- Static scenes can be cached; moving scenes often miss because of scroll or VRAM changes.
- Several optimization attempts were tested. Some helped, but ESP32-S3 plus this 800x480 RGB panel is not a comfortable full-speed GB target without further compromises such as frame skip, lower scale, less UI, or a stronger chip.

If development resumes on ESP32-S3:

1. Treat the emulator as a learning/prototype target.
2. Keep 2X as the default play scale.
3. Keep audio out until video/input are stable and performance has margin.
4. Avoid reintroducing experiments that caused flicker or layout instability.
5. Prefer small measurable optimizations with on-screen timing fields.

If the goal changes to a polished handheld or GBA-class system, consider stronger hardware such as ESP32-P4-class parts, high-end STM32H7 designs with external memory/display acceleration, or a small Linux-capable SoC.

## Git / Generated Files

Do not commit build output. Ignore or remove temporary extracted video frame folders if created during debugging.

The user previously pushed an earlier version to git. There may be uncommitted emulator/LCD/doc changes. Ask before committing or pushing.
