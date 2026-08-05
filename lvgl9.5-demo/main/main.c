/**
 * @file main.c
 * @brief ESP32-S3 立创实战派开发板 ST7789 SPI LCD LVGL 演示程序
 * @note 物理分辨率: 320x240 (ST7789), SPI 接口, RGB565 16位色
 * @note 支持竖屏/横屏运行时切换 (GPIO0 BOOT 按钮)
 * @note 支持 FT6336 触摸屏 (I2C 地址: 0x38, FT5x06 兼容)
 * @note 使用 PCA9557 I2C IO 扩展芯片控制 LCD_CS
 *
 * SPDX-FileCopyrightText: 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pca9557.h"
#include <string.h>

// LVGL
#include "demos/lv_demos.h"
#include "lvgl.h"

static const char *TAG = "main";

// ============================================================================
// 硬件配置 - 立创实战派 ESP32-S3
// ============================================================================

// LCD 分辨率
#define LCD_H_RES               320
#define LCD_V_RES               240

// LCD SPI 引脚
#define LCD_SPI_HOST            SPI3_HOST
#define LCD_SPI_MOSI_GPIO       GPIO_NUM_40
#define LCD_SPI_SCLK_GPIO       GPIO_NUM_41
#define LCD_SPI_MISO_GPIO       GPIO_NUM_NC
#define LCD_DC_GPIO             GPIO_NUM_39
#define LCD_CS_GPIO             GPIO_NUM_NC         // 由 PCA9557 控制
#define LCD_RST_GPIO            GPIO_NUM_NC         // 与硬件复位共用
#define LCD_SPI_MODE            2
#define LCD_SPI_CLK_HZ          (80 * 1000 * 1000)

// LCD 方向配置
#define LCD_SWAP_XY             true
#define LCD_MIRROR_X            true
#define LCD_MIRROR_Y            false
#define LCD_INVERT_COLOR        true

// 背光控制 (GPIO42, 低电平有效)
#define LCD_BL_GPIO             GPIO_NUM_42
#define LCD_BL_INVERT           true
#define LCD_BL_LEDC_TIMER       LEDC_TIMER_0
#define LCD_BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define LCD_BL_LEDC_DUTY_RES    LEDC_TIMER_13_BIT
#define LCD_BL_LEDC_FREQ        5000
#define LCD_BL_LEDC_DUTY_MAX    8191

// I2C 总线 (触摸屏 + PCA9557 共用)
#define I2C_BUS_PORT            (i2c_port_t)1
#define I2C_SDA_GPIO            GPIO_NUM_1
#define I2C_SCL_GPIO            GPIO_NUM_2
#define I2C_CLK_HZ              (400 * 1000)

// PCA9557 I2C 地址
#define PCA9557_ADDR            0x19

// 触摸屏 (FT6336/FT5x06)
#define TOUCH_I2C_ADDR          ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS

// 按钮
#define BUTTON_GPIO             GPIO_NUM_0          // BOOT 键

// 初始显示方向 (0=竖屏, 1=横屏)
#define DEFAULT_IS_LANDSCAPE    true

// ============================================================================
// 全局变量
// ============================================================================

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static pca9557_dev_t *s_pca9557 = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static lv_indev_t *s_indev = NULL;
static lv_display_t *s_disp = NULL;
static bool s_is_landscape = DEFAULT_IS_LANDSCAPE;

// ============================================================================
// 背光控制
// ============================================================================

static esp_err_t backlight_init(void) {
    ESP_LOGI(TAG, "初始化背光 (GPIO%d, %s)...",
             LCD_BL_GPIO, LCD_BL_INVERT ? "低电平有效" : "高电平有效");

    ledc_timer_config_t timer = {
        .speed_mode = LCD_BL_LEDC_MODE,
        .timer_num = LCD_BL_LEDC_TIMER,
        .duty_resolution = LCD_BL_LEDC_DUTY_RES,
        .freq_hz = LCD_BL_LEDC_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "LEDC 定时器失败");

    ledc_channel_config_t channel = {
        .speed_mode = LCD_BL_LEDC_MODE,
        .channel = LCD_BL_LEDC_CHANNEL,
        .timer_sel = LCD_BL_LEDC_TIMER,
        .gpio_num = LCD_BL_GPIO,
        .duty = LCD_BL_INVERT ? LCD_BL_LEDC_DUTY_MAX : 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "LEDC 通道失败");

    return ESP_OK;
}

static esp_err_t backlight_set(uint8_t percent) {
    uint32_t duty = (LCD_BL_LEDC_DUTY_MAX * percent) / 100;
    if (LCD_BL_INVERT) {
        duty = LCD_BL_LEDC_DUTY_MAX - duty;
    }
    ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL);
    ESP_LOGD(TAG, "背光: %d%%", percent);
    return ESP_OK;
}

// ============================================================================
// I2C 总线初始化
// ============================================================================

static esp_err_t i2c_bus_init(void) {
    ESP_LOGI(TAG, "初始化 I2C 总线 (SDA=GPIO%d, SCL=GPIO%d)...", I2C_SDA_GPIO, I2C_SCL_GPIO);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus),
                        TAG, "I2C 总线创建失败");

    ESP_LOGI(TAG, "I2C 总线初始化完成");
    return ESP_OK;
}

// ============================================================================
// SPI 总线初始化
// ============================================================================

static esp_err_t spi_bus_init(void) {
    ESP_LOGI(TAG, "初始化 SPI 总线 (MOSI=GPIO%d, SCLK=GPIO%d)...",
             LCD_SPI_MOSI_GPIO, LCD_SPI_SCLK_GPIO);

    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_SPI_MOSI_GPIO,
        .miso_io_num = LCD_SPI_MISO_GPIO,
        .sclk_io_num = LCD_SPI_SCLK_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI 总线初始化失败");

    ESP_LOGI(TAG, "SPI 总线初始化完成");
    return ESP_OK;
}

// ============================================================================
// LCD 初始化 (ST7789 SPI)
// ============================================================================

static esp_err_t lcd_init(void) {
    ESP_LOGI(TAG, "初始化 ST7789 SPI LCD...");

    // 1. 初始化 I2C (PCA9557)
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "I2C 初始化失败");

    // 2. 初始化 PCA9557
    ESP_RETURN_ON_ERROR(pca9557_init(s_i2c_bus, PCA9557_ADDR, &s_pca9557),
                        TAG, "PCA9557 初始化失败");

    // 3. 初始化 SPI
    ESP_RETURN_ON_ERROR(spi_bus_init(), TAG, "SPI 初始化失败");

    // 4. 初始化背光
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "背光初始化失败");
    backlight_set(0);

    // 5. 创建 SPI Panel IO
    ESP_LOGI(TAG, "创建 SPI Panel IO...");
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS_GPIO,
        .dc_gpio_num = LCD_DC_GPIO,
        .spi_mode = LCD_SPI_MODE,
        .pclk_hz = LCD_SPI_CLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_cfg, &s_panel_io),
                        TAG, "SPI Panel IO 创建失败");

    // 6. 创建 ST7789 面板
    ESP_LOGI(TAG, "创建 ST7789 面板...");
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_panel_io, &panel_cfg, &s_panel),
                        TAG, "ST7789 面板创建失败");

    // 7. 硬件复位 LCD (通过 PCA9557 拉低 LCD_CS 模拟复位)
    ESP_LOGI(TAG, "复位 LCD...");
    esp_lcd_panel_reset(s_panel);

    // 8. LCD_CS 拉低 (片选有效)
    pca9557_set_output(s_pca9557, PCA9557_IO_LCD_CS, 0);

    // 9. 初始化面板
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "ST7789 初始化失败");

    // 10. 颜色/方向配置
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, LCD_INVERT_COLOR),
                        TAG, "颜色反转失败");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY),
                        TAG, "坐标翻转失败");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y),
                        TAG, "镜像设置失败");

    // 11. 开启显示
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true),
                        TAG, "开启显示失败");

    ESP_LOGI(TAG, "LCD 初始化完成 (%dx%d)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

// ============================================================================
// 触摸屏初始化 (FT6336 / FT5x06)
// ============================================================================

static esp_err_t touch_init(void) {
    ESP_LOGI(TAG, "初始化 FT6336/FT5x06 触摸屏...");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    // 创建 I2C Panel IO 用于触摸屏
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = TOUCH_I2C_ADDR,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .flags = {
            .disable_control_phase = 1,
        },
    };
    tp_io_config.scl_speed_hz = I2C_CLK_HZ;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_config, &tp_io_handle),
                        TAG, "触摸 I2C IO 创建失败");

    // 创建 FT5x06 触摸设备
    esp_err_t ret = esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &s_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FT5x06 触摸初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "触摸屏初始化完成 (FT6336/FT5x06)");
    return ESP_OK;
}

static esp_err_t touch_register_lvgl(void) {
    if (!s_touch) {
        ESP_LOGE(TAG, "触摸设备未初始化");
        return ESP_FAIL;
    }

    s_indev = lv_indev_create();
    if (!s_indev) {
        ESP_LOGE(TAG, "创建 LVGL 输入设备失败");
        return ESP_FAIL;
    }
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);

    // 使用 esp_lcd_touch 读取回调
    lv_indev_set_read_cb(s_indev, esp_lcd_touch_read_data);
    lv_indev_set_user_data(s_indev, s_touch);

    ESP_LOGI(TAG, "触摸屏已注册到 LVGL");
    return ESP_OK;
}

// ============================================================================
// 按钮初始化
// ============================================================================

static void button_init(void) {
    ESP_LOGI(TAG, "初始化按钮 (GPIO%d, 低电平有效)...", BUTTON_GPIO);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);
    ESP_LOGI(TAG, "按钮初始化完成");
}

// ============================================================================
// 横竖屏切换
// ============================================================================

static void toggle_orientation(void) {
    s_is_landscape = !s_is_landscape;

    lv_display_rotation_t rot = s_is_landscape ? LV_DISPLAY_ROTATION_90
                                               : LV_DISPLAY_ROTATION_0;

    if (lvgl_port_lock(0)) {
        lv_display_set_rotation(s_disp, rot);
        lv_obj_clean(lv_screen_active());
        lv_demo_widgets();
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "切换到 %s (%dx%d)",
             s_is_landscape ? "横屏" : "竖屏",
             s_is_landscape ? LCD_H_RES : LCD_V_RES,
             s_is_landscape ? LCD_V_RES : LCD_H_RES);
}

// ============================================================================
// LVGL 初始化
// ============================================================================

static esp_err_t lvgl_init(void) {
    ESP_LOGI(TAG, "初始化 LVGL...");

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL Port 初始化失败");

    // SPI LCD 直接使用 panel_handle (非 MIPI DSI)
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_panel_io,
        .panel_handle = s_panel,
        .control_handle = NULL,
        .buffer_size = LCD_H_RES * LCD_V_RES / 2,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_spiram = true,
            .sw_rotate = true,
        },
    };

    s_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "LVGL 显示注册失败");

    // 设置初始方向
    if (s_is_landscape) {
        lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_90);
        ESP_LOGI(TAG, "初始方向: 横屏 %dx%d", LCD_H_RES, LCD_V_RES);
    } else {
        ESP_LOGI(TAG, "初始方向: 竖屏 %dx%d", LCD_V_RES, LCD_H_RES);
    }

    // 设置默认显示
    lv_display_set_default(s_disp);

    ESP_LOGI(TAG, "LVGL 初始化完成 (RGB565, PSRAM, sw_rotate)");
    return ESP_OK;
}

// ============================================================================
// 主函数
// ============================================================================

void app_main(void) {
    ESP_LOGI(TAG, "ESP32-S3 立创实战派 ST7789 LVGL 演示程序");
    ESP_LOGI(TAG, "物理分辨率: %dx%d, 默认: %s, GPIO%d 切换横竖屏",
             LCD_H_RES, LCD_V_RES,
             s_is_landscape ? "横屏" : "竖屏", BUTTON_GPIO);

    // LCD 初始化 (I2C + PCA9557 + SPI + ST7789)
    ESP_ERROR_CHECK(lcd_init());
    backlight_set(100);

    // LVGL 初始化
    ESP_ERROR_CHECK(lvgl_init());

    // 触摸屏初始化
    if (touch_init() == ESP_OK) {
        if (lvgl_port_lock(0)) {
            touch_register_lvgl();
            lvgl_port_unlock();
        }
        ESP_LOGI(TAG, "触摸屏已启用");
    } else {
        ESP_LOGW(TAG, "触摸屏初始化失败 (未检测到 FT6336)");
    }

    // 按钮初始化
    button_init();

    // 启动 LVGL Demo
    ESP_LOGI(TAG, "启动 LVGL Demo...");
    if (lvgl_port_lock(0)) {
        lv_demo_widgets();
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "启动完成");

    // 主循环 (按钮检测)
    bool btn_last = true;
    while (1) {
        bool btn_now = gpio_get_level(BUTTON_GPIO);
        if (btn_last == true && btn_now == false) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(BUTTON_GPIO) == 0) {
                ESP_LOGI(TAG, "按钮按下! 切换显示方向...");
                toggle_orientation();
                while (gpio_get_level(BUTTON_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        }
        btn_last = btn_now;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
