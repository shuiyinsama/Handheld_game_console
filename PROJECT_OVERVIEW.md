# 项目概览

## 目标

本项目的目标是从零开始制作一个掌机原型。当前阶段使用 ATK-DNESP32S3 开发板、ALIENTEK 4.3 英寸 RGB LCD、TF 卡和板载按键，验证“自制硬件 + 自写固件 + Game Boy 模拟器”的完整链路。

截至 2026-05-31，本项目进入阶段性收尾。结论是：ESP32-S3 加这块 800x480 RGB LCD 可以做出可玩的 Game Boy 原型，但想要稳定、流畅、完整地跑 GB，尤其是在 2X 或 3X 画面下，已经接近这个方案的实际上限。这个阶段不是失败，而是完成了硬件验证、LCD 驱动验证、ROM 加载、输入、存档和基础模拟器验证。

## 当前硬件

- 主控板：ATK-DNESP32S3 V1.3。
- 主芯片：ESP32-S3，带 8 MB PSRAM。
- 屏幕：ALIENTEK 4.3 英寸 RGB LCD，当前按 800x480 RGB 屏使用。
- 存储：TF 卡，SPI 接口。
- 输入：BOOT、KEY0、KEY1、KEY2、KEY3。
- 常用串口：COM6。

屏幕当前通过 RGB 排线工作。屏幕板上的黄色 SPI 排针不是当前 RGB 显示模式必须使用的接口。

## 分辨率

Game Boy 原始分辨率是 160x144。

当前项目里测试过的显示比例：

- 1X：160x144，最快，但画面太小。
- 2X：320x288，当前更适合作为默认游玩模式。
- 3X：480x432，画面更大，但视频渲染压力明显更高。

## 软件结构

主要文件和职责：

- `CMakeLists.txt`：ESP-IDF 工程入口。
- `main/CMakeLists.txt`：主固件组件配置。
- `main/board_config.h`：板级引脚、LCD 时序、屏幕尺寸、按键、TF 卡等配置。
- `main/board.c` / `main/board.h`：LCD、按键、TF 卡、帧缓冲等板级接口。
- `main/app_main.c`：应用主流程和界面状态。
- `main/game.c` / `main/game.h`：ROM 列表、玩家界面、游玩模式、调试界面和性能显示。
- `main/gb_core.c` / `main/gb_core.h`：Game Boy CPU、内存、MBC、计时器、中断、输入、存档等核心逻辑。
- `main/gb_ppu.c` / `main/gb_ppu.h`：Game Boy PPU 背景、窗口、精灵渲染和部分性能统计。
- `main/font8x8_basic.h`：简单点阵字体。
- `tools/idf-build.ps1`：构建、烧录、监视脚本。
- `tools/start-gui.ps1`、`start-gui.cmd`、`启动掌机控制台.cmd`：本地控制台启动入口。

## 当前已经完成

- ESP-IDF / CMake 工程可构建。
- 本地双击启动的构建/烧录工具可用。
- RGB LCD 已稳定点亮。
- LCD 绘制方式已改成更接近 ALIENTEK 例程的帧缓冲批量刷新方式。
- 解决过早期黑屏、花屏、闪屏、画面整体移动等问题。
- TF 卡可读取 ROM 文件。
- 可解析 GB/GBC ROM 头部信息。
- ROM 可加载到 PSRAM。
- 基础 Game Boy 模拟器可运行部分 ROM。
- 已支持基础 MBC1、MBC3、MBC5。
- 已支持 JOYP 输入映射。
- 已支持 save RAM 持久化，宝可梦黄测试过存档。
- 已能进入 PLAY 模式显示游戏画面。
- 已添加 FPS、RUN、DRAW、PPU、BG、OBJ、OTH、LCD 等性能指标。
- 2X 和 3X 显示模式都测试过。

## 当前局限

- 这不是完整准确的 Game Boy 模拟器。
- 计时、CPU、PPU、窗口、精灵、中断等仍有不完整之处。
- 音频尚未做。
- CGB 支持不完整。
- 部分 ROM 的显示或行为可能不正确。
- GBA 不适合继续在这套 ESP32-S3 方案上实现。
- 当前最大瓶颈不是 CPU 执行，而是 PPU / BG 渲染和缩放后的帧缓冲写入。

## 性能结论

当前典型测试数据：

- 2X 模式部分场景约 37 FPS。
- 2X 代表数据：`RUN 2ms`、`DRAW 24ms`、`PPU 15ms`、`BG 13ms`。
- 3X 模式通常在 20 多到 30 多 FPS 之间波动。
- 移动画面、滚动背景、背景缓存失效时，BG / PPU / DRAW 会明显升高。

也就是说，CPU 模拟本身已经不是最主要的问题。真正吃时间的是把 Game Boy 的背景、窗口、精灵渲染出来，再按 2X 或 3X 写入 800x480 RGB LCD 使用的帧缓冲。

静止画面可以靠缓存提高速度；一旦游戏场景滚动，缓存命中率下降，性能就会掉下来。这说明继续在 ESP32-S3 上做小优化会有收益，但收益会越来越小。

## 为什么阶段性收尾

这个阶段已经证明：

- 板子能用。
- 屏幕能用。
- TF 卡能用。
- 输入能用。
- ROM 能加载。
- 存档能保存。
- GB 游戏能显示并操作。

同时也证明：

- ESP32-S3 加 800x480 RGB LCD 跑 GB 原型可以。
- 想做稳定、完整、流畅的掌机体验，这个硬件组合比较吃力。
- 如果目标升级到更成熟的掌机，下一步更应该换更强的主控，而不是继续把大量时间花在 ESP32-S3 上硬榨性能。

## 后续建议

如果以后继续基于当前 ESP32-S3 做学习项目：

1. 保持 2X 作为默认画面。
2. 不急着加音频。
3. 先提高模拟器正确性，再考虑少量性能优化。
4. UI 和调试信息尽量轻量。
5. LCD 相关改动继续参考 `D:\1workandstudy\doc` 里的 ALIENTEK / 正点原子例程。

如果目标是做更像成品的掌机：

1. 考虑 ESP32-P4 这一类更强的芯片。
2. 考虑带外部 SDRAM 和显示加速能力的高端 STM32H7。
3. 如果目标包含更复杂系统、更多模拟器或 GBA，考虑小型 Linux SoC。
4. 把当前 ESP32-S3 项目当作第一代验证板，而不是最终硬件方案。

## 常用命令

构建：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 build
```

烧录：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 -p COM6 flash
```

如果烧录提示 COM 口被占用，通常是串口监视器、控制台工具或其他程序还在占用 COM6。

## Git 状态提醒

本次阶段性收尾只整理文档。是否提交、推送到 Git，需要用户单独确认。
