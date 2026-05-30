#pragma once

/*
 * Board profile for the ATK-DNESP32S3 V1.3 + ALIENTEK 4.3" RGBLCD module.
 *
 * The LCD timing values target the ATK-4342 480x272 RGBLCD module. The GPIO map
 * follows ALIENTEK's DNESP32S3 ESP-IDF RGBLCD experiment notes.
 */

/*
 * ALIENTEK 4.3" RGBLCD modules are commonly sold in two variants:
 * - ATK-4342: 480x272
 * - ATK-4384 / MD0430R-800480: 800x480
 *
 * Your first test pattern only occupied the right side of the panel, which is
 * a strong hint that this module may be the 800x480 variant. Keep the settings
 * below on 800x480 first; switch back to the commented 480x272 values if the
 * picture becomes worse.
 */
#define BOARD_LCD_H_RES            800
#define BOARD_LCD_V_RES            480
#define BOARD_LCD_PIXEL_CLOCK_HZ   (20 * 1000 * 1000)

/* 480x272 fallback:
#define BOARD_LCD_H_RES            480
#define BOARD_LCD_V_RES            272
#define BOARD_LCD_PIXEL_CLOCK_HZ   (9 * 1000 * 1000)
*/

/* RGB LCD control signals. The panel is driven in DE mode. */
#define BOARD_LCD_GPIO_PCLK        5
#define BOARD_LCD_GPIO_HSYNC       -1
#define BOARD_LCD_GPIO_VSYNC       -1
#define BOARD_LCD_GPIO_DE          4
#define BOARD_LCD_GPIO_DISP_EN     -1

/* LCD backlight is on XL9555 IO1_3, not a direct ESP32-S3 GPIO. */
#define BOARD_I2C_SDA_GPIO         41
#define BOARD_I2C_SCL_GPIO         42
#define BOARD_XL9555_I2C_ADDR      0x20
#define BOARD_XL9555_LCD_BL_PORT   1
#define BOARD_XL9555_LCD_BL_BIT    3
#define BOARD_LCD_BK_LIGHT_ON      1

/*
 * XL9555 pin masks from ALIENTEK's BSP:
 * P1_7..P1_4 are KEY0..KEY3, active low.
 */
#define BOARD_XL9555_CONFIG_VALUE  0xF003
#define BOARD_BUTTON_KEY0_MASK     0x8000
#define BOARD_BUTTON_KEY1_MASK     0x4000
#define BOARD_BUTTON_KEY2_MASK     0x2000
#define BOARD_BUTTON_KEY3_MASK     0x1000

/* First input bring-up uses the onboard BOOT/K0 button. Active low. */
#define BOARD_BUTTON_BOOT_GPIO     0

/* On-board TF card slot. ALIENTEK's SD example drives it through SPI2. */
#define BOARD_SD_SPI_MOSI_GPIO     11
#define BOARD_SD_SPI_CLK_GPIO      12
#define BOARD_SD_SPI_MISO_GPIO     13
#define BOARD_SD_SPI_CS_GPIO       2

/*
 * RGB565 data bus order expected by esp_lcd RGB panel:
 * B0..B4, G0..G5, R0..R4
 */
#define BOARD_LCD_DATA_GPIO_LIST \
    {                            \
        17, 16, 15, 7, 6,        \
        10, 9, 46, 3, 8, 18,     \
        45, 48, 47, 21, 14       \
    }

/*
 * DE-mode timing copied from ALIENTEK's 23_rgb ESP-IDF example for the
 * ATK-4384 / 800x480 RGBLCD module.
 */
#define BOARD_LCD_HSYNC_BACK_PORCH     88
#define BOARD_LCD_HSYNC_FRONT_PORCH    40
#define BOARD_LCD_HSYNC_PULSE_WIDTH    3
#define BOARD_LCD_VSYNC_BACK_PORCH     32
#define BOARD_LCD_VSYNC_FRONT_PORCH    13
#define BOARD_LCD_VSYNC_PULSE_WIDTH    48

#define BOARD_LCD_BOUNCE_BUFFER_PIXELS (BOARD_LCD_H_RES * 10)
