# Garden UI Sim Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a PC LVGL 9 + SDL2 simulator project at `garden-ui-sim/` that renders the garden main screen (1280×452, bright pixel art style) with mouse/keyboard input, without touching the existing `lvgl9.5-demo` ESP32-P4 project.

**Architecture:** `main.c` owns all SDL2/LVGL platform init and input mapping. `garden_ui.c` owns all UI widgets, animations, and state — zero SDL2 dependency, portable to ESP32-P4 by copy. LVGL v9.5.0 fetched via CMake FetchContent; SDL2 installed via MSYS2.

**Tech Stack:** C17, CMake 3.16+, LVGL 9.5.0, SDL2, MSYS2 MinGW-w64

---

## File Map

| File | Role |
|------|------|
| `garden-ui-sim/CMakeLists.txt` | Build system: FetchContent LVGL, find SDL2, compile targets |
| `garden-ui-sim/lv_conf.h` | LVGL config: color depth, display size, enabled features |
| `garden-ui-sim/main.c` | SDL2 init, LVGL display/indev registration, event loop, input mapping |
| `garden-ui-sim/garden_ui.h` | Public API: init, encoder_event, button_event, touch_event, tick |
| `garden-ui-sim/garden_ui.c` | All LVGL widgets, pixel art drawing, animation state machine |

---

## Task 1: CMakeLists.txt + MSYS2 environment

**Files:**
- Create: `garden-ui-sim/CMakeLists.txt`

- [ ] **Step 1: Install MSYS2 MinGW64 dependencies (one-time)**

In MSYS2 MinGW64 shell:
```bash
pacman -S --needed mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2
cmake --version   # expect 3.16+
sdl2-config --version  # expect 2.x
```

- [ ] **Step 2: Create `garden-ui-sim/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(garden-ui-sim C)
set(CMAKE_C_STANDARD 11)

include(FetchContent)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lvgl)

find_package(SDL2 REQUIRED)

add_executable(garden-sim main.c garden_ui.c)
target_compile_definitions(garden-sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(garden-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(garden-sim PRIVATE lvgl SDL2::SDL2 SDL2::SDL2main)
```

- [ ] **Step 3: Commit**

```bash
git add garden-ui-sim/CMakeLists.txt
git commit -m "build: add garden-ui-sim CMakeLists with LVGL 9.5 + SDL2"
```

---

## Task 2: lv_conf.h

**Files:**
- Create: `garden-ui-sim/lv_conf.h`

- [ ] **Step 1: Create `garden-ui-sim/lv_conf.h`**

```c
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Display resolution */
#define LV_HOR_RES_MAX 1280
#define LV_VER_RES_MAX 452

/* Memory */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (256 * 1024U)

/* HAL */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <SDL2/SDL.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (SDL_GetTicks())

/* Font */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Features needed for demo */
#define LV_USE_CANVAS 1
#define LV_USE_BAR    1
#define LV_USE_LABEL  1
#define LV_USE_BTN    1
#define LV_USE_OBJ    1

/* Animation */
#define LV_USE_ANIMATION 1

/* Logging */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#endif /* LV_CONF_H */
```

- [ ] **Step 2: Commit**

```bash
git add garden-ui-sim/lv_conf.h
git commit -m "config: add lv_conf.h for PC simulator (SDL2 tick, RGB565)"
```

---

## Task 3: garden_ui.h — public API header

**Files:**
- Create: `garden-ui-sim/garden_ui.h`

- [ ] **Step 1: Create `garden-ui-sim/garden_ui.h`**

```c
#ifndef GARDEN_UI_H
#define GARDEN_UI_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Call once after lv_init() and display registration */
void garden_ui_init(void);

/* Encoder: +1 = right turn, -1 = left turn */
void garden_ui_encoder_event(int delta);

/* Button: 0=short press, 1=long press(>=500ms), 2=double click */
void garden_ui_button_event(uint8_t type);

/* Touch/mouse: screen coordinates + pressed state */
void garden_ui_touch_event(int16_t x, int16_t y, bool pressed);

/* Call every frame from main loop; elapsed_ms = ms since last call */
void garden_ui_tick(uint32_t elapsed_ms);

#endif /* GARDEN_UI_H */
```

- [ ] **Step 2: Commit**

```bash
git add garden-ui-sim/garden_ui.h
git commit -m "feat: add garden_ui.h portable API header"
```

---

## Task 4: garden_ui.c — state structs + skeleton

**Files:**
- Create: `garden-ui-sim/garden_ui.c`

- [ ] **Step 1: Create `garden-ui-sim/garden_ui.c` with state types and stub implementations**

```c
#include "garden_ui.h"
#include <stdio.h>
#include <string.h>

/* ── Particle ── */
typedef struct {
    int16_t  x, y;
    int16_t  vy;       /* negative = upward, px per tick */
    uint8_t  alpha;    /* 0-255, fades as it rises */
    bool     active;
} particle_t;

/* ── Garden state ── */
typedef struct {
    uint8_t    plant_stage;    /* 0-5, fixed 3 (mature plant) in demo */
    uint16_t   energy_x10;    /* energy * 10, initial 450 = 45.0 */
    uint8_t    streak_days;   /* fixed 7 in demo */
    uint8_t    plant_frame;   /* 0-3 pixel animation frame */
    uint32_t   frame_timer;   /* ms accumulator for plant animation */
    uint32_t   burst_timer;   /* ms remaining for watering burst */
    particle_t particles[8];
} garden_state_t;

static garden_state_t s_state;

/* ── LVGL object handles ── */
static lv_obj_t *s_screen;
static lv_obj_t *s_canvas_sky;
static lv_obj_t *s_canvas_plant;
static lv_obj_t *s_canvas_particles;
static lv_obj_t *s_bar_energy;
static lv_obj_t *s_label_level;
static lv_obj_t *s_label_streak;
static lv_obj_t *s_label_temp;
static lv_obj_t *s_label_devices;
static lv_obj_t *s_label_status;
static lv_obj_t *s_btn_water;

/* Cloud positions (x offset, wraps at 900) */
static int16_t s_cloud1_x = 80;
static int16_t s_cloud2_x = 400;

/* Canvas pixel buffers (RGB565, static) */
#define SKY_W  900
#define SKY_H  452
#define PLANT_W 120
#define PLANT_H 160
#define PART_W  900
#define PART_H  452

static uint8_t s_sky_buf[LV_CANVAS_BUF_SIZE(SKY_W, SKY_H, 16, LV_DRAW_BUF_STRIDE_ALIGN)];
static uint8_t s_plant_buf[LV_CANVAS_BUF_SIZE(PLANT_W, PLANT_H, 16, LV_DRAW_BUF_STRIDE_ALIGN)];
static uint8_t s_part_buf[LV_CANVAS_BUF_SIZE(PART_W, PART_H, 16, LV_DRAW_BUF_STRIDE_ALIGN)];

/* Forward declarations */
static void build_layout(void);
static void draw_sky(void);
static void draw_plant(void);
static void draw_particles(void);
static void spawn_particles(int count);
static void water_cb(lv_event_t *e);

/* ── Public API ── */

void garden_ui_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.plant_stage  = 3;
    s_state.energy_x10   = 450;
    s_state.streak_days  = 7;
    spawn_particles(4);
    build_layout();
    draw_sky();
    draw_plant();
}

void garden_ui_encoder_event(int delta) {
    /* Page switch reserved — log only in demo */
    printf("[encoder] delta=%d (page switch reserved)\n", delta);
}

void garden_ui_button_event(uint8_t type) {
    if (type == 0) {
        /* Short press = water */
        s_state.energy_x10 += 100; /* +10.0 */
        if (s_state.energy_x10 > 1000) s_state.energy_x10 = 1000;
        lv_bar_set_value(s_bar_energy, s_state.energy_x10 / 10, LV_ANIM_ON);
        s_state.burst_timer = 1000;
        spawn_particles(8);
    }
}

void garden_ui_touch_event(int16_t x, int16_t y, bool pressed) {
    /* LVGL pointer indev handles this — nothing extra needed here */
    (void)x; (void)y; (void)pressed;
}

void garden_ui_tick(uint32_t elapsed_ms) {
    /* Plant animation: cycle 4 frames every 400ms */
    s_state.frame_timer += elapsed_ms;
    if (s_state.frame_timer >= 400) {
        s_state.frame_timer = 0;
        s_state.plant_frame = (s_state.plant_frame + 1) % 4;
        draw_plant();
    }

    /* Burst timer */
    if (s_state.burst_timer > 0) {
        s_state.burst_timer = (s_state.burst_timer > elapsed_ms)
                              ? s_state.burst_timer - elapsed_ms : 0;
    }

    /* Move clouds */
    s_cloud1_x += 1;
    s_cloud2_x += 1;
    if (s_cloud1_x > SKY_W) s_cloud1_x = -40;
    if (s_cloud2_x > SKY_W) s_cloud2_x = -40;

    /* Update particles */
    for (int i = 0; i < 8; i++) {
        if (!s_state.particles[i].active) continue;
        s_state.particles[i].y += s_state.particles[i].vy;
        s_state.particles[i].alpha = (uint8_t)(s_state.particles[i].alpha > 4
                                     ? s_state.particles[i].alpha - 4 : 0);
        if (s_state.particles[i].y < 0 || s_state.particles[i].alpha == 0) {
            /* Respawn at plant base */
            s_state.particles[i].x = (int16_t)(SKY_W / 2 - 20 + (i * 10));
            s_state.particles[i].y = SKY_H - 80;
            s_state.particles[i].vy = (int16_t)(-(2 + (i % 3)));
            s_state.particles[i].alpha = 200;
        }
    }
    draw_sky();
    draw_particles();
}
```

- [ ] **Step 2: Commit**

```bash
git add garden-ui-sim/garden_ui.c
git commit -m "feat: garden_ui.c state structs, API stubs, tick logic"
```

---

## Task 5: garden_ui.c — build_layout() + spawn_particles()

**Files:**
- Modify: `garden-ui-sim/garden_ui.c`

- [ ] **Step 1: Add `spawn_particles()` and `water_cb()` to `garden_ui.c`**

Append after the `garden_ui_tick` function:

```c
static void spawn_particles(int count) {
    int spawned = 0;
    for (int i = 0; i < 8 && spawned < count; i++) {
        s_state.particles[i].x     = (int16_t)(SKY_W / 2 - 20 + (i * 12));
        s_state.particles[i].y     = (int16_t)(SKY_H - 80);
        s_state.particles[i].vy    = (int16_t)(-(2 + (i % 4)));
        s_state.particles[i].alpha = (uint8_t)(180 + (i * 9));
        s_state.particles[i].active = true;
        spawned++;
    }
}

static void water_cb(lv_event_t *e) {
    (void)e;
    garden_ui_button_event(0);
}
```

- [ ] **Step 2: Add `build_layout()` to `garden_ui.c`**

```c
static void build_layout(void) {
    s_screen = lv_screen_active();
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x87CEEB), 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    /* ── Left status bar (x=0, w=120) ── */
    lv_obj_t *left = lv_obj_create(s_screen);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_size(left, 120, 452);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x5A8A3A), 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 8, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_label_temp = lv_label_create(left);
    lv_label_set_text(s_label_temp, "26 C");
    lv_obj_set_style_text_color(s_label_temp, lv_color_hex(0xFFFF88), 0);

    s_label_devices = lv_label_create(left);
    lv_label_set_text(s_label_devices, "DEV: 2");
    lv_obj_set_style_text_color(s_label_devices, lv_color_hex(0xAAFFAA), 0);

    s_label_status = lv_label_create(left);
    lv_label_set_text(s_label_status, "ONLINE");
    lv_obj_set_style_text_color(s_label_status, lv_color_hex(0xAAFFAA), 0);

    lv_obj_t *hub = lv_label_create(left);
    lv_label_set_text(hub, "HUB ***");
    lv_obj_set_style_text_color(hub, lv_color_hex(0xFFDD88), 0);

    /* ── Center garden canvas (x=120, w=900) ── */
    s_canvas_sky = lv_canvas_create(s_screen);
    lv_canvas_set_buffer(s_canvas_sky, s_sky_buf, SKY_W, SKY_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_sky, 120, 0);

    s_canvas_particles = lv_canvas_create(s_screen);
    lv_canvas_set_buffer(s_canvas_particles, s_part_buf, PART_W, PART_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_particles, 120, 0);
    lv_obj_set_style_opa(s_canvas_particles, LV_OPA_TRANSP, 0);

    s_canvas_plant = lv_canvas_create(s_screen);
    lv_canvas_set_buffer(s_canvas_plant, s_plant_buf, PLANT_W, PLANT_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_plant, 120 + SKY_W/2 - PLANT_W/2, 452 - PLANT_H);

    /* Add click handler on sky canvas for touch-to-water */
    lv_obj_add_flag(s_canvas_sky, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas_sky, water_cb, LV_EVENT_CLICKED, NULL);

    /* ── Right info bar (x=1020, w=260) ── */
    lv_obj_t *right = lv_obj_create(s_screen);
    lv_obj_set_pos(right, 1020, 0);
    lv_obj_set_size(right, 260, 452);
    lv_obj_set_style_bg_color(right, lv_color_hex(0x5A8A3A), 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 10, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *elabel = lv_label_create(right);
    lv_label_set_text(elabel, "Energy");
    lv_obj_set_style_text_color(elabel, lv_color_hex(0xFFFF88), 0);

    s_bar_energy = lv_bar_create(right);
    lv_obj_set_size(s_bar_energy, 220, 16);
    lv_bar_set_range(s_bar_energy, 0, 100);
    lv_bar_set_value(s_bar_energy, s_state.energy_x10 / 10, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_energy, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(s_bar_energy, lv_color_hex(0x44FF44), LV_PART_INDICATOR);

    s_label_level = lv_label_create(right);
    lv_label_set_text(s_label_level, "Lv.3 PLANT");
    lv_obj_set_style_text_color(s_label_level, lv_color_hex(0xFFFFAA), 0);

    s_label_streak = lv_label_create(right);
    lv_label_set_text(s_label_streak, "Streak: 7d");
    lv_obj_set_style_text_color(s_label_streak, lv_color_hex(0xAAFFAA), 0);

    /* Water button */
    s_btn_water = lv_btn_create(right);
    lv_obj_set_size(s_btn_water, 180, 44);
    lv_obj_set_style_bg_color(s_btn_water, lv_color_hex(0xFFDD44), 0);
    lv_obj_add_event_cb(s_btn_water, water_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(s_btn_water);
    lv_label_set_text(btn_lbl, "WATER");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0x333333), 0);
    lv_obj_center(btn_lbl);
}
```

- [ ] **Step 3: Commit**

```bash
git add garden-ui-sim/garden_ui.c
git commit -m "feat: garden_ui build_layout - 3-panel layout with canvas + water button"
```

---

## Task 6: garden_ui.c — draw_sky() + draw_plant()

**Files:**
- Modify: `garden-ui-sim/garden_ui.c`

- [ ] **Step 1: Add `draw_sky()` to `garden_ui.c`**

```c
static void draw_sky(void) {
    lv_canvas_fill_bg(s_canvas_sky, lv_color_hex(0x87CEEB), LV_OPA_COVER);

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.radius = 0;

    /* Ground strip */
    rect.bg_color = lv_color_hex(0x8B6914);
    lv_canvas_draw_rect(s_canvas_sky, 0, SKY_H - 40, SKY_W, 40, &rect);

    /* Grass edge */
    rect.bg_color = lv_color_hex(0x5A8A3A);
    lv_canvas_draw_rect(s_canvas_sky, 0, SKY_H - 44, SKY_W, 8, &rect);

    /* Cloud 1 (pixel blocks) */
    rect.bg_color = lv_color_hex(0xFFFFFF);
    lv_canvas_draw_rect(s_canvas_sky, s_cloud1_x,      30, 32, 12, &rect);
    lv_canvas_draw_rect(s_canvas_sky, s_cloud1_x + 8,  20, 20, 14, &rect);
    lv_canvas_draw_rect(s_canvas_sky, s_cloud1_x + 24, 26, 16, 10, &rect);

    /* Cloud 2 */
    lv_canvas_draw_rect(s_canvas_sky, s_cloud2_x,      60, 40, 12, &rect);
    lv_canvas_draw_rect(s_canvas_sky, s_cloud2_x + 10, 50, 24, 14, &rect);
}
```

- [ ] **Step 2: Add `draw_plant()` to `garden_ui.c`**

4-frame pixel art plant (mature stage, Stardew Valley style):

```c
/* Pixel art plant: 4 frames of gentle sway */
static const uint32_t PLANT_COLORS[] = {
    0x6AAA2A, /* stem */
    0x44CC44, /* leaf light */
    0x228822, /* leaf dark */
    0xFF6644, /* flower */
    0xFF88AA, /* petal */
    0xFFDD44, /* center */
};

static void draw_plant(void) {
    lv_canvas_fill_bg(s_canvas_plant, lv_color_hex(0x87CEEB), LV_OPA_TRANSP);

    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = 0;

    uint8_t f = s_state.plant_frame;
    /* Stem sway: offset alternates -2, 0, +2, 0 */
    int8_t sway[] = {-2, 0, 2, 0};
    int16_t sx = 56 + sway[f]; /* stem center x */

    /* Stem */
    r.bg_color = lv_color_hex(PLANT_COLORS[0]);
    lv_canvas_draw_rect(s_canvas_plant, sx - 4, 80, 8, 70, &r);

    /* Left leaf */
    r.bg_color = lv_color_hex(PLANT_COLORS[1]);
    lv_canvas_draw_rect(s_canvas_plant, sx - 28, 100, 24, 12, &r);
    r.bg_color = lv_color_hex(PLANT_COLORS[2]);
    lv_canvas_draw_rect(s_canvas_plant, sx - 28, 100, 8, 8, &r);

    /* Right leaf */
    r.bg_color = lv_color_hex(PLANT_COLORS[1]);
    lv_canvas_draw_rect(s_canvas_plant, sx + 8, 110, 24, 12, &r);
    r.bg_color = lv_color_hex(PLANT_COLORS[2]);
    lv_canvas_draw_rect(s_canvas_plant, sx + 24, 110, 8, 8, &r);

    /* Flower head */
    r.bg_color = lv_color_hex(PLANT_COLORS[4]);
    lv_canvas_draw_rect(s_canvas_plant, sx - 20, 50, 16, 16, &r);
    lv_canvas_draw_rect(s_canvas_plant, sx + 4,  50, 16, 16, &r);
    lv_canvas_draw_rect(s_canvas_plant, sx - 8,  36, 16, 16, &r);
    lv_canvas_draw_rect(s_canvas_plant, sx - 8,  64, 16, 16, &r);

    /* Flower center */
    r.bg_color = lv_color_hex(PLANT_COLORS[5]);
    lv_canvas_draw_rect(s_canvas_plant, sx - 12, 48, 24, 24, &r);
    r.bg_color = lv_color_hex(PLANT_COLORS[3]);
    lv_canvas_draw_rect(s_canvas_plant, sx - 8,  52, 16, 16, &r);
}
```

- [ ] **Step 3: Commit**

```bash
git add garden-ui-sim/garden_ui.c
git commit -m "feat: garden_ui pixel art sky + 4-frame plant animation"
```

---

## Task 7: garden_ui.c — draw_particles()

**Files:**
- Modify: `garden-ui-sim/garden_ui.c`

- [ ] **Step 1: Add `draw_particles()` to `garden_ui.c`**

```c
static void draw_particles(void) {
    /* Clear with transparent background */
    lv_canvas_fill_bg(s_canvas_particles, lv_color_hex(0x000000), LV_OPA_TRANSP);

    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = 0;

    for (int i = 0; i < 8; i++) {
        if (!s_state.particles[i].active) continue;
        r.bg_color = lv_color_hex(0x44FF88);
        r.bg_opa   = s_state.particles[i].alpha;
        lv_canvas_draw_rect(s_canvas_particles,
                            s_state.particles[i].x,
                            s_state.particles[i].y,
                            6, 6, &r);
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add garden-ui-sim/garden_ui.c
git commit -m "feat: garden_ui particle draw with alpha fade"
```

---

## Task 8: main.c — SDL2 + LVGL init + event loop

**Files:**
- Create: `garden-ui-sim/main.c`

- [ ] **Step 1: Create `garden-ui-sim/main.c`**

```c
#include "lvgl.h"
#include "garden_ui.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define DISP_W 1280
#define DISP_H 452

static SDL_Window   *s_window   = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture  = NULL;

/* ── LVGL display flush ── */
static void sdl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    SDL_Rect rect = { area->x1, area->y1, w, h };
    SDL_UpdateTexture(s_texture, &rect, px_map, w * 2); /* RGB565 = 2 bytes */
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
    lv_display_flush_ready(disp);
}

/* ── Button timing state ── */
static bool     s_enter_down     = false;
static uint32_t s_enter_down_ms  = 0;
static uint32_t s_last_enter_up  = 0;

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* SDL2 init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    s_window = SDL_CreateWindow("Garden UI Sim",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                DISP_W, DISP_H, SDL_WINDOW_SHOWN);
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    s_texture  = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565,
                                   SDL_TEXTUREACCESS_STREAMING, DISP_W, DISP_H);

    /* LVGL init */
    lv_init();

    lv_display_t *disp = lv_display_create(DISP_W, DISP_H);
    static uint16_t buf1[DISP_W * 40];
    static uint16_t buf2[DISP_W * 40];
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, sdl_flush_cb);

    /* Pointer indev (mouse = touch) */
    lv_indev_t *mouse_indev = lv_indev_create();
    lv_indev_set_type(mouse_indev, LV_INDEV_TYPE_POINTER);
    /* Read callback: poll SDL mouse state each LVGL read */
    lv_indev_set_read_cb(mouse_indev, NULL); /* handled via SDL events below */

    /* Build garden UI */
    garden_ui_init();

    uint32_t last_tick = SDL_GetTicks();
    bool running = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;

            /* Mouse → touch */
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    lv_indev_data_t d = { .point = {ev.button.x, ev.button.y},
                                          .state = LV_INDEV_STATE_PRESSED };
                    lv_indev_read(mouse_indev, &d);
                    garden_ui_touch_event((int16_t)ev.button.x,
                                          (int16_t)ev.button.y, true);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    lv_indev_data_t d = { .point = {ev.button.x, ev.button.y},
                                          .state = LV_INDEV_STATE_RELEASED };
                    lv_indev_read(mouse_indev, &d);
                    garden_ui_touch_event((int16_t)ev.button.x,
                                          (int16_t)ev.button.y, false);
                }
                break;
            case SDL_MOUSEMOTION:
                if (ev.motion.state & SDL_BUTTON_LMASK) {
                    garden_ui_touch_event((int16_t)ev.motion.x,
                                          (int16_t)ev.motion.y, true);
                }
                break;

            /* Keyboard → encoder + button */
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_LEFT:
                    garden_ui_encoder_event(-1);
                    break;
                case SDLK_RIGHT:
                    garden_ui_encoder_event(+1);
                    break;
                case SDLK_RETURN:
                    if (!s_enter_down) {
                        s_enter_down    = true;
                        s_enter_down_ms = SDL_GetTicks();
                    }
                    break;
                }
                break;
            case SDL_KEYUP:
                if (ev.key.keysym.sym == SDLK_RETURN && s_enter_down) {
                    uint32_t held = SDL_GetTicks() - s_enter_down_ms;
                    uint32_t now  = SDL_GetTicks();
                    if (held >= 500) {
                        garden_ui_button_event(1); /* long press */
                    } else if (now - s_last_enter_up < 300) {
                        garden_ui_button_event(2); /* double click */
                    } else {
                        garden_ui_button_event(0); /* short press */
                    }
                    s_last_enter_up = now;
                    s_enter_down    = false;
                }
                break;
            }
        }

        uint32_t now     = SDL_GetTicks();
        uint32_t elapsed = now - last_tick;
        last_tick = now;

        garden_ui_tick(elapsed);
        lv_timer_handler();

        SDL_Delay(16); /* ~60fps cap */
    }

    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}
```

- [ ] **Step 2: Build and run**

```bash
cd garden-ui-sim
cmake -B build -G "MinGW Makefiles"
cmake --build build -j4
./build/garden-sim.exe
```

Expected: 1280×452 window opens showing bright pixel-art garden with animated plant, moving clouds, floating particles, and WATER button on the right panel. Mouse click on garden or WATER button triggers energy bar increase.

- [ ] **Step 3: Commit**

```bash
git add garden-ui-sim/main.c
git commit -m "feat: main.c SDL2+LVGL init, mouse/keyboard input mapping, main loop"
```

---

## Task 9: Final integration check + .gitignore

**Files:**
- Modify: `.gitignore` (root)

- [ ] **Step 1: Add build artifacts to .gitignore**

Add to root `.gitignore` (create if missing):
```
garden-ui-sim/build/
.superpowers/
```

- [ ] **Step 2: Verify full build from clean state**

```bash
rm -rf garden-ui-sim/build
cd garden-ui-sim
cmake -B build -G "MinGW Makefiles"
cmake --build build -j4
./build/garden-sim.exe
```

Expected: clean build, window opens, all animations running.

- [ ] **Step 3: Verify ESP32 project untouched**

```bash
cd lvgl9.5-demo
idf.py build
```

Expected: builds successfully with no changes.

- [ ] **Step 4: Final commit**

```bash
git add .gitignore
git commit -m "chore: ignore garden-ui-sim build artifacts and .superpowers"
```


