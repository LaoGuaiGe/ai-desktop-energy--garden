/**
 * @file main.c
 * @brief 🚀 ESP32-P4 6.2寸 AXS15260 MIPI DSI LCD LVGL 演示程序
 * @note 物理分辨率: 452x1280, 2 Lane MIPI DSI, RGB888 24位色
 * @note 支持竖屏/横屏运行时切换 (GPIO47 按钮)
 * @note 支持 AXS15260 触摸屏 (I2C 地址: 0x3B)
 *
 * SPDX-FileCopyrightText: 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// 🖥️ LCD 和触摸屏驱动
#include "esp_lcd_axs15260.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_axs15260.h"

// 🎨 LVGL
#include "demos/lv_demos.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "main";

// ============================================================================
// 🔧 硬件配置
// ============================================================================

// 🔌 LCD GPIO
#define LCD_RST_GPIO GPIO_NUM_24
#define LCD_BL_GPIO GPIO_NUM_29

// 👆 触摸屏 GPIO
#define TOUCH_I2C_SDA GPIO_NUM_26
#define TOUCH_I2C_SCL GPIO_NUM_27
#define TOUCH_RST_GPIO GPIO_NUM_28
#define TOUCH_INT_GPIO GPIO_NUM_25
#define TOUCH_I2C_PORT I2C_NUM_0

// 🔘 按钮 GPIO (低电平使能)
#define BUTTON_GPIO GPIO_NUM_47

// ⚡ MIPI DSI PHY 电源
#define MIPI_DSI_PHY_PWR_LDO_CHAN 3
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

// 💡 背光 PWM
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES LEDC_TIMER_13_BIT
#define BL_LEDC_FREQ 5000
#define BL_LEDC_DUTY_MAX 8191

// ============================================================================
// 🔄 显示方向配置
// ============================================================================
// 初始显示方向 (运行时可通过 GPIO47 按钮切换)
// 0 = 竖屏 (Portrait)  452x1280
// 1 = 横屏 (Landscape) 1280x452
#define DEFAULT_ORIENTATION 1

// ============================================================================
// 📦 全局变量
// ============================================================================

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_mipi_io = NULL;
static esp_lcd_dsi_bus_handle_t s_dsi_bus = NULL;
static axs15260_touch_handle_t s_touch = NULL;
static lv_indev_t *s_indev = NULL;
static lv_display_t *s_disp = NULL;
static bool s_is_landscape = (DEFAULT_ORIENTATION == 1);

// ============================================================================
// 💡 背光控制
// ============================================================================

static esp_err_t backlight_init(void) {
  ESP_LOGI(TAG, "💡 初始化背光...");

  ledc_timer_config_t timer = {.speed_mode = BL_LEDC_MODE,
                               .timer_num = BL_LEDC_TIMER,
                               .duty_resolution = BL_LEDC_DUTY_RES,
                               .freq_hz = BL_LEDC_FREQ,
                               .clk_cfg = LEDC_AUTO_CLK};
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "❌ LEDC 定时器失败");

  ledc_channel_config_t channel = {
      .speed_mode = BL_LEDC_MODE,
      .channel = BL_LEDC_CHANNEL,
      .timer_sel = BL_LEDC_TIMER,
      .gpio_num = LCD_BL_GPIO,
      .duty = 0,
  };
  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "❌ LEDC 通道失败");

  return ESP_OK;
}

static esp_err_t backlight_set(uint8_t percent) {
  uint32_t duty = (BL_LEDC_DUTY_MAX * percent) / 100;
  ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
  ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
  ESP_LOGI(TAG, "💡 背光: %d%%", percent);
  return ESP_OK;
}

// ============================================================================
// 🖥️ LCD 初始化
// ============================================================================

static esp_err_t lcd_init(void) {
  // ⚡ MIPI DSI PHY 电源
  ESP_LOGI(TAG, "⚡ 启用 MIPI DSI PHY 电源...");
  esp_ldo_channel_handle_t ldo = NULL;
  esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
      .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
  };
  ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG,
                      "❌ LDO 失败");

  // 💡 背光初始化
  ESP_RETURN_ON_ERROR(backlight_init(), TAG, "❌ 背光失败");
  backlight_set(0);

  // 🔄 LCD 复位
  ESP_LOGI(TAG, "🔄 LCD 复位...");
  gpio_config_t rst_cfg = {
      .pin_bit_mask = (1ULL << LCD_RST_GPIO),
      .mode = GPIO_MODE_OUTPUT,
  };
  gpio_config(&rst_cfg);
  gpio_set_level(LCD_RST_GPIO, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(LCD_RST_GPIO, 1);
  vTaskDelay(pdMS_TO_TICKS(170));

  // 📡 MIPI DSI 总线
  ESP_LOGI(TAG, "📡 创建 MIPI DSI 总线...");
  esp_lcd_dsi_bus_config_t bus_cfg = AXS15260_PANEL_BUS_DSI_2CH_CONFIG();
  ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus), TAG,
                      "❌ DSI 总线失败");

  // 📡 MIPI DBI IO
  ESP_LOGI(TAG, "📡 创建 MIPI DBI IO...");
  esp_lcd_dbi_io_config_t dbi_cfg = AXS15260_PANEL_IO_DBI_CONFIG();
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_mipi_io),
                      TAG, "❌ DBI IO 失败");

  // 🖥️ AXS15260 面板
  ESP_LOGI(TAG, "🖥️ 创建 AXS15260 面板...");
  esp_lcd_dpi_panel_config_t dpi_cfg =
      AXS15260_452_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB888);
  dpi_cfg.num_fbs = 2;
  dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB888;
  dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB888;

  axs15260_vendor_config_t vendor_cfg = {
      .mipi_config =
          {
              .dsi_bus = s_dsi_bus,
              .dpi_config = &dpi_cfg,
              .lane_num = AXS15260_MIPI_LANES,
          },
      .flags.use_mipi_interface = 1,
  };

  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = LCD_RST_GPIO,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 24,
      .vendor_config = &vendor_cfg,
  };

  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_axs15260(s_mipi_io, &panel_cfg, &s_panel), TAG,
      "❌ 面板创建失败");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "❌ 面板初始化失败");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG,
                      "❌ 开启显示失败");

  ESP_LOGI(TAG, "✅ LCD 初始化完成 (%dx%d)", AXS15260_LCD_H_RES,
           AXS15260_LCD_V_RES);
  return ESP_OK;
}

// ============================================================================
// 👆 触摸屏
// ============================================================================

// 📍 保存上一次触摸坐标 (用于滑动)
static int16_t s_last_x = 0;
static int16_t s_last_y = 0;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  // 📖 读取触摸数据
  axs15260_touch_data_t touch;
  esp_err_t ret = axs15260_touch_read(s_touch, &touch);

  if (ret == ESP_OK && touch.point_num > 0) {
    // 👆 有触摸点 - 驱动返回物理像素坐标 (竖屏: X=0~451, Y=0~1279)
    uint16_t raw_x = touch.points[0].x;
    uint16_t raw_y = touch.points[0].y;

    // ⚠️ 越界过滤 (噪声/误触)
    if (raw_x >= AXS15260_LCD_H_RES || raw_y >= AXS15260_LCD_V_RES) {
      data->point.x = s_last_x;
      data->point.y = s_last_y;
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }

    // 📍 直接传入物理坐标, LVGL 内部会根据 display rotation 自动变换
    s_last_x = raw_x;
    s_last_y = raw_y;
    data->point.x = s_last_x;
    data->point.y = s_last_y;

    // 📋 根据事件类型判断状态
    uint8_t event = touch.points[0].event;
    if (event == 1) {
      data->state = LV_INDEV_STATE_RELEASED;
    } else {
      data->state = LV_INDEV_STATE_PRESSED;
    }
  } else {
    data->point.x = s_last_x;
    data->point.y = s_last_y;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static esp_err_t touch_init(void) {
  ESP_LOGI(TAG, "👆 初始化触摸屏...");

  axs15260_touch_config_t cfg = {
      .i2c_sda = TOUCH_I2C_SDA,
      .i2c_scl = TOUCH_I2C_SCL,
      .rst_gpio = TOUCH_RST_GPIO,
      .int_gpio = TOUCH_INT_GPIO,
      .i2c_port = TOUCH_I2C_PORT,
  };

  esp_err_t ret = axs15260_touch_new(&cfg, &s_touch);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ 触摸屏初始化失败");
    return ret;
  }

  uint16_t ver = 0;
  if (axs15260_touch_get_version(s_touch, &ver) == ESP_OK) {
    ESP_LOGI(TAG, "👆 固件版本: 0x%04X", ver);
  }

  ESP_LOGI(TAG, "✅ 触摸屏初始化完成");
  return ESP_OK;
}

static esp_err_t touch_register_lvgl(void) {
  s_indev = lv_indev_create();
  if (!s_indev) {
    ESP_LOGE(TAG, "❌ 创建输入设备失败");
    return ESP_FAIL;
  }
  lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(s_indev, touch_read_cb);
  ESP_LOGI(TAG, "✅ 触摸屏已注册到 LVGL");
  return ESP_OK;
}

// ============================================================================
// 🔘 按钮初始化
// ============================================================================

static void button_init(void) {
  ESP_LOGI(TAG, "🔘 初始化按钮 (GPIO%d, 低电平使能)...", BUTTON_GPIO);
  gpio_config_t btn_cfg = {
      .pin_bit_mask = (1ULL << BUTTON_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&btn_cfg);
  ESP_LOGI(TAG, "✅ 按钮初始化完成");
}

// ============================================================================
// 🔄 横竖屏切换
// ============================================================================

static void toggle_orientation(void) {
  s_is_landscape = !s_is_landscape;

  lv_display_rotation_t rot =
      s_is_landscape ? LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_0;

  if (lvgl_port_lock(0)) {
    // 🔄 切换旋转方向
    lv_display_set_rotation(s_disp, rot);

    // 🗑️ 清除当前 UI 并重建 Demo
    lv_obj_clean(lv_screen_active());
    lv_demo_widgets();

    lvgl_port_unlock();
  }

  ESP_LOGI(TAG, "🔄 切换到 %s (%dx%d)", s_is_landscape ? "横屏" : "竖屏",
           s_is_landscape ? AXS15260_LCD_V_RES : AXS15260_LCD_H_RES,
           s_is_landscape ? AXS15260_LCD_H_RES : AXS15260_LCD_V_RES);
}

// ============================================================================
// 🎨 LVGL 初始化
// ============================================================================

static esp_err_t lvgl_init(void) {
  ESP_LOGI(TAG, "🎨 初始化 LVGL...");

  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "❌ LVGL Port 失败");

  esp_lcd_panel_handle_t dpi_panel = esp_lcd_axs15260_get_dpi_panel(s_panel);
  ESP_RETURN_ON_FALSE(dpi_panel, ESP_FAIL, TAG, "❌ 获取 DPI 面板失败");

  // ⚙️ 使用 SPIRAM + sw_rotate 配置 (支持运行时横竖屏切换)
  // ⚠️ 不能用 avoid_tearing + DPI帧缓冲 (旋转时行宽不匹配会越界)
  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = s_mipi_io,
      .panel_handle = dpi_panel,
      .control_handle = s_panel,
      .buffer_size = AXS15260_LCD_H_RES * AXS15260_LCD_V_RES / 4,
      .double_buffer = true,
      .hres = AXS15260_LCD_H_RES,
      .vres = AXS15260_LCD_V_RES,
      .color_format = LV_COLOR_FORMAT_RGB888,
      .flags =
          {
              .buff_spiram = true,
              .sw_rotate = true,
          },
  };

  const lvgl_port_display_dsi_cfg_t dsi_cfg = {
      .flags.avoid_tearing = false,
  };

  s_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
  ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "❌ LVGL 显示注册失败");

  // 🔄 设置初始方向
  if (s_is_landscape) {
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_90);
    ESP_LOGI(TAG, "🖥️ 初始: 横屏 %dx%d", AXS15260_LCD_V_RES,
             AXS15260_LCD_H_RES);
  } else {
    ESP_LOGI(TAG, "📱 初始: 竖屏 %dx%d", AXS15260_LCD_H_RES,
             AXS15260_LCD_V_RES);
  }

  ESP_LOGI(TAG, "✅ LVGL 初始化完成 (sw_rotate + SPIRAM, 支持运行时切换)");
  return ESP_OK;
}

// ============================================================================
// 🚀 主函数
// ============================================================================

void app_main(void) {
  ESP_LOGI(TAG, "🚀 ESP32-P4 AXS15260 LVGL 演示程序");
  ESP_LOGI(TAG, "📋 物理分辨率: %dx%d, 默认: %s, GPIO%d 按钮切换横竖屏",
           AXS15260_LCD_H_RES, AXS15260_LCD_V_RES,
           s_is_landscape ? "横屏" : "竖屏", BUTTON_GPIO);

  // 🖥️ LCD
  ESP_ERROR_CHECK(lcd_init());
  backlight_set(100);

  // 🎨 LVGL
  ESP_ERROR_CHECK(lvgl_init());

  // 👆 触摸屏
  if (touch_init() == ESP_OK) {
    if (lvgl_port_lock(0)) {
      touch_register_lvgl();
      lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "✅ 触摸屏已启用");
  } else {
    ESP_LOGW(TAG, "⚠️ 触摸屏初始化失败");
  }

  // 🔘 按钮
  button_init();

  // 🎨 LVGL Demo
  ESP_LOGI(TAG, "🎨 启动 LVGL Demo...");
  if (lvgl_port_lock(0)) {
    lv_demo_widgets();
    lvgl_port_unlock();
  }
  ESP_LOGI(TAG, "✅ 启动完成");

  // 📊 主循环 (含按钮检测)
  bool btn_last = true; // 上一次按钮状态 (上拉, 默认高)
  while (1) {
    // 🔘 按钮检测 (低电平使能, 带消抖)
    bool btn_now = gpio_get_level(BUTTON_GPIO);
    if (btn_last == true && btn_now == false) {
      // ⬇️ 检测到下降沿 (按下)
      vTaskDelay(pdMS_TO_TICKS(50)); // 消抖
      if (gpio_get_level(BUTTON_GPIO) == 0) {
        ESP_LOGI(TAG, "🔘 按钮按下! 切换显示方向...");
        toggle_orientation();
        // 等待按钮松开
        while (gpio_get_level(BUTTON_GPIO) == 0) {
          vTaskDelay(pdMS_TO_TICKS(50));
        }
      }
    }
    btn_last = btn_now;

    vTaskDelay(pdMS_TO_TICKS(20)); // 20ms 轮询周期
  }
}
