# Project Overview

## Goal

Build a small ESP32-S3 handheld game console prototype on the ALIENTEK / 正点原子 ATK-DNESP32S3 board with a 4.3 inch RGB LCD. The current milestone is a Game Boy / Game Boy Color ROM loader plus an early emulator/debug screen.

This is an ESP-IDF C project managed by CMake.

## Current Hardware

- Main board: ATK-DNESP32S3 V1.3, ESP32-S3 with 8 MB PSRAM.
- Display: ALIENTEK 4.3 inch RGBLCD, detected/treated as 800x480 RGB panel.
- Storage: onboard TF card slot, used in SPI mode.
- Input: BOOT GPIO plus KEY0..KEY3 through XL9555.

## Important Pin/Timing Data

The active board profile is in `main/board_config.h`.

LCD:

- Resolution: 800x480
- Pixel clock: 18 MHz
- PCLK: GPIO5
- DE: GPIO4
- HSYNC/VSYNC: unused, DE mode
- Data pins, ESP-IDF RGB bus order B0..B4, G0..G5, R0..R4:
  - B: GPIO17, GPIO16, GPIO15, GPIO7, GPIO6
  - G: GPIO10, GPIO9, GPIO46, GPIO3, GPIO8, GPIO18
  - R: GPIO45, GPIO48, GPIO47, GPIO21, GPIO14
- Timing:
  - HBP 88, HFP 40, HPW 3
  - VBP 32, VFP 13, VPW 48

I2C / XL9555:

- SDA: GPIO41
- SCL: GPIO42
- XL9555 address: `0x20`
- LCD backlight: XL9555 IO1_3

Buttons:

- BOOT: GPIO0 active-low
- KEY0: XL9555 P1_7, active-low
- KEY1: XL9555 P1_6, active-low
- KEY2: XL9555 P1_5, active-low
- KEY3: XL9555 P1_4, active-low
- Current movement mapping: KEY3 up, KEY1 down, KEY2 left, KEY0 right.

TF card:

- MOSI: GPIO11
- CLK: GPIO12
- MISO: GPIO13
- CS: GPIO2
- Format: FAT32
- ROM location: `.gb` / `.gbc` files in TF card root.

## Software Layout

- `main/app_main.c`: board init and game loop entry.
- `main/board.c/.h`: LCD, framebuffer, input, RGB565 drawing helpers.
- `main/board_config.h`: board-specific GPIO/timing config.
- `main/game.c/.h`: menu, input test, ROM browser, GB player/debug UI.
- `main/storage.c/.h`: TF card mount, ROM list, header parsing, ROM loading to PSRAM.
- `main/gb_player.c/.h`: ROM-loaded player wrapper around the core.
- `main/gb_core.c/.h`: early Game Boy CPU/memory/timer/LCD timing core.
- `main/gb_ppu.c/.h`: early PPU background tile preview renderer.
- `tools/idf-build.ps1`: local ESP-IDF build/flash wrapper for `D:\1workandstudy\Espressif`.
- `start-gui.cmd` / `启动掌机控制台.cmd`: double-click local desktop control panel entry.

## Build And Flash

Typical build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 build
```

Flash to the board, usually COM6 on the current machine:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 -p COM6 flash
```

The local GUI can also build/flash/monitor without typing commands.

## Current State

Working:

- LCD bring-up is stable at 800x480.
- Backlight control works through XL9555.
- Input scan works for BOOT and KEY0..KEY3.
- TF card mount and ROM browser work.
- ROM header parsing works; user has tested a ROM showing `FEEDITSQULS`, 1024 KB, MBC5 BAT, CGB only.
- ROM can be loaded into PSRAM.
- Early CPU core runs far enough to write VRAM.
- Early PPU background tile preview renders visible tile-map output.
- RGB LCD flicker was greatly reduced by switching to a double-framebuffer style based on ALIENTEK examples.

Not complete yet:

- Game Boy emulation is still early and incomplete.
- Only background tile preview is rendered, not full PPU scanline output.
- Window layer, sprite layer, CGB palettes/attributes, audio, input-to-JOYP, save RAM persistence, and accurate timing are not done.
- CPU can enter `HALTED`; this can be normal, but current interrupt/HALT behavior still needs validation.

## Reference Material Used

Local board examples are under:

```text
D:\1workandstudy\doc
```

Most relevant examples:

- `4，程序源码\v5.1.2版本的例程\1，标准例程-IDF版\basic_routines\23_rgb`
- `4，程序源码\v5.3.x版本的例程\4，扩展例程-IDF版\4，LVGL例程\01_lvgl_transplant`

Important pattern copied from the later LVGL/RGBLCD examples:

- Use RGB LCD frame buffers from `esp_lcd_rgb_panel_get_frame_buffer`.
- Use `num_fbs = 2`.
- Register frame/bounce completion callbacks.
- Draw into a back buffer, then present whole frames rather than constantly pushing tiny rectangles directly to the panel.
