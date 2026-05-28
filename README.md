# SD / ROM browser

The on-board TF slot is wired as SPI2:

- MOSI: GPIO11
- CLK: GPIO12
- MISO: GPIO13
- CS: GPIO2

Format the TF card as FAT32, then copy `.gb` or `.gbc` files to the card root.
After flashing, open `ROM BROWSER` from the main menu. Select a ROM and press
`BOOT` to read its Game Boy header. Press `BOOT` again on the ROM info page to
load the whole ROM into PSRAM and enter the `GB PLAYER` screen.

- `KEY3`: move up
- `KEY1`: move down
- `KEY0`: refresh SD / ROM list
- `KEY2`: back to menu
- `BOOT`: open selected ROM / load from ROM info / return from GB PLAYER
- In `GB PLAYER`: `KEY1` steps one CPU instruction, `KEY0` runs a short CPU slice,
  `KEY2` resets CPU state. The status area shows PC, opcode, registers, ROM bank,
  CPU step count, LCD line (`LY`), and rough emulated cycle count (`CY`).

# Handheld Game Console

第一版目标：使用正点原子 `ATK_DNESP32S3 V1.3` 和 4.3 寸 RGBLCD 做掌机原型。

当前程序是 ESP-IDF/CMake 工程，先完成 LCD bring-up：

- 初始化 ESP32-S3 RGB LCD 外设
- 使用 PSRAM 分配测试帧
- 显示彩条
- 在屏幕中央画出 Game Boy 原始画面区域参考框

## 环境

需要安装 ESP-IDF，并使用 ESP32-S3 目标：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

`COMx` 换成开发板实际串口号。

本机如果使用 `D:\1workandstudy\Espressif` 里的 ESP-IDF v5.5.1，可以直接运行项目脚本：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 build
powershell -ExecutionPolicy Bypass -File .\tools\idf-build.ps1 -p COMx flash monitor
```

也可以双击项目根目录的 `start-gui.cmd` 或 `启动掌机控制台.cmd`，打开本地桌面控制台，直接点按钮编译和烧录。

这个桌面控制台用 Python Tkinter 实现，不需要网页和本地端口。

## 板级配置

屏幕分辨率、时序和 GPIO 都集中在：

```text
main/board_config.h
```

如果烧录后背光亮但屏幕黑屏，优先检查：

1. `BOARD_LCD_DATA_GPIO_LIST`
2. `BOARD_LCD_GPIO_PCLK`
3. `BOARD_LCD_GPIO_HSYNC`
4. `BOARD_LCD_GPIO_VSYNC`
5. `BOARD_LCD_GPIO_DE`
6. `BOARD_XL9555_LCD_BL_PORT` / `BOARD_XL9555_LCD_BL_BIT`

RGBLCD 的 GPIO 表来自正点原子 DNESP32S3 的 ESP-IDF RGBLCD 实验资料；这块板的背光不是直连 GPIO，而是通过 `XL9555 IO1_3` 控制。
