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
#define BOARD_LCD_PIXEL_CLOCK_HZ   (30 * 1000 * 1000)

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
 * RGB565 data bus order expected by esp_lcd RGB panel:
 * B0..B4, G0..G5, R0..R4
 */
#define BOARD_LCD_DATA_GPIO_LIST \
    {                            \
        17, 16, 15, 7, 6,        \
        10, 9, 46, 3, 8, 18,     \
        45, 48, 47, 21, 14       \
    }

/* DE-mode timing. H/V sync pins are not connected on this board. */
#define BOARD_LCD_HSYNC_BACK_PORCH     40
#define BOARD_LCD_HSYNC_FRONT_PORCH    20
#define BOARD_LCD_HSYNC_PULSE_WIDTH    1
#define BOARD_LCD_VSYNC_BACK_PORCH     8
#define BOARD_LCD_VSYNC_FRONT_PORCH    4
#define BOARD_LCD_VSYNC_PULSE_WIDTH    1
