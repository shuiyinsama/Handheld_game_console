#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "board_config.h"
#include "esp_cache.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "handheld";

#define I2C_MASTER_PORT        I2C_NUM_0
#define I2C_MASTER_FREQ_HZ     400000
#define XL9555_REG_OUTPUT0     0x02
#define XL9555_REG_CONFIG0     0x06

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static esp_err_t xl9555_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_master_write_to_device(
        I2C_MASTER_PORT,
        BOARD_XL9555_I2C_ADDR,
        (uint8_t[]){reg, value},
        2,
        pdMS_TO_TICKS(100));
}

static esp_err_t xl9555_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_write_read_device(
        I2C_MASTER_PORT,
        BOARD_XL9555_I2C_ADDR,
        &reg,
        1,
        value,
        1,
        pdMS_TO_TICKS(100));
}

static esp_err_t board_i2c_init(void)
{
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_MASTER_PORT, &i2c_config), TAG, "I2C config failed");
    return i2c_driver_install(I2C_MASTER_PORT, i2c_config.mode, 0, 0, 0);
}

static void lcd_backlight_on(void)
{
    const uint8_t port = BOARD_XL9555_LCD_BL_PORT;
    const uint8_t bit_mask = 1U << BOARD_XL9555_LCD_BL_BIT;
    const uint8_t output_reg = XL9555_REG_OUTPUT0 + port;
    const uint8_t config_reg = XL9555_REG_CONFIG0 + port;

    uint8_t output = 0;
    uint8_t config = 0xFF;

    ESP_ERROR_CHECK(xl9555_read_reg(output_reg, &output));
    ESP_ERROR_CHECK(xl9555_read_reg(config_reg, &config));

    config &= (uint8_t)~bit_mask;
    if (BOARD_LCD_BK_LIGHT_ON) {
        output |= bit_mask;
    } else {
        output &= (uint8_t)~bit_mask;
    }

    ESP_ERROR_CHECK(xl9555_write_reg(output_reg, output));
    ESP_ERROR_CHECK(xl9555_write_reg(config_reg, config));
}

static esp_lcd_panel_handle_t lcd_init(void)
{
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = {
            .pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT_PORCH,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE_WIDTH,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT_PORCH,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE_WIDTH,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .psram_trans_align = 64,
        .hsync_gpio_num = BOARD_LCD_GPIO_HSYNC,
        .vsync_gpio_num = BOARD_LCD_GPIO_VSYNC,
        .de_gpio_num = BOARD_LCD_GPIO_DE,
        .pclk_gpio_num = BOARD_LCD_GPIO_PCLK,
        .disp_gpio_num = BOARD_LCD_GPIO_DISP_EN,
        .data_gpio_nums = BOARD_LCD_DATA_GPIO_LIST,
        .flags.fb_in_psram = true,
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    esp_err_t disp_ret = esp_lcd_panel_disp_on_off(panel, true);
    if (disp_ret != ESP_OK && disp_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_ERROR_CHECK(disp_ret);
    }

    return panel;
}

static uint16_t *lcd_get_frame_buffer(esp_lcd_panel_handle_t panel)
{
    void *frame_buffer = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &frame_buffer));
    return (uint16_t *)frame_buffer;
}

static void draw_color_bars(uint16_t *frame)
{
    const uint16_t bars[] = {
        0xFFFF,
        rgb565(255, 255, 0),
        rgb565(0, 255, 255),
        rgb565(0, 255, 0),
        rgb565(255, 0, 255),
        rgb565(255, 0, 0),
        rgb565(0, 0, 255),
        0x0000,
    };

    for (int y = 0; y < BOARD_LCD_V_RES; y++) {
        for (int x = 0; x < BOARD_LCD_H_RES; x++) {
            frame[y * BOARD_LCD_H_RES + x] = bars[(x * 8) / BOARD_LCD_H_RES];
        }
    }
}

static void draw_rect(uint16_t *frame, int x0, int y0, int w, int h, uint16_t color)
{
    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= BOARD_LCD_V_RES) {
            continue;
        }

        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= BOARD_LCD_H_RES) {
                continue;
            }

            frame[y * BOARD_LCD_H_RES + x] = color;
        }
    }
}

static void draw_gameboy_viewport(uint16_t *frame)
{
    int scale = BOARD_LCD_H_RES / 160;
    const int scale_y = BOARD_LCD_V_RES / 144;
    if (scale > scale_y) {
        scale = scale_y;
    }
    if (scale < 1) {
        scale = 1;
    }

    const int gb_w = 160 * scale;
    const int gb_h = 144 * scale;
    const int gb_x = (BOARD_LCD_H_RES - gb_w) / 2;
    const int gb_y = (BOARD_LCD_V_RES - gb_h) / 2;

    draw_rect(frame, gb_x - 4, gb_y - 4, gb_w + 8, gb_h + 8, rgb565(32, 32, 32));
    draw_rect(frame, gb_x, gb_y, gb_w, gb_h, rgb565(155, 188, 15));

    const int tile = 8 * scale;
    for (int y = 0; y < gb_h; y += tile) {
        for (int x = 0; x < gb_w; x += tile) {
            if (((x + y) / tile) & 1) {
                draw_rect(frame, gb_x + x, gb_y + y, tile, tile, rgb565(139, 172, 15));
            }
        }
    }

    draw_rect(frame, gb_x + 12 * scale, gb_y + 12 * scale, gb_w - 24 * scale, 4 * scale, rgb565(15, 56, 15));
    draw_rect(frame, gb_x + 12 * scale, gb_y + gb_h - 16 * scale, gb_w - 24 * scale, 4 * scale, rgb565(15, 56, 15));
}

static void draw_alignment_marks(uint16_t *frame)
{
    const int mark = 24;

    draw_rect(frame, 0, 0, BOARD_LCD_H_RES, 4, 0xFFFF);
    draw_rect(frame, 0, BOARD_LCD_V_RES - 4, BOARD_LCD_H_RES, 4, 0xFFFF);
    draw_rect(frame, 0, 0, 4, BOARD_LCD_V_RES, 0xFFFF);
    draw_rect(frame, BOARD_LCD_H_RES - 4, 0, 4, BOARD_LCD_V_RES, 0xFFFF);

    draw_rect(frame, 0, 0, mark, mark, rgb565(255, 0, 0));
    draw_rect(frame, BOARD_LCD_H_RES - mark, 0, mark, mark, rgb565(0, 255, 0));
    draw_rect(frame, 0, BOARD_LCD_V_RES - mark, mark, mark, rgb565(0, 0, 255));
    draw_rect(frame, BOARD_LCD_H_RES - mark, BOARD_LCD_V_RES - mark, mark, mark, rgb565(255, 255, 255));
}

static void fill_screen(uint16_t *frame, uint16_t color)
{
    for (int y = 0; y < BOARD_LCD_V_RES; y++) {
        for (int x = 0; x < BOARD_LCD_H_RES; x++) {
            frame[y * BOARD_LCD_H_RES + x] = color;
        }
    }
}

static void lcd_sync_frame_buffer(uint16_t *frame)
{
    ESP_ERROR_CHECK(esp_cache_msync(
        frame,
        BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t),
        ESP_CACHE_MSYNC_FLAG_DIR_C2M));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Booting handheld LCD bring-up");
    ESP_ERROR_CHECK(board_i2c_init());
    lcd_backlight_on();

    esp_lcd_panel_handle_t panel = lcd_init();

    uint16_t *frame = lcd_get_frame_buffer(panel);

    draw_color_bars(frame);
    draw_gameboy_viewport(frame);
    draw_alignment_marks(frame);
    lcd_sync_frame_buffer(frame);

    ESP_LOGI(TAG, "LCD test pattern drawn");

    const struct {
        const char *name;
        uint16_t color;
    } smoke_test_colors[] = {
        {"black", rgb565(0, 0, 0)},
        {"white", rgb565(255, 255, 255)},
        {"red", rgb565(255, 0, 0)},
        {"green", rgb565(0, 255, 0)},
        {"blue", rgb565(0, 0, 255)},
    };

    int color_index = 0;
    while (true) {
        fill_screen(frame, smoke_test_colors[color_index].color);
        lcd_sync_frame_buffer(frame);
        ESP_LOGI(TAG, "LCD smoke test color: %s", smoke_test_colors[color_index].name);
        color_index = (color_index + 1) % (sizeof(smoke_test_colors) / sizeof(smoke_test_colors[0]));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
