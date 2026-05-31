# Session Notes

## Stage-End Summary

This session brought the handheld project from board bring-up to a playable Game Boy prototype on real hardware. The project is now being paused at a natural checkpoint because the remaining performance work is running into the practical limits of ESP32-S3 plus the current 800x480 RGB LCD path.

## Hardware Confirmed

- Board: ATK-DNESP32S3 V1.3, ESP32-S3 with 8 MB PSRAM.
- Display: ALIENTEK 4.3-inch RGB LCD, treated as an 800x480 RGB panel.
- Storage: TF card over SPI.
- Input: BOOT plus KEY0 to KEY3 through the board input path.
- Typical serial port during testing: COM6.
- The LCD works through the RGB ribbon cable. The small yellow SPI-style header on the LCD module was not needed for the current RGB mode.

## Software Milestones

- Created and maintained an ESP-IDF/CMake firmware project.
- Added local scripts and a desktop control panel workflow for build, flash, and monitor.
- Brought up the RGB LCD and fixed early flicker by following the ALIENTEK-style framebuffer approach.
- Added stable batched drawing through `board_begin_frame`, `board_end_frame`, and `board_present`.
- Added TF card ROM scanning and ROM selection.
- Added Game Boy ROM header parsing and loading into PSRAM.
- Added a basic Game Boy emulator core with partial CPU, timer, interrupt, LCD, PPU, MBC, input, and save RAM behavior.
- Added play/debug screens and on-screen performance counters.
- Tested with `josplanet_demo_jam.gb` and Pokemon Yellow.
- Confirmed save RAM persistence with Pokemon Yellow.

## Current User-Facing State

- The firmware can boot to a ROM/player interface.
- ROM information can be shown.
- Some GB ROMs can enter play mode and show recognizable graphics.
- Input works.
- Save RAM exists and has been tested.
- 2X and 3X display modes were explored.
- 2X is the practical default for performance.

## Performance Notes

Game Boy resolution:

- Native: 160x144.
- 2X: 320x288.
- 3X: 480x432.

Representative measurements reported by the user:

- 3X mode: around 26 to 33 FPS depending on scene.
- 2X mode: around 37 FPS in some scenes.
- 2X reported example: `FPS 37`, `RUN 2ms`, `DRAW 24ms`, `PPU 15ms`, `BG 13ms`, reason `S`.

The current bottleneck is not mainly CPU execution. `RUN` has been reduced to a small part of the frame time. The heavier cost is in the PPU/background rendering and scaled framebuffer writes, especially while scrolling. Static scenes can benefit from caching, while scrolling scenes miss the cache and become expensive again.

## Optimization Attempts

Useful or partially useful changes:

- Stable full-frame RGB presentation instead of immediate small-rectangle LCD writes.
- Back framebuffer drawing.
- Performance timing split for `RUN`, `DRAW`, `PPU`, `BG`, `OBJ`, `OTH`, and `LCD`.
- 2X scaling mode.
- Background/tile caching experiments.
- Reduced flicker by keeping LCD presentation stable and avoiding unsafe buffer updates.

Experiments that were not worth keeping or should be treated carefully:

- Aggressive scroll/source cache experiments caused little gain or flicker/layout instability.
- Frame skipping improved responsiveness but sacrificed visual quality, so it was not treated as the preferred final direction.
- Higher apparent FPS sometimes introduced sprite flicker until the timing/presentation path was made more conservative.

## Current Technical Conclusion

The ESP32-S3 and this 800x480 RGB LCD can demonstrate a GB handheld prototype, but they are not an ideal platform for a polished full-speed Game Boy experience at comfortable 2X/3X scale. The project has succeeded as a hardware/software learning prototype and has exposed the main architecture constraint: video rendering and scaled framebuffer movement dominate the frame budget.

For a polished handheld, the next serious step should probably be stronger hardware rather than endless micro-optimizations on this board.

Recommended future hardware directions:

- ESP32-P4-class board with stronger display acceleration potential.
- High-end STM32H7 with external SDRAM and a display pipeline.
- Linux-capable handheld SoC if the goal expands to stronger emulation or a richer UI.

GBA emulation should not be expected on this ESP32-S3 setup. It belongs in a different hardware tier.

## If This Project Resumes

Good next steps on the current ESP32-S3 project:

1. Keep 2X mode as the default.
2. Keep the UI/debug overlay lightweight.
3. Avoid adding audio until video has real margin.
4. Improve emulator correctness before chasing marginal FPS gains.
5. Test changes on small ROMs and Pokemon Yellow.
6. Keep following ALIENTEK examples for LCD changes.

Good next steps for the broader handheld goal:

1. Document this ESP32-S3 prototype as the learning board.
2. Choose the next main chip based on display bandwidth, RAM, and emulator target.
3. Treat GBA as a later architecture goal, not a continuation of this exact ESP32-S3 firmware.

## Git Notes

No final commit or push was requested during this stage-end documentation update. Ask the user before committing or pushing.
