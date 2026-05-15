# Garden UI Sim — PC LVGL 仿真器设计文档

**日期：** 2026-05-15
**版本：** v1.0
**目标：** 在 PC 上用 LVGL 9 + SDL2 仿真器制作 AI 桌面能量花园 UI demo，后续移植回 ESP32-P4 工程。

---

## 1. 背景与目标

硬件目标为 ESP32-P4 + AXS15260 MIPI 屏（物理分辨率 452×1280，横屏使用为 1280×452，支持电容触摸）。当前硬件不在身边，需要先在 PC 上验证 UI 布局、动画逻辑和交互流程。

**核心约束：**
- 不破坏现有 `lvgl9.5-demo` ESP32-P4 工程
- UI 代码必须可移植：`garden_ui.c/.h` 零平台依赖，只调用 LVGL API
- LVGL 版本与 ESP32 工程一致：v9.5.0
- demo 阶段只实现页面 1（花园主界面）

---

## 2. 工程结构

```
ai-desktop-energy--garden/
├── lvgl9.5-demo/          ← 现有 ESP32-P4 工程，不修改
└── garden-ui-sim/         ← 新增 PC 仿真器工程
    ├── CMakeLists.txt
    ├── lv_conf.h           ← LVGL 配置（可与 ESP32 工程共用，#ifdef 区分平台）
    ├── main.c              ← PC 专属：SDL2+LVGL 初始化、键盘/鼠标映射、主循环
    ├── garden_ui.c         ← 可移植 UI 层：控件、动画、状态机
    └── garden_ui.h         ← 公开 API
```

**移植路径：** ESP32-P4 工程只需复制 `garden_ui.c`、`garden_ui.h`、`lv_conf.h`，在 `app_main.c` 中调用相同 API 即可。

---

## 3. 可移植 API（garden_ui.h）

```c
// 初始化：创建所有控件，启动动画定时器
void garden_ui_init(void);

// 编码器输入：+1 右旋，-1 左旋
void garden_ui_encoder_event(int delta);

// 按钮输入：0=短按, 1=长按(>500ms), 2=双击
void garden_ui_button_event(uint8_t type);

// 触摸/鼠标输入：坐标 + 按下状态
void garden_ui_touch_event(int16_t x, int16_t y, bool pressed);

// 主循环每帧调用，推进状态机和动画（elapsed_ms 为距上次调用的毫秒数）
void garden_ui_tick(uint32_t elapsed_ms);
```

**设计原则：**
- `garden_ui.c` 内部只调用 `lv_*` API，零 SDL2 依赖，零 ESP-IDF 依赖
- 所有平台胶水代码（SDL2 事件循环、LVGL flush callback）全部在 `main.c`

---

## 4. PC 输入映射（main.c）

| SDL2 事件 | 调用 |
|-----------|------|
| 鼠标左键按下/移动/释放 | `garden_ui_touch_event(x, y, pressed)` |
| ← 方向键 | `garden_ui_encoder_event(-1)` |
| → 方向键 | `garden_ui_encoder_event(+1)` |
| Enter 短按（<500ms） | `garden_ui_button_event(0)` |
| Enter 长按（≥500ms） | `garden_ui_button_event(1)` |
| Enter 双击（<300ms 内两次） | `garden_ui_button_event(2)` |

LVGL 9 通过 `lv_indev` 注册两个独立输入设备：编码器 indev（group 导航）和 pointer indev（触摸/鼠标直接命中控件），两者互不干扰。

---

## 5. 花园主界面布局（1280×452）

三区布局，从左到右：

```
┌──────────────────────────────────────────────────────────────────────────┐
│  状态栏(120px)  │         花园场景(900px)          │   信息栏(260px)      │
│                │                                  │                     │
│  26 C          │   [像素天空 + 云朵横移]            │  Energy 45/100      │
│  DEV: 2        │   [像素植物摇摆动画]               │  Lv.3 PLANT         │
│  ONLINE        │   [能量粒子上浮]                   │  Streak: 7d         │
│  HUB ***       │   [像素地面]                      │  [WATER]            │
└──────────────────────────────────────────────────────────────────────────┘
```

> **注：** demo 阶段所有标签使用 ASCII 英文（LVGL 内置 Montserrat 字体不支持中文）。ESP32-P4 移植时替换为 `lv_font_conv` 生成的中文子集字体，标签文本改回中文，`garden_ui.c` 其余代码不变。

**视觉风格：** 明亮像素风（Stardew Valley 感）
- 天空：蓝色渐变背景（`lv_draw_rect` 渐变）
- 地面：棕色像素块（`lv_draw_rect`）
- 植物：`lv_canvas` 手绘像素图案，4 帧循环摇摆
- 云朵：白色像素矩形，缓慢横向移动
- 粒子：小方块从植物根部上浮消散

---

## 6. 内部状态结构

```c
typedef struct {
    int16_t  x, y;      // 当前位置（相对花园区域左上角）
    int16_t  vy;        // 垂直速度（负值 = 向上，单位 px/tick）
    uint8_t  alpha;     // 透明度 0~255，随高度衰减
    bool     active;    // 是否活跃
} particle_t;

typedef struct {
    uint8_t  plant_stage;       // 进化阶段 0~5，demo 固定为 3（成株）
    uint16_t energy_x10;        // 能量值 ×10，demo 初始 450（= 45.0）
    uint8_t  streak_days;       // 连续天数，demo 固定 7
    uint8_t  plant_frame;       // 当前像素帧 0~3（植物摇摆动画）
    uint32_t frame_timer;       // 帧计时器（ms）
    uint8_t  particle_count;    // 活跃粒子数，最多 8 个
    particle_t particles[8];    // 粒子位置/速度
} garden_state_t;
```

---

## 7. 动画规格

| 动画 | 实现方式 | 节奏 |
|------|----------|------|
| 植物像素摇摆 | `lv_canvas` 4 帧像素图案循环 | 每 400ms 换帧 |
| 能量粒子上浮 | 8 个小方块从植物根部向上漂移，到顶消失再生 | 每帧更新位置 |
| 云朵横移 | 2 个白色像素块向右移动，超出边界回绕 | 每帧 +0.5px |
| 能量条填充 | `lv_bar` 动画到目标值 | 触发浇水时 300ms |
| 浇水粒子爆发 | 粒子数临时 ×2，持续 1 秒后恢复 | 点击浇水触发 |

---

## 8. 交互响应（demo 范围）

| 操作 | 效果 |
|------|------|
| 点击/短按"浇水"按钮 | 能量 +10，粒子爆发效果 |
| 鼠标点击植物区域 | 同浇水效果 |
| 编码器右旋 | 预留页面切换（demo 阶段打印 log，无实际效果） |
| 编码器左旋 | 同上 |

---

## 9. 构建系统

### CMakeLists.txt 核心

```cmake
cmake_minimum_required(VERSION 3.16)
project(garden-ui-sim C)

include(FetchContent)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
)
FetchContent_MakeAvailable(lvgl)

find_package(SDL2 REQUIRED)

add_executable(garden-sim
    main.c
    garden_ui.c
)

target_link_libraries(garden-sim PRIVATE lvgl SDL2::SDL2)
target_include_directories(garden-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```

### MSYS2 依赖安装（一次性）

```bash
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2
```

### 构建与运行

```bash
cd garden-ui-sim
cmake -B build -G "MinGW Makefiles"
cmake --build build
./build/garden-sim.exe
```

运行后弹出 **1280×452 原生 Windows 窗口**，鼠标 = 触摸，键盘方向键 = 编码器，Enter = 确认键。

---

## 10. 与 ESP32-P4 工程的差异

| 项目 | PC 仿真器 | ESP32-P4 |
|------|-----------|----------|
| 帧率 | ~60fps（SDL2 vsync） | 目标 30-45fps（PPA 加速） |
| 输入 | 鼠标 + 键盘 | AXS15260 触摸 + 编码器 + 按钮 |
| 字体 | lv_font_montserrat（内置） | lv_font_conv 生成中文子集 |
| 帧缓冲 | 系统内存 | PSRAM（双缓冲 2.2MB） |
| `garden_ui.c` | 完全相同 | 完全相同 |

---

## 11. 不在 demo 范围内

- 页面 2（番茄钟）、页面 3（AI 对话）、页面 4（图鉴）
- 昼夜循环动效
- 天气 API 联动
- 中文字体（demo 使用 LVGL 内置英文字体）
- 音效
