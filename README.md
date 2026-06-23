# ESP32-S3 Handheld Game Console Prototype

这是一个基于正点原子 `ATK-DNESP32S3 V1.3` 开发板和 4.3 英寸 RGB LCD 的掌机原型项目。

项目目标不是做一个完整成品，而是验证一条学习链路：ESP32-S3 驱动 RGB LCD、读取 TF 卡、扫描按键、加载 Game Boy ROM，并运行一个自写的基础 Game Boy 模拟器。

截至当前阶段，这个项目已经进入阶段性收尾。它能跑起部分 Game Boy 游戏，支持基本输入和存档，但模拟器不完整，音频未实现，性能也达不到原版 Game Boy 接近 60 FPS 的体验。请把它看作一个硬件和固件学习原型，而不是成熟模拟器或可量产掌机方案。

## 当前硬件

- 主控板：ALIENTEK / 正点原子 `ATK-DNESP32S3 V1.3`
- 主芯片：ESP32-S3，8 MB PSRAM
- 屏幕：ALIENTEK 4.3 英寸 RGB LCD，当前按 800x480 RGB 屏使用
- 存储：TF 卡，SPI 接口
- 输入：板载 `BOOT`、`KEY0`、`KEY1`、`KEY2`、`KEY3`
- 常用串口：`COM6`

TF 卡使用 SPI2：

- MOSI: `GPIO11`
- CLK: `GPIO12`
- MISO: `GPIO13`
- CS: `GPIO2`

屏幕分辨率、RGB 时序、GPIO、背光、按键和 TF 卡配置集中在 [main/board_config.h](./main/board_config.h)。

## 已完成内容

- ESP-IDF / CMake 固件工程
- RGB LCD 初始化和帧缓冲绘制
- 基于后备帧缓冲的批量绘制和 `board_present()` 提交
- TF 卡 ROM 扫描和 `.gb` / `.gbc` 文件列表
- Game Boy ROM 头部解析
- ROM 加载到 PSRAM
- 基础 Game Boy CPU、内存映射、计时器、中断和 JOYP 输入
- MBC1、MBC3、MBC5 的基础支持
- save RAM 持久化
- PPU 背景、窗口、精灵的部分渲染
- 2X / 3X 游戏画面显示模式
- 播放界面的性能状态显示：`FPS`、`RUN`、`DRAW`、`PPU`、`BG`、`OBJ`、`OTH`、`LCD`

## 当前局限

这个项目里的 Game Boy 模拟器是教学和验证性质的原型，不是完整准确的模拟器。

- CPU、PPU、窗口、精灵、中断和时序仍有不完整之处
- CGB 支持不完整
- 音频未实现
- 部分 ROM 可能无法运行，或显示、行为不正确
- ESP32-S3 加 800x480 RGB LCD 的主要瓶颈在 PPU 渲染、背景绘制、缩放和帧缓冲写入
- 2X 模式下部分场景约 37 FPS，3X 模式通常在 20 多到 30 多 FPS 之间波动
- 如果目标是更成熟的掌机体验，建议考虑更强的主控或 Linux SoC

## 软件结构

- [main/board_config.h](./main/board_config.h)：板级引脚、LCD 时序、屏幕尺寸、按键、TF 卡配置
- [main/board.c](./main/board.c) / [main/board.h](./main/board.h)：LCD、按键、TF 卡、帧缓冲等板级接口
- [main/game.c](./main/game.c) / [main/game.h](./main/game.h)：菜单、ROM 浏览、Game Boy 播放界面、性能显示
- [main/gb_core.c](./main/gb_core.c) / [main/gb_core.h](./main/gb_core.h)：Game Boy CPU、内存、MBC、计时器、中断、输入和存档
- [main/gb_ppu.c](./main/gb_ppu.c) / [main/gb_ppu.h](./main/gb_ppu.h)：PPU 背景、窗口、精灵渲染和性能统计
- [tools/idf-build.ps1](./tools/idf-build.ps1)：ESP-IDF 构建、烧录、监视脚本
- [PROJECT_OVERVIEW.md](./PROJECT_OVERVIEW.md)：更完整的阶段性项目概览
- [ZHihu_Technical_Retrospective.md](./ZHihu_Technical_Retrospective.md)：项目技术复盘草稿

## 构建和烧录

本项目使用 ESP-IDF，当前开发环境使用：

```text
ESP-IDF v5.5.1
Target: esp32s3
```

推荐使用项目脚本构建：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 build
```

烧录到常用串口 `COM6`：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 -p COM6 flash
```

如果烧录提示串口被占用，通常是串口监视器、GUI 工具或其他程序还在占用这个 COM 口。

也可以双击项目根目录的 `start-gui.cmd` 或 `启动掌机控制台.cmd`，打开本地 Tkinter 控制台进行构建和烧录。

## 使用 TF 卡和 ROM

将 TF 卡格式化为 FAT32，把 `.gb` 或 `.gbc` 文件放到 TF 卡根目录。烧录后从主菜单进入 `ROM BROWSER`，选择 ROM 后按 `BOOT` 读取头部信息，再按 `BOOT` 加载 ROM 并进入 `GB PLAYER`。

基本按键：

- `KEY3`：上移
- `KEY1`：下移
- `KEY0`：刷新 ROM 列表或运行调试片段
- `KEY2`：返回或重置
- `BOOT`：确认、打开、加载或作为组合键

进入 PLAY 模式后，界面左侧会显示性能状态。`FPS` 是实际显示帧率；`RUN` 是模拟逻辑耗时；`DRAW` 是整次绘制耗时；`PPU`、`BG`、`OBJ`、`OTH`、`LCD` 用于拆分画面生成和 LCD 提交成本。

## ROM 和版权说明

仓库不包含任何商业 Game Boy ROM、BIOS 或游戏资源。请只使用你有权使用的 ROM 文件进行测试，不要把 `.gb`、`.gbc`、存档文件或商业游戏素材提交到仓库。

## 阶段结论

这个项目证明了 ESP32-S3、RGB LCD、TF 卡、按键、ROM 加载、存档和基础 Game Boy 画面显示这一整条链路是可行的。

同时它也说明：在 ESP32-S3 和 800x480 RGB LCD 这个组合上，继续追求完整、稳定、满速的 Game Boy 体验会比较吃力。后续如果目标是更接近成品的掌机，更合理的方向可能是 ESP32-P4、高端 STM32H7，或小型 Linux SoC。
