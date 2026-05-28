#include "board.h"

#include "board_config.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "board";

#define I2C_MASTER_PORT        I2C_NUM_0
#define I2C_MASTER_FREQ_HZ     400000
#define XL9555_REG_INPUT0      0x00
#define XL9555_REG_OUTPUT0     0x02
#define XL9555_REG_CONFIG0     0x06

typedef struct {
    const char *name;
    int gpio;
    uint16_t xl9555_mask;
    bool active_low;
    bool pressed;
    bool last_pressed;
} button_hw_t;

static button_hw_t s_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_BOOT] = {.name = "BOOT", .gpio = BOARD_BUTTON_BOOT_GPIO, .xl9555_mask = 0, .active_low = true},
    [BOARD_BUTTON_KEY0] = {.name = "KEY0", .gpio = -1, .xl9555_mask = BOARD_BUTTON_KEY0_MASK, .active_low = true},
    [BOARD_BUTTON_KEY1] = {.name = "KEY1", .gpio = -1, .xl9555_mask = BOARD_BUTTON_KEY1_MASK, .active_low = true},
    [BOARD_BUTTON_KEY2] = {.name = "KEY2", .gpio = -1, .xl9555_mask = BOARD_BUTTON_KEY2_MASK, .active_low = true},
    [BOARD_BUTTON_KEY3] = {.name = "KEY3", .gpio = -1, .xl9555_mask = BOARD_BUTTON_KEY3_MASK, .active_low = true},
};

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_frame_buffers[2];
static uint8_t s_back_buffer_index;
static uint8_t s_batch_depth;
static bool s_frame_dirty;
static int s_dirty_x1;
static int s_dirty_y1;
static int s_dirty_x2;
static int s_dirty_y2;
static volatile bool s_frame_done;

void board_present(void);

uint16_t board_rgb565(uint8_t r, uint8_t g, uint8_t b)
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

static esp_err_t xl9555_write_reg_pair(uint8_t reg, uint16_t value)
{
    const uint8_t data[3] = {
        reg,
        (uint8_t)(value & 0xFF),
        (uint8_t)(value >> 8),
    };

    return i2c_master_write_to_device(I2C_MASTER_PORT, BOARD_XL9555_I2C_ADDR, data, sizeof(data), pdMS_TO_TICKS(100));
}

static esp_err_t xl9555_read_inputs(uint16_t *inputs)
{
    uint8_t port0 = 0;
    uint8_t port1 = 0;

    ESP_RETURN_ON_ERROR(xl9555_read_reg(XL9555_REG_INPUT0, &port0), TAG, "XL9555 input0 read failed");
    ESP_RETURN_ON_ERROR(xl9555_read_reg(XL9555_REG_INPUT0 + 1, &port1), TAG, "XL9555 input1 read failed");

    *inputs = ((uint16_t)port1 << 8) | port0;
    return ESP_OK;
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

static esp_err_t input_init(void)
{
    uint64_t pin_mask = 0;
    bool has_gpio_input = false;
    for (size_t i = 0; i < BOARD_BUTTON_COUNT; i++) {
        if (s_buttons[i].gpio >= 0) {
            pin_mask |= 1ULL << s_buttons[i].gpio;
            has_gpio_input = true;
        }
    }

    if (has_gpio_input) {
        gpio_config_t input_config = {
            .pin_bit_mask = pin_mask,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&input_config), TAG, "Button GPIO config failed");
    }

    return xl9555_write_reg_pair(XL9555_REG_CONFIG0, BOARD_XL9555_CONFIG_VALUE);
}

static esp_err_t lcd_backlight_on(void)
{
    const uint8_t port = BOARD_XL9555_LCD_BL_PORT;
    const uint8_t bit_mask = 1U << BOARD_XL9555_LCD_BL_BIT;
    const uint8_t output_reg = XL9555_REG_OUTPUT0 + port;
    const uint8_t config_reg = XL9555_REG_CONFIG0 + port;

    uint8_t output = 0;
    uint8_t config = 0xFF;

    ESP_RETURN_ON_ERROR(xl9555_read_reg(output_reg, &output), TAG, "LCD backlight output read failed");
    ESP_RETURN_ON_ERROR(xl9555_read_reg(config_reg, &config), TAG, "LCD backlight config read failed");

    config &= (uint8_t)~bit_mask;
    if (BOARD_LCD_BK_LIGHT_ON) {
        output |= bit_mask;
    } else {
        output &= (uint8_t)~bit_mask;
    }

    ESP_RETURN_ON_ERROR(xl9555_write_reg(output_reg, output), TAG, "LCD backlight output write failed");
    return xl9555_write_reg(config_reg, config);
}

static bool lcd_frame_done_callback(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;
    s_frame_done = true;
    return false;
}

static esp_err_t lcd_init(void)
{
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .num_fbs = 2,
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

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, &s_panel), TAG, "RGB panel create failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "RGB panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "RGB panel init failed");
    void *frame_buffer0 = NULL;
    void *frame_buffer1 = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &frame_buffer0, &frame_buffer1),
        TAG,
        "RGB frame buffer get failed");
    s_frame_buffers[0] = frame_buffer0;
    s_frame_buffers[1] = frame_buffer1;

    const esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_frame_buf_complete = lcd_frame_done_callback,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_rgb_panel_register_event_callbacks(s_panel, &callbacks, NULL),
        TAG,
        "RGB callback register failed");

    memset(s_frame_buffers[0], 0, BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t));
    memset(s_frame_buffers[1], 0, BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t));

    esp_err_t disp_ret = esp_lcd_panel_disp_on_off(s_panel, true);
    if (disp_ret != ESP_OK && disp_ret != ESP_ERR_NOT_SUPPORTED) {
        return disp_ret;
    }

    return ESP_OK;
}

static uint16_t *back_buffer(void)
{
    return s_frame_buffers[s_back_buffer_index];
}

static void mark_dirty_rect(int x1, int y1, int x2, int y2)
{
    if (x2 <= x1 || y2 <= y1) {
        return;
    }
    if (!s_frame_dirty) {
        s_dirty_x1 = x1;
        s_dirty_y1 = y1;
        s_dirty_x2 = x2;
        s_dirty_y2 = y2;
    } else {
        if (x1 < s_dirty_x1) {
            s_dirty_x1 = x1;
        }
        if (y1 < s_dirty_y1) {
            s_dirty_y1 = y1;
        }
        if (x2 > s_dirty_x2) {
            s_dirty_x2 = x2;
        }
        if (y2 > s_dirty_y2) {
            s_dirty_y2 = y2;
        }
    }
    s_frame_dirty = true;
    if (s_batch_depth == 0) {
        board_present();
    }
}

esp_err_t board_init(void)
{
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(input_init(), TAG, "Input init failed");
    ESP_RETURN_ON_ERROR(lcd_backlight_on(), TAG, "LCD backlight init failed");
    return lcd_init();
}

bool board_input_scan(board_input_t *input)
{
    bool changed = false;
    uint16_t xl9555_inputs = 0xFFFF;
    bool needs_xl9555 = false;

    for (size_t i = 0; i < BOARD_BUTTON_COUNT; i++) {
        needs_xl9555 |= s_buttons[i].xl9555_mask != 0;
    }

    if (needs_xl9555) {
        ESP_ERROR_CHECK(xl9555_read_inputs(&xl9555_inputs));
    }

    for (size_t i = 0; i < BOARD_BUTTON_COUNT; i++) {
        bool pressed = false;

        if (s_buttons[i].gpio >= 0) {
            const int level = gpio_get_level(s_buttons[i].gpio);
            pressed = s_buttons[i].active_low ? (level == 0) : (level != 0);
        } else if (s_buttons[i].xl9555_mask != 0) {
            const bool level_high = (xl9555_inputs & s_buttons[i].xl9555_mask) != 0;
            pressed = s_buttons[i].active_low ? !level_high : level_high;
        }

        s_buttons[i].last_pressed = s_buttons[i].pressed;
        s_buttons[i].pressed = pressed;
        input->pressed[i] = pressed;
        input->changed[i] = pressed != s_buttons[i].last_pressed;

        if (input->changed[i]) {
            ESP_LOGI(TAG, "Button %s %s", s_buttons[i].name, pressed ? "pressed" : "released");
            changed = true;
        }
    }

    return changed;
}

void board_fill_rect(int x0, int y0, int w, int h, uint16_t color)
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

    uint16_t *canvas = back_buffer();
    for (int y = y1; y < y2; y++) {
        uint16_t *row = &canvas[y * BOARD_LCD_H_RES + x1];
        for (int x = 0; x < clipped_w; x++) {
            row[x] = color;
        }
    }
    mark_dirty_rect(x1, y1, x2, y2);
}

void board_fill_screen(uint16_t color)
{
    uint16_t *canvas = back_buffer();
    for (size_t i = 0; i < BOARD_LCD_H_RES * BOARD_LCD_V_RES; i++) {
        canvas[i] = color;
    }
    mark_dirty_rect(0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
}

void board_draw_rgb565_bitmap(int x0, int y0, int w, int h, const uint16_t *pixels)
{
    if (w <= 0 || h <= 0 || pixels == NULL) {
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

    uint16_t *canvas = back_buffer();
    for (int y = y1; y < y2; y++) {
        const int src_y = y - y0;
        const int src_x = x1 - x0;
        memcpy(&canvas[y * BOARD_LCD_H_RES + x1], &pixels[src_y * w + src_x], clipped_w * sizeof(uint16_t));
    }
    mark_dirty_rect(x1, y1, x2, y2);
}

void board_begin_frame(void)
{
    s_batch_depth++;
}

void board_end_frame(void)
{
    if (s_batch_depth == 0) {
        return;
    }
    s_batch_depth--;
    if (s_batch_depth == 0) {
        board_present();
    }
}

void board_present(void)
{
    if (!s_frame_dirty || s_panel == NULL || s_frame_buffers[0] == NULL || s_frame_buffers[1] == NULL) {
        return;
    }

    uint16_t *front = back_buffer();
    const int dirty_x1 = s_dirty_x1;
    const int dirty_y1 = s_dirty_y1;
    const int dirty_x2 = s_dirty_x2;
    const int dirty_y2 = s_dirty_y2;

    s_frame_done = false;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, front));

    const TickType_t start = xTaskGetTickCount();
    while (!s_frame_done && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(100)) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    s_back_buffer_index ^= 1;
    uint16_t *next_back = back_buffer();
    for (int y = dirty_y1; y < dirty_y2; y++) {
        memcpy(
            &next_back[y * BOARD_LCD_H_RES + dirty_x1],
            &front[y * BOARD_LCD_H_RES + dirty_x1],
            (size_t)(dirty_x2 - dirty_x1) * sizeof(uint16_t));
    }
    s_frame_dirty = false;
}
