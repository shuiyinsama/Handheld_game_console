#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "board_config.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
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

typedef struct {
    const char *name;
    int gpio;
    bool active_low;
    bool pressed;
    bool last_pressed;
} handheld_button_t;

static handheld_button_t s_buttons[] = {
    {.name = "BOOT", .gpio = BOARD_BUTTON_BOOT_GPIO, .active_low = true},
};

#define BUTTON_TEST_MARGIN 48
#define BUTTON_TEST_GAP    16
#define BUTTON_TEST_COUNT  8
#define BUTTON_TEST_BOX_H  92

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void fill_screen(esp_lcd_panel_handle_t panel, uint16_t color);
static void draw_boot_button(esp_lcd_panel_handle_t panel);

static int button_test_box_w(void)
{
    return (BOARD_LCD_H_RES - BUTTON_TEST_MARGIN * 2 - BUTTON_TEST_GAP * (BUTTON_TEST_COUNT - 1)) /
           BUTTON_TEST_COUNT;
}

static int button_test_y(void)
{
    return (BOARD_LCD_V_RES - BUTTON_TEST_BOX_H) / 2;
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

static void input_init(void)
{
    uint64_t pin_mask = 0;
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        if (s_buttons[i].gpio >= 0) {
            pin_mask |= 1ULL << s_buttons[i].gpio;
        }
    }

    gpio_config_t input_config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input_config));
}

static bool input_scan(void)
{
    bool changed = false;

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        if (s_buttons[i].gpio < 0) {
            continue;
        }

        const int level = gpio_get_level(s_buttons[i].gpio);
        const bool pressed = s_buttons[i].active_low ? (level == 0) : (level != 0);
        s_buttons[i].last_pressed = s_buttons[i].pressed;
        s_buttons[i].pressed = pressed;

        if (s_buttons[i].pressed != s_buttons[i].last_pressed) {
            ESP_LOGI(TAG, "Button %s %s", s_buttons[i].name, s_buttons[i].pressed ? "pressed" : "released");
            changed = true;
        }
    }

    return changed;
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
        .bounce_buffer_size_px = BOARD_LCD_BOUNCE_BUFFER_PIXELS,
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

static void lcd_fill_rect(esp_lcd_panel_handle_t panel, int x0, int y0, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    const int x1 = x0 < 0 ? 0 : x0;
    const int y1 = y0 < 0 ? 0 : y0;
    const int x2 = (x0 + w) > BOARD_LCD_H_RES ? BOARD_LCD_H_RES : (x0 + w);
    const int y2 = (y0 + h) > BOARD_LCD_V_RES ? BOARD_LCD_V_RES : (y0 + h);
    const int clipped_w = x2 - x1;

    if (clipped_w <= 0 || y2 <= y1) {
        return;
    }

    uint16_t *line = heap_caps_malloc(clipped_w * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(line ? ESP_OK : ESP_ERR_NO_MEM);

    for (int x = 0; x < clipped_w; x++) {
        line[x] = color;
    }

    for (int y = y1; y < y2; y++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, x1, y, x2, y + 1, line));
    }

    heap_caps_free(line);
}

static void draw_button_test_screen(esp_lcd_panel_handle_t panel)
{
    fill_screen(panel, rgb565(20, 24, 28));

    const int box_w = button_test_box_w();
    const int y = button_test_y();

    for (int i = 0; i < BUTTON_TEST_COUNT; i++) {
        const int x = BUTTON_TEST_MARGIN + i * (box_w + BUTTON_TEST_GAP);
        lcd_fill_rect(panel, x, y, box_w, BUTTON_TEST_BOX_H, rgb565(48, 56, 64));
        lcd_fill_rect(panel, x + 4, y + 4, box_w - 8, BUTTON_TEST_BOX_H - 8, rgb565(32, 38, 44));
    }

    draw_boot_button(panel);
}

static void draw_boot_button(esp_lcd_panel_handle_t panel)
{
    const int box_w = button_test_box_w();
    const int y = button_test_y();

    const uint16_t boot_color = s_buttons[0].pressed ? rgb565(80, 220, 120) : rgb565(82, 96, 108);
    lcd_fill_rect(panel, BUTTON_TEST_MARGIN + 4, y + 4, box_w - 8, BUTTON_TEST_BOX_H - 8, boot_color);

    /* Small fixed markers: left is BOOT/K0, remaining slots are future D-pad/A/B/Start/Select. */
    lcd_fill_rect(panel, BUTTON_TEST_MARGIN + 18, y + 18, box_w - 36, 10, rgb565(10, 14, 18));
    lcd_fill_rect(panel, BUTTON_TEST_MARGIN + 18, y + BUTTON_TEST_BOX_H - 28, box_w - 36, 10, rgb565(10, 14, 18));
}

static void fill_screen(esp_lcd_panel_handle_t panel, uint16_t color)
{
    lcd_fill_rect(panel, 0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, color);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Booting handheld LCD bring-up");
    ESP_ERROR_CHECK(board_i2c_init());
    input_init();
    lcd_backlight_on();

    esp_lcd_panel_handle_t panel = lcd_init();

    input_scan();
    draw_button_test_screen(panel);

    ESP_LOGI(TAG, "Button test screen drawn");

    while (true) {
        if (input_scan()) {
            draw_boot_button(panel);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
