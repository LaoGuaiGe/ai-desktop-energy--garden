# Garden UI — ESP32-P4 移植指南

从 PC 仿真器 (`garden-ui-sim`) 移植到 ESP32-P4 + AXS15260 MIPI 屏的完整指南。

## 移植概览

| 项目 | PC 仿真器 | ESP32-P4 目标 |
|------|-----------|---------------|
| 渲染方式 | fill_rect 直写 RGB565 缓冲 | LVGL canvas + layer API（PPA 加速） |
| 刷新策略 | 全帧 memcpy + invalidate | LVGL 脏矩形，只刷变化区域 |
| 帧缓冲 | 系统内存 ~1.6MB | PSRAM 双缓冲 2.2MB |
| 输入 | SDL2 鼠标/键盘 | AXS15260 触摸 + 编码器 + 按钮 |
| 字体 | lv_font_montserrat（内置英文） | lv_font_conv 中文子集 |
| 色深 | RGB565 (16bit) | RGB888 (24bit, MIPI 原生) |
| 帧率 | ~60fps | 目标 30-45fps |

## 需要复制的文件

```
garden-ui-sim/garden_ui.h  →  lvgl9.5-demo/main/garden_ui.h
garden-ui-sim/garden_ui.c  →  lvgl9.5-demo/main/garden_ui.c（需改造）
```

## 改造步骤

### 1. 替换渲染方式

PC 版用 `fill_rect()` 直接写像素缓冲区，ESP32-P4 上改用 LVGL layer API + PPA 加速。

**PC 版（当前）：**
```c
static void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                      uint32_t color, uint8_t alpha) {
    // 直接写 RGB565 像素到 s_scene_buf
}
```

**ESP32-P4 版（目标）：**
```c
static lv_layer_t s_layer;

static void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                      uint32_t color, uint8_t alpha) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa   = alpha;
    dsc.radius   = 0;
    lv_area_t a  = { x, y, (int16_t)(x + w - 1), (int16_t)(y + h - 1) };
    lv_draw_rect(&s_layer, &dsc, &a);
}
```

### 2. 替换 draw_scene 结构

**PC 版：** memcpy 背景 → fill_rect 动态元素 → invalidate

**ESP32-P4 版：**
```c
static void draw_scene(void) {
    lv_canvas_init_layer(s_canvas_scene, &s_layer);

    // 静态背景用 lv_img 控件（只创建一次，不每帧重绘）
    // 动态元素用 fill_rect（走 PPA 加速）

    lv_canvas_finish_layer(s_canvas_scene, &s_layer);
}
```

### 3. 静态背景优化

PC 版的 `draw_background()` + memcpy 方案在 ESP32-P4 上不适用。替代方案：

**方案 A（推荐）：LVGL image 控件**
- 将静态背景预渲染为 PNG/bin 图片资源，存 SPI Flash
- 用 `lv_img_create()` 作为最底层控件
- 动态元素用 canvas 叠加在上面
- 优势：零 CPU 开销，PPA 直接 DMA 搬运

**方案 B：首帧绘制 + snapshot**
- 首帧用 layer API 绘制所有静态元素
- `lv_snapshot_take()` 保存为 image
- 后续帧用 snapshot 作为背景
- 优势：不需要预制图片资源

### 4. 色深适配

PC 仿真器用 RGB565，ESP32-P4 MIPI 屏用 RGB888。

改动点：
- `lv_conf.h` 中 `LV_COLOR_DEPTH` 改为 `24` 或 `32`
- `lv_canvas_set_buffer` 格式改为 `LV_COLOR_FORMAT_RGB888`
- 帧缓冲大小：1280 × 452 × 3 = 1.7MB（单缓冲）

`fill_rect` 内部不需要改——`lv_color_hex()` 和 `lv_draw_rect` 自动适配色深。

### 5. 输入设备注册

```c
// 触摸（AXS15260 I2C 回调）
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    axs15260_touch_data_t touch;
    if (axs15260_touch_read(s_touch, &touch) == ESP_OK && touch.point_num > 0) {
        data->point.x = touch.points[0].x;
        data->point.y = touch.points[0].y;
        data->state   = LV_INDEV_STATE_PRESSED;
        garden_ui_touch_event(data->point.x, data->point.y, true);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// 编码器（GPIO 中断）
static void IRAM_ATTR encoder_isr(void *arg) {
    int delta = /* 读取 A/B 相判断方向 */ ;
    garden_ui_encoder_event(delta);
}

// 按钮（GPIO + 消抖）
static void button_task(void *arg) {
    // 检测短按/长按/双击，调用 garden_ui_button_event(type)
}
```

### 6. 主循环集成

```c
#include "garden_ui.h"

void app_main(void) {
    // LCD + LVGL 初始化（已有代码）
    ESP_ERROR_CHECK(lcd_init());
    ESP_ERROR_CHECK(lvgl_init());
    touch_init();
    encoder_init();
    button_init();

    // 注册输入设备
    lv_indev_t *touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, touch_read_cb);

    // 初始化花园 UI
    if (lvgl_port_lock(0)) {
        garden_ui_init();
        lvgl_port_unlock();
    }

    // 主循环
    uint32_t last = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t elapsed = now - last;
        last = now;

        if (lvgl_port_lock(0)) {
            garden_ui_tick(elapsed);
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(20));  // 50fps tick
    }
}
```

### 7. lv_conf.h 差异

| 配置项 | PC 仿真器 | ESP32-P4 |
|--------|-----------|----------|
| `LV_COLOR_DEPTH` | 16 | 24 |
| `LV_MEM_SIZE` | 512KB | 256KB（内部 SRAM） |
| `LV_TICK_CUSTOM` | SDL_GetTicks | esp_timer / xTaskGetTickCount |
| `LV_USE_DRAW_PPA` | 0 | 1（ESP32-P4 PPA 加速） |
| `LV_FONT_DEFAULT` | montserrat_14 | 自定义中文子集字体 |

### 8. 中文字体

```bash
lv_font_conv --font SourceHanSans-Regular.ttf \
  --size 16 --bpp 4 \
  --range 0x20-0x7F \
  --symbols "温度设备在线能量浇水连续天花园" \
  -o lv_font_chinese_16.c
```

将 `garden_ui.c` 中的标签文本从英文改回中文：
```c
lv_label_set_text(l_temp, "26°C");      // was "26 C"
lv_label_set_text(l_dev, "设备: 2");     // was "DEV: 2"
lv_label_set_text(l_net, "在线");        // was "ONLINE"
```

### 9. 可直接复用的代码（无需改动）

- `garden_ui.h` — 公开 API 完全不变
- `garden_state_t` 结构体 — 状态机逻辑不变
- `garden_ui_init()` — 布局创建逻辑（LVGL 控件代码通用）
- `garden_ui_encoder_event()` — 编码器处理
- `garden_ui_button_event()` — 按钮处理 + 能量系统
- `garden_ui_touch_event()` — 触摸处理
- `garden_ui_tick()` — 动画时序、状态更新
- `garden_ui_set_stage()` — 阶段切换
- `build_layout()` — 三区布局 + LVGL 控件（完全通用）
- `spawn_particles()` — 粒子生成逻辑

### 10. 需要改造的代码

| 函数 | 改动内容 |
|------|----------|
| `fill_rect()` | 从直写像素改为 `lv_draw_rect` layer API |
| `draw_scene()` | 去掉 memcpy，改用 `lv_canvas_init_layer` / `finish_layer` |
| `draw_background()` | 替换为预制 PNG 背景或 snapshot |
| `rgb888_to_565()` | 删除（不再需要色彩转换） |

### 11. 预估工作量

| 任务 | 时间 |
|------|------|
| 复制文件 + 调整 CMakeLists | 10 分钟 |
| fill_rect 替换为 layer API | 30 分钟 |
| 静态背景方案实现 | 1-2 小时 |
| 触摸/编码器/按钮接入 | 30 分钟 |
| 中文字体生成 + 替换文本 | 30 分钟 |
| 调试 + 帧率优化 | 1-2 小时 |
| **合计** | **约半天** |
