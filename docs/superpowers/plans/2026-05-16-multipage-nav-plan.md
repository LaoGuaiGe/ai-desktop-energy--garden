# Multi-Page Navigation Architecture — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor 2-page hardcoded UI into N-page navigation with independent page modules, coordinated swipe transitions, and clean separation of concerns.

**Architecture:** New `garden_nav.c` hub manages page registry, touch drag, snap animation, and tick dispatch. Each page implements a 4-function contract (`create/destroy/tick/on_button`). All pages are full-screen siblings under the screen, positioned horizontally by the nav layer.

**Tech Stack:** ESP32-P4, LVGL 9.5, C11, IDF component build

---

## File Map

| File | Role | Lines (est.) |
|------|------|------|
| `garden_nav.h` | NEW — nav hub API + page_def_t contract | ~35 |
| `garden_nav.c` | NEW — page registry, drag/snap, tick dispatch | ~130 |
| `garden_page.h` | REWRITE from garden_ui.h — garden page API only | ~15 |
| `garden_page.c` | REFACTOR from garden_ui.c — garden as page module, nav logic removed | ~1000 → ~900 |
| `garden_focus.h` | MODIFY — expose create/destroy/done_cb, remove old API | ~20 |
| `garden_focus.c` | MODIFY — adapt to page contract, remove drag/gesture callbacks | ~320 → ~280 |
| `garden_ai.h` / `garden_ai.c` | NEW — placeholder page | ~10 / ~20 |
| `garden_device.h` / `garden_device.c` | NEW — placeholder page | ~10 / ~20 |
| `garden_album.h` / `garden_album.c` | NEW — placeholder page | ~10 / ~20 |
| `main.c` | MODIFY — use nav layer, add long-press detection | ~425 → ~440 |
| `CMakeLists.txt` | MODIFY — add new source files | ~3 → ~4 lines |

---

### Task 1: Create garden_nav.h

**Files:**
- Create: `lvgl9.5-demo/main/garden_nav.h`

- [ ] **Step 1: Write garden_nav.h**

```c
#ifndef GARDEN_NAV_H
#define GARDEN_NAV_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Page contract — every page module implements these 5 functions */
typedef struct {
    const char *name;
    lv_obj_t *  (*create)(lv_obj_t *parent);
    void        (*destroy)(lv_obj_t *page);
    void        (*tick)(uint32_t elapsed_ms);
    bool        (*on_button)(uint8_t type);  /* 0=short press, 1=long press; return true if handled */
    void        (*set_active)(bool active);  /* optional — nav layer notifies page when it becomes active/inactive */
} garden_page_def_t;

#define GARDEN_NAV_MAX_PAGES 8
#define GARDEN_NAV_HOME_INDEX 1   /* garden page is HOME */

void garden_nav_init(lv_obj_t *screen);
void garden_nav_register(int index, const garden_page_def_t *def);
void garden_nav_go_home(bool animate);
void garden_nav_tick(uint32_t elapsed_ms);
void garden_nav_button(uint8_t type);       /* 0=short, 1=long */

/* Reserved for future encoder hardware */
void garden_nav_encoder(int delta);
void garden_nav_encoder_press(void);

#endif
```

---

### Task 2: Create garden_nav.c

**Files:**
- Create: `lvgl9.5-demo/main/garden_nav.c`

- [ ] **Step 1: Write garden_nav.c — navigation hub implementation**

```c
#include "garden_nav.h"
#include <string.h>

#define DISP_W          1280
#define DISP_H          452
#define DRAG_DEADZONE   10     /* px before drag engages */
#define DRAG_THRESHOLD  (DISP_W / 5)  /* 256px to commit page switch */
#define SNAP_ANIM_MS    200

/* ── Nav state ── */
static struct {
    lv_obj_t *screen;
    const garden_page_def_t *defs[GARDEN_NAV_MAX_PAGES];
    lv_obj_t *objs[GARDEN_NAV_MAX_PAGES];
    int count;
    int current;
    int home_index;

    /* Drag state */
    int16_t drag_start_x;
    int16_t drag_offset;
    bool    dragging;
} s_nav;

static int32_t s_anim_dummy;  /* placeholder for lv_anim_set_var */

/* ── Forward declarations ── */
static void position_pages(void);
static void show_adjacent_pages(void);
static void nav_drag_cb(lv_event_t *e);
static void snap_to_page(int target);
static void snap_anim_cb(void *var, int32_t v);
static void snap_ready_cb(lv_anim_t *a);

/* ── Public API ── */

void garden_nav_init(lv_obj_t *screen) {
    memset(&s_nav, 0, sizeof(s_nav));
    s_nav.screen     = screen;
    s_nav.home_index = GARDEN_NAV_HOME_INDEX;
    s_nav.current    = s_nav.home_index;

    /* Nav layer intercepts all touch drag on the screen */
    lv_obj_add_event_cb(screen, nav_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen, nav_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(screen, nav_drag_cb, LV_EVENT_RELEASED, NULL);
}

void garden_nav_register(int index, const garden_page_def_t *def) {
    if (index < 0 || index >= GARDEN_NAV_MAX_PAGES || !def || !def->create) return;

    s_nav.defs[index] = def;
    if (index >= s_nav.count) s_nav.count = index + 1;

    s_nav.objs[index] = def->create(s_nav.screen);
    if (s_nav.objs[index]) {
        lv_obj_add_flag(s_nav.objs[index], LV_OBJ_FLAG_HIDDEN);
    }

    position_pages();
    show_adjacent_pages();
}

void garden_nav_go_home(bool animate) {
    if (animate) {
        snap_to_page(s_nav.home_index);
    } else {
        s_nav.current    = s_nav.home_index;
        s_nav.drag_offset = 0;
        position_pages();
        show_adjacent_pages();
    }
}

void garden_nav_tick(uint32_t elapsed_ms) {
    /* Tick current page and neighbors only */
    for (int i = 0; i < s_nav.count; i++) {
        if (!s_nav.defs[i] || !s_nav.defs[i]->tick) continue;
        if (i < s_nav.current - 1 || i > s_nav.current + 1) continue;
        s_nav.defs[i]->tick(elapsed_ms);
    }
}

void garden_nav_button(uint8_t type) {
    if (type == 1) {
        /* Long press = global HOME */
        garden_nav_go_home(true);
        return;
    }
    /* Short press → current page */
    int idx = s_nav.current;
    if (idx >= 0 && idx < s_nav.count && s_nav.defs[idx] && s_nav.defs[idx]->on_button) {
        s_nav.defs[idx]->on_button(type);
    }
}

void garden_nav_encoder(int delta) {
    (void)delta;
    /* Reserved — no hardware */
}

void garden_nav_encoder_press(void) {
    /* Reserved — same as short press */
    garden_nav_button(0);
}

/* ── Internal helpers ── */

static void position_pages(void) {
    for (int i = 0; i < s_nav.count; i++) {
        lv_obj_t *page = s_nav.objs[i];
        if (!page) continue;
        int16_t x = (int16_t)((i - s_nav.current) * DISP_W + s_nav.drag_offset);
        lv_obj_set_pos(page, x, 0);
    }
}

static void show_adjacent_pages(void) {
    for (int i = 0; i < s_nav.count; i++) {
        lv_obj_t *page = s_nav.objs[i];
        if (!page) continue;
        if (i >= s_nav.current - 1 && i <= s_nav.current + 1) {
            lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ── Drag handling ── */

static void nav_drag_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_nav.drag_start_x = p.x;
        s_nav.drag_offset  = 0;
        s_nav.dragging     = false;
    } else if (code == LV_EVENT_PRESSING) {
        int16_t dx = (int16_t)(p.x - s_nav.drag_start_x);
        if (dx < -DRAG_DEADZONE || dx > DRAG_DEADZONE || s_nav.dragging) {
            s_nav.dragging = true;
            /* Pause canvas rendering on garden page during drag */
            if (s_nav.defs[s_nav.current] && s_nav.defs[s_nav.current]->set_active) {
                s_nav.defs[s_nav.current]->set_active(false);
            }
            /* Clamp: at most one page worth of drag in either direction */
            if (dx < -DISP_W) dx = -DISP_W;
            if (dx >  DISP_W) dx =  DISP_W;
            /* Block drag past boundaries */
            if (s_nav.current == 0 && dx > 0) dx = 0;
            if (s_nav.current == s_nav.count - 1 && dx < 0) dx = 0;
            s_nav.drag_offset = dx;
            position_pages();
            show_adjacent_pages();
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (!s_nav.dragging) return;
        s_nav.dragging = false;

        /* Resume canvas rendering */
        if (s_nav.defs[s_nav.current] && s_nav.defs[s_nav.current]->set_active) {
            s_nav.defs[s_nav.current]->set_active(true);
        }

        int16_t dx = s_nav.drag_offset;
        if (dx < -DRAG_THRESHOLD && s_nav.current < s_nav.count - 1) {
            snap_to_page(s_nav.current + 1);
        } else if (dx > DRAG_THRESHOLD && s_nav.current > 0) {
            snap_to_page(s_nav.current - 1);
        } else {
            snap_to_page(s_nav.current);  /* spring back */
        }
    }
}

/* ── Snap animation ── */

static void snap_anim_cb(void *var, int32_t v) {
    (void)var;
    s_nav.drag_offset = (int16_t)v;
    position_pages();
}

static void snap_ready_cb(lv_anim_t *a) {
    (void)a;
    s_nav.drag_offset = 0;
    position_pages();
    show_adjacent_pages();
    /* Notify new current page it's active */
    if (s_nav.defs[s_nav.current] && s_nav.defs[s_nav.current]->set_active) {
        s_nav.defs[s_nav.current]->set_active(true);
    }
}

static void snap_to_page(int target) {
    int16_t from = s_nav.drag_offset;
    int16_t to   = 0;
    s_nav.current = target;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_anim_dummy);
    lv_anim_set_exec_cb(&anim, snap_anim_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, SNAP_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&anim, snap_ready_cb);
    lv_anim_start(&anim);
}
```

---

### Task 3: Adapt garden_focus.h to page contract

**Files:**
- Modify: `lvgl9.5-demo/main/garden_focus.h`

- [ ] **Step 1: Rewrite garden_focus.h**

Replace the entire file content:

```c
#ifndef GARDEN_FOCUS_H
#define GARDEN_FOCUS_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Page contract — called by garden_nav */
lv_obj_t * garden_focus_create(lv_obj_t *parent);
void       garden_focus_destroy(lv_obj_t *page);
void       garden_focus_tick(uint32_t elapsed_ms);
bool       garden_focus_on_button(uint8_t type);

/* Cross-page callback: nav layer wires this to garden_page_add_energy */
typedef void (*garden_focus_done_cb_t)(uint16_t reward_x10);
void garden_focus_set_done_cb(garden_focus_done_cb_t cb);

/* Optional: nav layer notifies focus page of active state */
void garden_focus_set_active(bool active);

#endif
```

---

### Task 4: Adapt garden_focus.c to page contract

**Files:**
- Modify: `lvgl9.5-demo/main/garden_focus.c`

- [ ] **Step 1: Replace includes and type definitions — remove event enum, add done_cb**

Change lines 1-27. Replace the `#include`, `#define` block, and `typedef struct` with:

```c
#include "garden_focus.h"
#include <stdio.h>

#define DISP_W         1280
#define DISP_H         452
#define LEFT_W         120
#define RIGHT_W        260
#define SCENE_W        (DISP_W - LEFT_W - RIGHT_W)
#define SCENE_X        LEFT_W
#define FOCUS_TOTAL_MS (25U * 60U * 1000U)

typedef struct {
    lv_obj_t *page;
    lv_obj_t *time_label;
    lv_obj_t *state_label;
    lv_obj_t *button_label;
    lv_obj_t *progress;
    garden_focus_done_cb_t done_cb;
    uint32_t remaining_ms;
    bool running;
} garden_focus_t;

static garden_focus_t s_focus;
```

- [ ] **Step 2: Rewrite the public API functions — replace garden_focus_init with garden_focus_create**

Delete the old `garden_focus_init` (lines 36-171) and replace with:

```c
lv_obj_t * garden_focus_create(lv_obj_t *parent) {
    s_focus.remaining_ms = FOCUS_TOTAL_MS;
    s_focus.running = false;
    s_focus.done_cb = NULL;

    s_focus.page = lv_obj_create(parent);
    lv_obj_set_pos(s_focus.page, 0, 0);
    lv_obj_set_size(s_focus.page, DISP_W, DISP_H);
    lv_obj_set_style_bg_color(s_focus.page, lv_color_hex(0x14222A), 0);
    lv_obj_set_style_border_width(s_focus.page, 0, 0);
    lv_obj_set_style_radius(s_focus.page, 0, 0);
    lv_obj_set_style_pad_all(s_focus.page, 0, 0);
    lv_obj_clear_flag(s_focus.page, LV_OBJ_FLAG_SCROLLABLE);
    /* NOTE: NO drag/gesture event callbacks — nav layer handles all navigation */

    /* ── Left sidebar ── */
    lv_obj_t *left = lv_obj_create(s_focus.page);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_size(left, LEFT_W, DISP_H);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x1D343D), 0);
    lv_obj_set_style_border_width(left, 3, 0);
    lv_obj_set_style_border_color(left, lv_color_hex(0x345C68), 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_set_style_pad_all(left, 10, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 8, 0);

    lv_obj_t *l_title = lv_label_create(left);
    lv_label_set_text(l_title, "FOCUS");
    lv_obj_set_style_text_color(l_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l_title, &lv_font_montserrat_14, 0);

    lv_obj_t *l_mode = lv_label_create(left);
    lv_label_set_text(l_mode, "POMO\n25 MIN");
    lv_obj_set_style_text_color(l_mode, lv_color_hex(0xA6E3E9), 0);
    lv_obj_set_style_text_font(l_mode, &lv_font_montserrat_14, 0);

    lv_obj_t *l_hint = lv_label_create(left);
    lv_label_set_text(l_hint, "< swipe\nto garden");
    lv_obj_set_style_text_color(l_hint, lv_color_hex(0x88AAB0), 0);
    lv_obj_set_style_text_font(l_hint, &lv_font_montserrat_14, 0);

    /* ── Center area ── */
    lv_obj_t *center = lv_obj_create(s_focus.page);
    lv_obj_set_pos(center, LEFT_W, 0);
    lv_obj_set_size(center, SCENE_W, DISP_H);
    lv_obj_set_style_bg_color(center, lv_color_hex(0x18313A), 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_radius(center, 0, 0);
    lv_obj_set_style_pad_all(center, 0, 0);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *task = lv_label_create(center);
    lv_label_set_text(task, "DEEP WORK");
    lv_obj_set_style_text_color(task, lv_color_hex(0xB8F3D4), 0);
    lv_obj_set_style_text_font(task, &lv_font_montserrat_14, 0);
    lv_obj_align(task, LV_ALIGN_TOP_MID, 0, 50);

    s_focus.time_label = lv_label_create(center);
    lv_label_set_text(s_focus.time_label, "25:00");
    lv_obj_set_style_text_color(s_focus.time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_focus.time_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_focus.time_label, LV_ALIGN_CENTER, 0, -34);

    s_focus.state_label = lv_label_create(center);
    lv_label_set_text(s_focus.state_label, "READY");
    lv_obj_set_style_text_color(s_focus.state_label, lv_color_hex(0xFFDD88), 0);
    lv_obj_set_style_text_font(s_focus.state_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_focus.state_label, LV_ALIGN_CENTER, 0, 18);

    s_focus.progress = lv_bar_create(center);
    lv_obj_set_size(s_focus.progress, 520, 18);
    lv_bar_set_range(s_focus.progress, 0, 100);
    lv_bar_set_value(s_focus.progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_focus.progress, lv_color_hex(0x0A171B), 0);
    lv_obj_set_style_bg_color(s_focus.progress, lv_color_hex(0x66D9A8), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_focus.progress, 3, 0);
    lv_obj_set_style_radius(s_focus.progress, 3, LV_PART_INDICATOR);
    lv_obj_align(s_focus.progress, LV_ALIGN_CENTER, 0, 64);

    /* ── Right sidebar ── */
    lv_obj_t *right = lv_obj_create(s_focus.page);
    lv_obj_set_pos(right, SCENE_X + SCENE_W, 0);
    lv_obj_set_size(right, RIGHT_W, DISP_H);
    lv_obj_set_style_bg_color(right, lv_color_hex(0x1D343D), 0);
    lv_obj_set_style_border_width(right, 3, 0);
    lv_obj_set_style_border_color(right, lv_color_hex(0x345C68), 0);
    lv_obj_set_style_border_side(right, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_set_style_pad_all(right, 14, 0);
    lv_obj_set_style_pad_top(right, 20, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 10, 0);

    lv_obj_t *r_title = lv_label_create(right);
    lv_label_set_text(r_title, "SESSION");
    lv_obj_set_style_text_color(r_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(r_title, &lv_font_montserrat_14, 0);

    lv_obj_t *reward = lv_label_create(right);
    lv_label_set_text(reward, "+15 ENERGY\non finish");
    lv_obj_set_style_text_color(reward, lv_color_hex(0xA6E3E9), 0);
    lv_obj_set_style_text_font(reward, &lv_font_montserrat_14, 0);

    /* Start/pause button — CLICKED only, nav handles drag */
    lv_obj_t *start_btn = lv_btn_create(right);
    lv_obj_set_size(start_btn, 230, 56);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x66D9A8), 0);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x38B67E), LV_STATE_PRESSED);
    lv_obj_set_style_radius(start_btn, 6, 0);
    lv_obj_add_event_cb(start_btn, start_pause_cb, LV_EVENT_CLICKED, NULL);
    s_focus.button_label = lv_label_create(start_btn);
    lv_label_set_text(s_focus.button_label, "START");
    lv_obj_set_style_text_color(s_focus.button_label, lv_color_hex(0x0A171B), 0);
    lv_obj_set_style_text_font(s_focus.button_label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_focus.button_label);

    /* Reset button */
    lv_obj_t *reset_btn = lv_btn_create(right);
    lv_obj_set_size(reset_btn, 230, 48);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x345C68), 0);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x264650), LV_STATE_PRESSED);
    lv_obj_set_style_radius(reset_btn, 6, 0);
    lv_obj_add_event_cb(reset_btn, reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "RESET");
    lv_obj_set_style_text_color(reset_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(reset_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(reset_lbl);

    update_labels();
    return s_focus.page;
}
```

- [ ] **Step 3: Add destroy, on_button, set_done_cb — insert after garden_focus_create**

```c
void garden_focus_destroy(lv_obj_t *page) {
    if (page) lv_obj_delete(page);
    memset(&s_focus, 0, sizeof(s_focus));
}

bool garden_focus_on_button(uint8_t type) {
    (void)type;
    garden_focus_start_pause();
    return true;
}

void garden_focus_set_done_cb(garden_focus_done_cb_t cb) {
    s_focus.done_cb = cb;
}

void garden_focus_set_active(bool active) {
    (void)active;  /* focus page has no heavy rendering to pause */
}
```

- [ ] **Step 4: Delete old public API functions — remove these functions entirely**

Delete: `garden_focus_set_visible()`, `garden_focus_drag_to()`, `garden_focus_finish_drag()`, `garden_focus_is_visible()`, `garden_focus_start_pause()`, `garden_focus_reset()`, `page_slide_ready_cb()`.

- [ ] **Step 5: Replace garden_focus_tick — update the completion callback**

Replace the old `garden_focus_tick` (lines 231-244) with:

```c
void garden_focus_tick(uint32_t elapsed_ms) {
    if (!s_focus.running) return;

    if (s_focus.remaining_ms > elapsed_ms) {
        s_focus.remaining_ms -= elapsed_ms;
    } else {
        s_focus.remaining_ms = 0;
        s_focus.running = false;
        if (s_focus.done_cb) {
            s_focus.done_cb(150);  /* +15.0 energy reward */
        }
    }
    update_labels();
}
```

- [ ] **Step 6: Replace internal helpers — keep these as static, remove gesture/drag callbacks**

Delete `gesture_cb()` (lines 282-289) and `drag_cb()` (lines 291-318) entirely. Keep `update_labels()`, `start_pause_cb()`, `reset_cb()` as static functions.

Make `garden_focus_start_pause()` and `garden_focus_reset()` static:

```c
static void garden_focus_start_pause(void) {
    if (s_focus.remaining_ms == 0) {
        s_focus.remaining_ms = FOCUS_TOTAL_MS;
    }
    s_focus.running = !s_focus.running;
    update_labels();
}

static void garden_focus_reset(void) {
    s_focus.running = false;
    s_focus.remaining_ms = FOCUS_TOTAL_MS;
    update_labels();
}
```

---

### Task 5: Refactor garden_ui.h → garden_page.h

**Files:**
- Delete (git rm): `lvgl9.5-demo/main/garden_ui.h`
- Create: `lvgl9.5-demo/main/garden_page.h`

- [ ] **Step 1: Remove old header, create new**

```bash
git -C "Z:/ai-desktop-energy--garden" rm lvgl9.5-demo/main/garden_ui.h
```

- [ ] **Step 2: Write garden_page.h**

```c
#ifndef GARDEN_PAGE_H
#define GARDEN_PAGE_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Page contract functions — called by garden_nav */
lv_obj_t * garden_page_create(lv_obj_t *parent);
void       garden_page_destroy(lv_obj_t *page);
void       garden_page_tick(uint32_t elapsed_ms);
bool       garden_page_on_button(uint8_t type);

/* Cross-page API */
void garden_page_add_energy(uint16_t energy_x10);  /* focus done → energy reward */
void garden_page_set_active(bool active);           /* nav layer: pause canvas during drag */

#endif
```

---

### Task 6: Refactor garden_ui.c → garden_page.c

**Files:**
- Remove: `lvgl9.5-demo/main/garden_ui.c`
- Create: `lvgl9.5-demo/main/garden_page.c`

This is the largest change. The file keeps all garden rendering code but strips navigation logic.

- [ ] **Step 1: Remove old file**

```bash
git -C "Z:/ai-desktop-energy--garden" rm lvgl9.5-demo/main/garden_ui.c
```

- [ ] **Step 2: Write garden_page.c — page module with garden rendering**

The new file starts with the same includes, layout constants, and types (particle_t, waterdrop_t, garden_state_t), but:
- Remove `PAGE_GARDEN`/`PAGE_FOCUS` enum
- Remove `s_current_page`, `s_page_drag_start_x`, `s_page_drag_opening`
- Remove `garden_focus.h` include (nav handles focus)
- Keep all rendering code: draw_background, draw_scene, fill_rect, rgb888_to_565, spawn_particles
- Keep all animation state: particles, drops, clouds, butterfly, bird
- Keep FPS label and perf logging

Write the full file:

```c
#include "garden_page.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Layout constants ── */
#define DISP_W   1280
#define DISP_H   452
#define LEFT_W   120
#define RIGHT_W  260
#define SCENE_W  (DISP_W - LEFT_W - RIGHT_W)
#define SCENE_H  DISP_H
#define SCENE_X  LEFT_W
#define SCENE_BUF_SIZE (SCENE_W * SCENE_H * 2)
#define SCENE_IDLE_FRAME_MS   66
#define SCENE_ACTIVE_FRAME_MS 33
#define PERF_LOG_INTERVAL_MS  2000

static const char *TAG = "garden_page";

/* ── Particle ── */
typedef struct {
    int16_t  x, y;
    int16_t  vx, vy;
    uint8_t  alpha;
    bool     active;
} particle_t;

/* ── Water drop ── */
typedef struct {
    int16_t  x, y;
    int16_t  vy;
    bool     active;
} waterdrop_t;

/* ── Garden state ── */
typedef struct {
    uint8_t    plant_stage;
    uint16_t   energy_x10;
    uint8_t    streak_days;
    uint8_t    plant_frame;
    uint32_t   frame_timer;
    uint32_t   draw_timer;
    uint32_t   render_timer;
    uint32_t   perf_timer;
    uint32_t   perf_frames;
    uint32_t   last_draw_us;
    uint32_t   burst_timer;
    uint32_t   wet_timer;
    uint32_t   evolve_timer;
    bool       dirty;
    bool       active;       /* false when page is not visible */
    particle_t particles[8];
    waterdrop_t drops[12];
} garden_state_t;

static garden_state_t s_state;

/* ── LVGL handles ── */
static lv_obj_t *s_page;         /* container returned by create */
static lv_obj_t *s_canvas_scene;
static lv_obj_t *s_bar_energy;
static lv_obj_t *s_label_level;
static lv_obj_t *s_label_streak;
static lv_obj_t *s_label_fps;

/* Cloud positions */
static int16_t s_cloud1_x = 80;
static int16_t s_cloud2_x = 380;

/* Canvas buffers */
static uint8_t *s_scene_buf;
static uint8_t *s_bg_buf;
static bool s_bg_ready = false;

/* ── Forward declarations ── */
static void build_page(lv_obj_t *parent);
static void draw_background(void);
static void draw_scene(void);
static void spawn_particles(int count);

/* Butterfly / bird state */
static int16_t s_butterfly_x = 200, s_butterfly_y = 180;
static int8_t  s_butterfly_dx = 2, s_butterfly_dy = -1;
static uint8_t s_butterfly_frame = 0;
static int16_t s_bird_x = -30, s_bird_y = 60;

/* ── Page contract ── */

lv_obj_t * garden_page_create(lv_obj_t *parent) {
    memset(&s_state, 0, sizeof(s_state));
    s_bg_ready = false;
    s_state.plant_stage = 3;
    s_state.energy_x10  = 450;
    s_state.streak_days = 7;
    s_state.active = true;

    if (s_scene_buf == NULL) s_scene_buf = malloc(SCENE_BUF_SIZE);
    if (s_bg_buf == NULL)    s_bg_buf    = malloc(SCENE_BUF_SIZE);
    if (s_scene_buf == NULL || s_bg_buf == NULL) {
        printf("[garden_page] failed to allocate scene buffers\n");
        return NULL;
    }

    spawn_particles(4);
    build_page(parent);
    draw_scene();
    return s_page;
}

void garden_page_destroy(lv_obj_t *page) {
    if (page) lv_obj_delete(page);
    free(s_scene_buf); s_scene_buf = NULL;
    free(s_bg_buf);    s_bg_buf    = NULL;
}

void garden_page_tick(uint32_t elapsed_ms) {
    bool active_anim = false;

    /* Skip heavy rendering if page is being dragged away */
    if (!s_state.active) return;

    /* Plant animation: 4 frames x 400ms */
    s_state.frame_timer += elapsed_ms;
    if (s_state.frame_timer >= 400) {
        s_state.frame_timer = 0;
        s_state.plant_frame = (s_state.plant_frame + 1) % 4;
        s_state.dirty = true;
    }

    /* Timers */
    if (s_state.burst_timer > elapsed_ms) {
        s_state.burst_timer -= elapsed_ms; active_anim = true;
    } else s_state.burst_timer = 0;

    if (s_state.wet_timer > elapsed_ms) {
        s_state.wet_timer -= elapsed_ms; active_anim = true;
    } else s_state.wet_timer = 0;

    if (s_state.evolve_timer > elapsed_ms) {
        s_state.evolve_timer -= elapsed_ms; active_anim = true;
    } else s_state.evolve_timer = 0;

    for (int i = 0; i < 8; i++) {
        if (s_state.particles[i].active) { active_anim = true; break; }
    }
    for (int i = 0; i < 12; i++) {
        if (s_state.drops[i].active) { active_anim = true; break; }
    }

    uint32_t frame_ms = active_anim ? SCENE_ACTIVE_FRAME_MS : SCENE_IDLE_FRAME_MS;
    s_state.render_timer += elapsed_ms;
    if (s_state.render_timer < frame_ms && !s_state.dirty) {
        s_state.perf_timer += elapsed_ms;
        return;
    }
    s_state.render_timer = 0;
    if (active_anim) s_state.dirty = true;

    /* Move clouds */
    s_state.draw_timer += frame_ms;
    if (s_state.draw_timer >= SCENE_IDLE_FRAME_MS) {
        s_state.draw_timer = 0;
        s_cloud1_x = (int16_t)(s_cloud1_x + 1);
        s_cloud2_x = (int16_t)(s_cloud2_x + 1);
        if (s_cloud1_x > SCENE_W + 40) s_cloud1_x = -40;
        if (s_cloud2_x > SCENE_W + 40) s_cloud2_x = -40;
        s_state.dirty = true;
    }

    /* Update particles */
    for (int i = 0; i < 8; i++) {
        if (!s_state.particles[i].active) continue;
        s_state.particles[i].y = (int16_t)(s_state.particles[i].y + s_state.particles[i].vy);
        s_state.particles[i].x = (int16_t)(s_state.particles[i].x + s_state.particles[i].vx);
        s_state.particles[i].alpha = (uint8_t)(s_state.particles[i].alpha > 5
                                     ? s_state.particles[i].alpha - 5 : 0);
        if (s_state.particles[i].y < 0 || s_state.particles[i].alpha == 0) {
            s_state.particles[i].active = false;
        }
        s_state.dirty = true;
    }

    /* Update water drops */
    for (int i = 0; i < 12; i++) {
        if (!s_state.drops[i].active) continue;
        s_state.drops[i].y = (int16_t)(s_state.drops[i].y + s_state.drops[i].vy);
        if (s_state.drops[i].y > SCENE_H - 60) {
            s_state.drops[i].active = false;
            if (s_state.wet_timer == 0) s_state.wet_timer = 2000;
        }
        s_state.dirty = true;
    }

    if (s_state.dirty) {
        s_state.dirty = false;
        draw_scene();
        s_state.perf_frames++;
    }

    s_state.perf_timer += elapsed_ms;
    if (s_state.perf_timer >= PERF_LOG_INTERVAL_MS) {
        uint32_t fps_x10 = s_state.perf_frames * 10000U / s_state.perf_timer;
        if (s_label_fps) {
            char fps_text[32];
            snprintf(fps_text, sizeof(fps_text), "FPS %lu.%lu  %lums",
                     (unsigned long)(fps_x10 / 10),
                     (unsigned long)(fps_x10 % 10),
                     (unsigned long)(s_state.last_draw_us / 1000));
            lv_label_set_text(s_label_fps, fps_text);
        }
        ESP_LOGI(TAG, "draw=%lu us fps=%lu.%lu heap=%lu psram=%lu",
                 (unsigned long)s_state.last_draw_us,
                 (unsigned long)(fps_x10 / 10), (unsigned long)(fps_x10 % 10),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        s_state.perf_timer = 0;
        s_state.perf_frames = 0;
    }
}

bool garden_page_on_button(uint8_t type) {
    if (type != 0) return false;
    /* Short press = water */
    s_state.energy_x10 += 100;
    if (s_state.energy_x10 > 1000) s_state.energy_x10 = 1000;
    lv_bar_set_value(s_bar_energy, s_state.energy_x10 / 10, LV_ANIM_ON);
    s_state.burst_timer = 1000;
    s_state.wet_timer   = 0;
    spawn_particles(8);
    for (int i = 0; i < 12; i++) {
        s_state.drops[i].x  = (int16_t)(SCENE_W / 2 - 30 + (i * 5));
        s_state.drops[i].y  = (int16_t)(20 + (i * 11) % 60);
        s_state.drops[i].vy = (int16_t)(10 + (i % 4) * 2);
        s_state.drops[i].active = true;
    }
    s_state.dirty = true;
    if (s_state.energy_x10 >= 1000 && s_state.plant_stage < 5) {
        s_state.evolve_timer = 800;
    }
    return true;
}

void garden_page_add_energy(uint16_t energy_x10) {
    s_state.energy_x10 += energy_x10;
    if (s_state.energy_x10 > 1000) s_state.energy_x10 = 1000;
    lv_bar_set_value(s_bar_energy, s_state.energy_x10 / 10, LV_ANIM_ON);
    spawn_particles(8);
    s_state.burst_timer = 1200;
    s_state.dirty = true;
}

void garden_page_set_active(bool active) {
    s_state.active = active;
}

/* ── Internal helpers ── */

static void spawn_particles(int count) {
    for (int i = 0; i < 8 && i < count; i++) {
        s_state.particles[i].x      = (int16_t)(SCENE_W / 2 - 20 + i * 12);
        s_state.particles[i].y      = (int16_t)(SCENE_H - 80);
        s_state.particles[i].vy     = (int16_t)(-(2 + (i % 4)));
        s_state.particles[i].vx     = (int16_t)((i % 3) - 1);
        s_state.particles[i].alpha  = (uint8_t)(180 + i * 9);
        s_state.particles[i].active = true;
    }
}

static void build_page(lv_obj_t *parent) {
    s_page = lv_obj_create(parent);
    lv_obj_set_pos(s_page, 0, 0);
    lv_obj_set_size(s_page, DISP_W, DISP_H);
    lv_obj_set_style_bg_color(s_page, lv_color_hex(0x1A3A1A), 0);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    /* NOTE: NO drag/gesture callbacks — nav layer handles all navigation */

    /* ── Left status bar ── */
    lv_obj_t *left = lv_obj_create(s_page);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_size(left, LEFT_W, DISP_H);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x2A4A1A), 0);
    lv_obj_set_style_border_width(left, 3, 0);
    lv_obj_set_style_border_color(left, lv_color_hex(0x5A3A1A), 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_set_style_pad_all(left, 10, 0);
    lv_obj_set_style_pad_top(left, 16, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(left, 6, 0);

    lv_obj_t *l_title = lv_label_create(left);
    lv_label_set_text(l_title, "STATUS");
    lv_obj_set_style_text_color(l_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l_title, &lv_font_montserrat_14, 0);

    lv_obj_t *sep1 = lv_obj_create(left);
    lv_obj_set_size(sep1, 90, 2);
    lv_obj_set_style_bg_color(sep1, lv_color_hex(0x5A8A3A), 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_radius(sep1, 0, 0);
    lv_obj_set_style_pad_all(sep1, 0, 0);

    lv_obj_t *w_card = lv_obj_create(left);
    lv_obj_set_size(w_card, 100, 50);
    lv_obj_set_style_bg_color(w_card, lv_color_hex(0x1A3A0A), 0);
    lv_obj_set_style_border_width(w_card, 2, 0);
    lv_obj_set_style_border_color(w_card, lv_color_hex(0x4A7A3A), 0);
    lv_obj_set_style_radius(w_card, 4, 0);
    lv_obj_set_style_pad_all(w_card, 6, 0);
    lv_obj_clear_flag(w_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(w_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w_card, 2, 0);

    lv_obj_t *l_sun = lv_label_create(w_card);
    lv_label_set_text(l_sun, "* 26 C");
    lv_obj_set_style_text_color(l_sun, lv_color_hex(0xFFDD44), 0);
    lv_obj_set_style_text_font(l_sun, &lv_font_montserrat_14, 0);

    lv_obj_t *l_weather = lv_label_create(w_card);
    lv_label_set_text(l_weather, "Sunny");
    lv_obj_set_style_text_color(l_weather, lv_color_hex(0xAADDAA), 0);
    lv_obj_set_style_text_font(l_weather, &lv_font_montserrat_14, 0);

    lv_obj_t *d_card = lv_obj_create(left);
    lv_obj_set_size(d_card, 100, 70);
    lv_obj_set_style_bg_color(d_card, lv_color_hex(0x1A3A0A), 0);
    lv_obj_set_style_border_width(d_card, 2, 0);
    lv_obj_set_style_border_color(d_card, lv_color_hex(0x4A7A3A), 0);
    lv_obj_set_style_radius(d_card, 4, 0);
    lv_obj_set_style_pad_all(d_card, 6, 0);
    lv_obj_clear_flag(d_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(d_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d_card, 4, 0);

    lv_obj_t *l_dev = lv_label_create(d_card);
    lv_label_set_text(l_dev, "DEV: 2");
    lv_obj_set_style_text_color(l_dev, lv_color_hex(0xAAFFAA), 0);
    lv_obj_set_style_text_font(l_dev, &lv_font_montserrat_14, 0);

    lv_obj_t *l_net = lv_label_create(d_card);
    lv_label_set_text(l_net, "ONLINE");
    lv_obj_set_style_text_color(l_net, lv_color_hex(0x44FF88), 0);
    lv_obj_set_style_text_font(l_net, &lv_font_montserrat_14, 0);

    lv_obj_t *l_hub = lv_label_create(d_card);
    lv_label_set_text(l_hub, "HUB: 2/3");
    lv_obj_set_style_text_color(l_hub, lv_color_hex(0xFFDD88), 0);
    lv_obj_set_style_text_font(l_hub, &lv_font_montserrat_14, 0);

    lv_obj_t *t_card = lv_obj_create(left);
    lv_obj_set_size(t_card, 100, 40);
    lv_obj_set_style_bg_color(t_card, lv_color_hex(0x1A3A0A), 0);
    lv_obj_set_style_border_width(t_card, 2, 0);
    lv_obj_set_style_border_color(t_card, lv_color_hex(0x4A7A3A), 0);
    lv_obj_set_style_radius(t_card, 4, 0);
    lv_obj_set_style_pad_all(t_card, 6, 0);
    lv_obj_clear_flag(t_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l_time = lv_label_create(t_card);
    lv_label_set_text(l_time, "14:32");
    lv_obj_set_style_text_color(l_time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l_time, &lv_font_montserrat_14, 0);
    lv_obj_center(l_time);

    /* ── Center scene canvas ── */
    s_canvas_scene = lv_canvas_create(s_page);
    lv_canvas_set_buffer(s_canvas_scene, s_scene_buf, SCENE_W, SCENE_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_scene, SCENE_X, 0);
    /* CLICKED is fine — nav layer handles drag via screen-level events */
    lv_obj_add_flag(s_canvas_scene, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas_scene, water_click_cb, LV_EVENT_CLICKED, NULL);

    /* ── Right info bar ── */
    lv_obj_t *right = lv_obj_create(s_page);
    lv_obj_set_pos(right, SCENE_X + SCENE_W, 0);
    lv_obj_set_size(right, RIGHT_W, DISP_H);
    lv_obj_set_style_bg_color(right, lv_color_hex(0x2A4A1A), 0);
    lv_obj_set_style_border_width(right, 3, 0);
    lv_obj_set_style_border_color(right, lv_color_hex(0x5A3A1A), 0);
    lv_obj_set_style_border_side(right, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_set_style_pad_all(right, 14, 0);
    lv_obj_set_style_pad_top(right, 16, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(right, 8, 0);

    lv_obj_t *r_title = lv_label_create(right);
    lv_label_set_text(r_title, "MY GARDEN");
    lv_obj_set_style_text_color(r_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(r_title, &lv_font_montserrat_14, 0);

    s_label_fps = lv_label_create(right);
    lv_label_set_text(s_label_fps, "FPS --.-  --ms");
    lv_obj_set_style_text_color(s_label_fps, lv_color_hex(0xAADDAA), 0);
    lv_obj_set_style_text_font(s_label_fps, &lv_font_montserrat_14, 0);

    lv_obj_t *sep2 = lv_obj_create(right);
    lv_obj_set_size(sep2, 220, 2);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(0x5A8A3A), 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_radius(sep2, 0, 0);
    lv_obj_set_style_pad_all(sep2, 0, 0);

    lv_obj_t *p_card = lv_obj_create(right);
    lv_obj_set_size(p_card, 230, 60);
    lv_obj_set_style_bg_color(p_card, lv_color_hex(0x1A3A0A), 0);
    lv_obj_set_style_border_width(p_card, 2, 0);
    lv_obj_set_style_border_color(p_card, lv_color_hex(0x4A7A3A), 0);
    lv_obj_set_style_radius(p_card, 4, 0);
    lv_obj_set_style_pad_all(p_card, 8, 0);
    lv_obj_clear_flag(p_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(p_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p_card, 4, 0);

    s_label_level = lv_label_create(p_card);
    lv_label_set_text(s_label_level, "Lv.3 MATURE");
    lv_obj_set_style_text_color(s_label_level, lv_color_hex(0xFFDD44), 0);
    lv_obj_set_style_text_font(s_label_level, &lv_font_montserrat_14, 0);

    lv_obj_t *stage_lbl = lv_label_create(p_card);
    lv_label_set_text(stage_lbl, ". > .. > ... > [*] > ** > ***");
    lv_obj_set_style_text_color(stage_lbl, lv_color_hex(0x88CC88), 0);
    lv_obj_set_style_text_font(stage_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *e_card = lv_obj_create(right);
    lv_obj_set_size(e_card, 230, 65);
    lv_obj_set_style_bg_color(e_card, lv_color_hex(0x1A3A0A), 0);
    lv_obj_set_style_border_width(e_card, 2, 0);
    lv_obj_set_style_border_color(e_card, lv_color_hex(0x4A7A3A), 0);
    lv_obj_set_style_radius(e_card, 4, 0);
    lv_obj_set_style_pad_all(e_card, 8, 0);
    lv_obj_clear_flag(e_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(e_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(e_card, 6, 0);

    lv_obj_t *e_lbl = lv_label_create(e_card);
    lv_label_set_text(e_lbl, "ENERGY  45/100");
    lv_obj_set_style_text_color(e_lbl, lv_color_hex(0xAAFFAA), 0);
    lv_obj_set_style_text_font(e_lbl, &lv_font_montserrat_14, 0);

    s_bar_energy = lv_bar_create(e_card);
    lv_obj_set_size(s_bar_energy, 210, 20);
    lv_bar_set_range(s_bar_energy, 0, 100);
    lv_bar_set_value(s_bar_energy, s_state.energy_x10 / 10, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_energy, lv_color_hex(0x0A1A0A), 0);
    lv_obj_set_style_bg_color(s_bar_energy, lv_color_hex(0x44FF44), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_energy, 3, 0);
    lv_obj_set_style_radius(s_bar_energy, 3, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_bar_energy, 1, 0);
    lv_obj_set_style_border_color(s_bar_energy, lv_color_hex(0x4A7A3A), 0);

    lv_obj_t *s_card = lv_obj_create(right);
    lv_obj_set_size(s_card, 230, 50);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(0x1A3A0A), 0);
    lv_obj_set_style_border_width(s_card, 2, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(0x4A7A3A), 0);
    lv_obj_set_style_radius(s_card, 4, 0);
    lv_obj_set_style_pad_all(s_card, 8, 0);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_card, 2, 0);

    s_label_streak = lv_label_create(s_card);
    lv_label_set_text(s_label_streak, "STREAK: 7 days");
    lv_obj_set_style_text_color(s_label_streak, lv_color_hex(0xFFDD88), 0);
    lv_obj_set_style_text_font(s_label_streak, &lv_font_montserrat_14, 0);

    lv_obj_t *streak_dots = lv_label_create(s_card);
    lv_label_set_text(streak_dots, "[*][*][*][*][*][*][*]");
    lv_obj_set_style_text_color(streak_dots, lv_color_hex(0x44FF88), 0);
    lv_obj_set_style_text_font(streak_dots, &lv_font_montserrat_14, 0);

    /* Water button */
    lv_obj_t *btn = lv_btn_create(right);
    lv_obj_set_size(btn, 230, 56);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x44AAFF), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2288DD), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x2266AA), 0);
    lv_obj_set_style_shadow_width(btn, 4, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x1A3A5A), 0);
    lv_obj_set_style_shadow_offset_y(btn, 3, 0);
    lv_obj_add_event_cb(btn, water_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "~ WATER ~");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);

    lv_obj_t *tip = lv_label_create(right);
    lv_label_set_text(tip, "Water daily\nto grow!");
    lv_obj_set_style_text_color(tip, lv_color_hex(0x88AA88), 0);
    lv_obj_set_style_text_font(tip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(tip, LV_TEXT_ALIGN_CENTER, 0);
}

static void water_click_cb(lv_event_t *e) {
    (void)e;
    garden_page_on_button(0);
}

/* ── Pixel rendering (unchanged from original garden_ui.c) ── */

static inline uint16_t rgb888_to_565(uint32_t c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >>  8) & 0xFF;
    uint8_t b =  c        & 0xFF;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                      uint32_t color, uint8_t alpha) {
    /* ── IDENTICAL to original garden_ui.c fill_rect ── */
    if (alpha == 0 || w <= 0 || h <= 0) return;
    int16_t x1 = x, y1 = y;
    int16_t x2 = (int16_t)(x + w);
    int16_t y2 = (int16_t)(y + h);
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > SCENE_W) x2 = SCENE_W;
    if (y2 > SCENE_H) y2 = SCENE_H;
    if (x1 >= x2 || y1 >= y2) return;

    uint16_t *buf = (uint16_t *)(void *)s_scene_buf;
    uint16_t  c   = rgb888_to_565(color);

    if (alpha == 255) {
        int16_t clipped_w = (int16_t)(x2 - x1);
        for (int16_t row = y1; row < y2; row++) {
            uint16_t *dst = &buf[row * SCENE_W + x1];
            for (int16_t col = 0; col < clipped_w; col++) dst[col] = c;
        }
        return;
    }

    uint8_t fr = (c >> 11) & 0x1F, fg = (c >> 5) & 0x3F, fb = c & 0x1F;
    uint8_t a = alpha, ia = (uint8_t)(255 - a);
    for (int16_t row = y1; row < y2; row++) {
        uint16_t *dst = &buf[row * SCENE_W + x1];
        for (int16_t col = x1; col < x2; col++) {
            uint16_t bg = *dst;
            uint8_t br = (bg >> 11) & 0x1F, bg2 = (bg >> 5) & 0x3F, bb = bg & 0x1F;
            uint8_t nr = (uint8_t)((fr * a + br * ia) / 255);
            uint8_t ng = (uint8_t)((fg * a + bg2 * ia) / 255);
            uint8_t nb = (uint8_t)((fb * a + bb * ia) / 255);
            *dst++ = (uint16_t)((nr << 11) | (ng << 5) | nb);
        }
    }
}

static void draw_background(void) {
    int16_t base_y = (int16_t)(SCENE_H - 60);

    fill_rect(0, 0,           SCENE_W, SCENE_H / 3,     0x5BAFE6, 255);
    fill_rect(0, SCENE_H / 3, SCENE_W, SCENE_H / 3,     0x87CEEB, 255);
    fill_rect(0, SCENE_H * 2 / 3, SCENE_W, SCENE_H / 3, 0xA8DCEF, 255);

    fill_rect(780, 20, 40, 40, 0xFFEE44, 255);
    fill_rect(776, 28, 48, 24, 0xFFEE44, 255);
    fill_rect(788, 16, 24, 48, 0xFFEE44, 255);
    fill_rect(770, 36, 6, 8, 0xFFDD44, 180);
    fill_rect(824, 36, 6, 8, 0xFFDD44, 180);
    fill_rect(796, 10, 8, 6, 0xFFDD44, 180);
    fill_rect(796, 64, 8, 6, 0xFFDD44, 180);

    for (int16_t i = 0; i < SCENE_W; i += 6) {
        int16_t h = (int16_t)(30 + 15 * ((i * 7 + 13) % 11) / 10);
        fill_rect(i, base_y - h - 60, 6, h, 0x6AAA5A, 255);
    }
    fill_rect(50,  base_y - 100, 120, 20, 0x6AAA5A, 255);
    fill_rect(250, base_y - 110, 150, 25, 0x5A9A4A, 255);
    fill_rect(500, base_y - 95,  130, 18, 0x6AAA5A, 255);
    fill_rect(700, base_y - 105, 140, 22, 0x5A9A4A, 255);

    for (int tx = 80; tx < SCENE_W - 50; tx += 140) {
        int16_t th = (int16_t)(40 + (tx * 3) % 20);
        fill_rect(tx, base_y - 60 - th, 12, th, 0x3A7A2A, 255);
        fill_rect(tx - 10, base_y - 60 - th - 10, 32, 20, 0x4A8A3A, 255);
        fill_rect(tx - 6,  base_y - 60 - th - 20, 24, 14, 0x4A8A3A, 255);
    }

    fill_rect(0, base_y, SCENE_W, 8, 0x5A8A3A, 255);
    fill_rect(0, base_y + 8, SCENE_W, SCENE_H - base_y - 8, 0x8B6914, 255);
    for (int16_t dx = 10; dx < SCENE_W; dx += 30) {
        int16_t dy = (int16_t)(base_y + 12 + (dx * 7) % 20);
        fill_rect(dx, dy, 8, 4, 0x7A5A0A, 255);
    }
    for (int16_t gx = 5; gx < SCENE_W; gx += 18) {
        int16_t gh = (int16_t)(6 + (gx * 3) % 8);
        fill_rect(gx, base_y - gh, 4, gh, 0x4A9A2A, 255);
        fill_rect(gx + 5, base_y - gh + 2, 3, gh - 2, 0x5AAA3A, 255);
    }

    for (int16_t fx = 30; fx < 200; fx += 24) {
        fill_rect(fx, base_y - 30, 6, 30, 0xAA8844, 255);
        fill_rect(fx, base_y - 28, 8, 4, 0xBB9955, 255);
    }
    fill_rect(30, base_y - 20, 170, 4, 0x997733, 255);
    fill_rect(30, base_y - 10, 170, 4, 0x997733, 255);
    for (int16_t fx = 720; fx < SCENE_W - 20; fx += 24) {
        fill_rect(fx, base_y - 30, 6, 30, 0xAA8844, 255);
        fill_rect(fx, base_y - 28, 8, 4, 0xBB9955, 255);
    }
    fill_rect(720, base_y - 20, SCENE_W - 740, 4, 0x997733, 255);
    fill_rect(720, base_y - 10, SCENE_W - 740, 4, 0x997733, 255);

    fill_rect(240, base_y - 16, 4, 16, 0x5A9A2A, 255);
    fill_rect(236, base_y - 22, 12, 8, 0xFF6688, 255);
    fill_rect(238, base_y - 24, 8, 4, 0xFFAACC, 255);
    fill_rect(280, base_y - 12, 4, 12, 0x5A9A2A, 255);
    fill_rect(276, base_y - 18, 12, 8, 0xFFAA44, 255);
    fill_rect(278, base_y - 20, 8, 4, 0xFFCC66, 255);
    fill_rect(320, base_y - 20, 4, 20, 0x5A9A2A, 255);
    fill_rect(316, base_y - 26, 12, 8, 0xAA44FF, 255);
    fill_rect(318, base_y - 28, 8, 4, 0xCC88FF, 255);
    fill_rect(600, base_y - 14, 4, 14, 0x5A9A2A, 255);
    fill_rect(596, base_y - 20, 12, 8, 0xFF4466, 255);
    fill_rect(598, base_y - 22, 8, 4, 0xFF8899, 255);
    fill_rect(650, base_y - 18, 4, 18, 0x5A9A2A, 255);
    fill_rect(646, base_y - 24, 12, 8, 0x44AAFF, 255);
    fill_rect(648, base_y - 26, 8, 4, 0x88CCFF, 255);
    fill_rect(690, base_y - 10, 4, 10, 0x5A9A2A, 255);
    fill_rect(686, base_y - 16, 12, 8, 0xFFDD44, 255);
    fill_rect(688, base_y - 18, 8, 4, 0xFFEE88, 255);

    fill_rect(160, base_y - 10, 4, 10, 0xEEDDCC, 255);
    fill_rect(155, base_y - 16, 14, 8, 0xFF4444, 255);
    fill_rect(157, base_y - 18, 4, 4, 0xFFFFFF, 255);
    fill_rect(163, base_y - 16, 4, 4, 0xFFFFFF, 255);
    fill_rect(175, base_y - 8, 3, 8, 0xEEDDCC, 255);
    fill_rect(172, base_y - 12, 10, 6, 0xFF6644, 255);
    fill_rect(174, base_y - 13, 3, 3, 0xFFFFFF, 255);

    fill_rect(400, base_y + 2, 12, 8, 0x999999, 255);
    fill_rect(402, base_y,     8, 4, 0xAAAAAA, 255);
    fill_rect(500, base_y + 4, 10, 6, 0x888888, 255);
    fill_rect(350, base_y + 6, 8, 5, 0x777777, 255);
    fill_rect(380, base_y + 2, 140, 6, 0x9A7A2A, 255);
    fill_rect(385, base_y + 1, 130, 2, 0xAA8A3A, 255);

    memcpy(s_bg_buf, s_scene_buf, SCENE_BUF_SIZE);
    s_bg_ready = true;
}

static void draw_scene(void) {
    int64_t draw_start_us = esp_timer_get_time();
    int16_t base_y = (int16_t)(SCENE_H - 60);

    if (!s_bg_ready) draw_background();
    memcpy(s_scene_buf, s_bg_buf, SCENE_BUF_SIZE);

    /* ── Clouds ── */
    fill_rect(s_cloud1_x,      30, 36, 14, 0xFFFFFF, 230);
    fill_rect(s_cloud1_x +  8, 20, 24, 18, 0xFFFFFF, 230);
    fill_rect(s_cloud1_x + 28, 26, 18, 12, 0xFFFFFF, 230);
    fill_rect(s_cloud1_x + 4,  38, 28, 6,  0xEEEEFF, 180);
    fill_rect(s_cloud2_x,      55, 44, 14, 0xFFFFFF, 230);
    fill_rect(s_cloud2_x + 12, 44, 28, 16, 0xFFFFFF, 230);
    fill_rect(s_cloud2_x + 6,  63, 32, 6,  0xEEEEFF, 180);
    int16_t c3x = (int16_t)((s_cloud1_x + 500) % (SCENE_W + 80) - 40);
    fill_rect(c3x, 80, 30, 10, 0xFFFFFF, 200);
    fill_rect(c3x + 6, 72, 20, 14, 0xFFFFFF, 200);

    /* ── Plant (stages 0-5, identical to original) ── */
    int8_t  sway[]  = {-4, -1, 4, 1};
    int16_t root_x  = (int16_t)(SCENE_W / 2);
    int16_t sway_px = sway[s_state.plant_frame];
    int16_t sx = (int16_t)(root_x + sway_px);

    fill_rect(root_x - 22, base_y - 4, 44, 6, 0x7A5A0A, 255);
    fill_rect(root_x - 18, base_y - 8, 36, 6, 0x6A4A0A, 255);
    fill_rect(root_x - 14, base_y - 10, 28, 4, 0x5A3A0A, 255);

    switch (s_state.plant_stage) {
    case 0:
        fill_rect(root_x - 4, base_y - 14, 8, 6, 0x8B6914, 255);
        fill_rect(root_x - 2, base_y - 16, 4, 4, 0xAA8833, 255);
        break;
    case 1:
        fill_rect(root_x - 2, base_y - 24, 4, 16, 0x5A9A2A, 255);
        fill_rect(root_x - 8, base_y - 28, 8, 6, 0x44CC44, 255);
        fill_rect(root_x + 2, base_y - 30, 6, 6, 0x55DD55, 255);
        break;
    case 2:
        fill_rect(root_x - 3, base_y - 50, 6, 42, 0x4A8A1A, 255);
        fill_rect(root_x - 3 + sway_px / 4, base_y - 55, 6, 10, 0x5A9A2A, 255);
        fill_rect(root_x - 20 + sway_px / 3, base_y - 45, 18, 8, 0x44CC44, 255);
        fill_rect(root_x + 4 + sway_px / 3, base_y - 40, 18, 8, 0x44CC44, 255);
        fill_rect(root_x - 4 + sway_px / 2, base_y - 62, 8, 8, 0x88DD88, 255);
        break;
    case 3:
        fill_rect(root_x - 4, base_y - 40, 8, 30, 0x4A8A1A, 255);
        fill_rect(root_x - 4 + sway_px / 4, base_y - 70, 8, 30, 0x5A9A2A, 255);
        fill_rect(root_x - 4 + sway_px / 2, base_y - 100, 8, 30, 0x5A9A2A, 255);
        fill_rect(root_x - 3 + sway_px * 3 / 4, base_y - 120, 6, 20, 0x6AAA2A, 255);
        fill_rect(sx - 36, base_y - 85, 32, 14, 0x44CC44, 255);
        fill_rect(sx - 36, base_y - 85, 12, 10, 0x228822, 255);
        fill_rect(sx + 6,  base_y - 70, 32, 14, 0x44CC44, 255);
        fill_rect(sx + 26, base_y - 70, 12, 10, 0x228822, 255);
        fill_rect(sx - 28, base_y - 105, 24, 10, 0x55DD55, 255);
        fill_rect(sx + 10, base_y - 100, 20, 8, 0x55DD55, 255);
        fill_rect(sx - 8, base_y - 135, 16, 16, 0x88CC88, 255);
        fill_rect(sx - 4, base_y - 138, 8, 6, 0xAAEEAA, 255);
        break;
    case 4:
        fill_rect(root_x - 4, base_y - 40, 8, 30, 0x4A8A1A, 255);
        fill_rect(root_x - 4 + sway_px / 4, base_y - 70, 8, 30, 0x5A9A2A, 255);
        fill_rect(root_x - 4 + sway_px / 2, base_y - 100, 8, 30, 0x5A9A2A, 255);
        fill_rect(root_x - 3 + sway_px * 3 / 4, base_y - 125, 6, 25, 0x6AAA2A, 255);
        fill_rect(sx - 36, base_y - 85, 32, 14, 0x44CC44, 255);
        fill_rect(sx - 36, base_y - 85, 12, 10, 0x228822, 255);
        fill_rect(sx + 6,  base_y - 70, 32, 14, 0x44CC44, 255);
        fill_rect(sx + 26, base_y - 70, 12, 10, 0x228822, 255);
        fill_rect(sx - 28, base_y - 105, 24, 10, 0x55DD55, 255);
        fill_rect(sx - 26, base_y - 155, 18, 18, 0xFF88AA, 255);
        fill_rect(sx + 8,  base_y - 155, 18, 18, 0xFF88AA, 255);
        fill_rect(sx - 10, base_y - 170, 18, 18, 0xFF99BB, 255);
        fill_rect(sx - 10, base_y - 140, 18, 18, 0xFF77AA, 255);
        fill_rect(sx - 26, base_y - 170, 14, 14, 0xFF6699, 200);
        fill_rect(sx + 12, base_y - 170, 14, 14, 0xFF6699, 200);
        fill_rect(sx - 16, base_y - 160, 32, 32, 0xFFDD44, 255);
        fill_rect(sx - 10, base_y - 154, 20, 20, 0xFFAA22, 255);
        fill_rect(sx - 6,  base_y - 150, 12, 12, 0xFF8800, 255);
        break;
    case 5:
        fill_rect(root_x - 4, base_y - 40, 8, 30, 0x4A8A1A, 255);
        fill_rect(root_x - 4 + sway_px / 4, base_y - 70, 8, 30, 0x5A9A2A, 255);
        fill_rect(root_x - 3 + sway_px / 2, base_y - 95, 6, 25, 0x8AAA6A, 255);
        fill_rect(root_x - 2 + sway_px * 3 / 4, base_y - 120, 4, 25, 0xAA9955, 255);
        fill_rect(sx - 14, base_y - 140, 28, 28, 0xFFFFFF, 180);
        fill_rect(sx - 10, base_y - 144, 20, 20, 0xFFFFFF, 220);
        fill_rect(sx - 6,  base_y - 148, 12, 12, 0xFFFFFF, 255);
        fill_rect(sx - 18, base_y - 132, 4, 4, 0xEEEEEE, 200);
        fill_rect(sx + 14, base_y - 136, 4, 4, 0xEEEEEE, 200);
        fill_rect(sx - 2,  base_y - 152, 4, 4, 0xEEEEEE, 200);
        fill_rect(sx + 8,  base_y - 148, 4, 4, 0xEEEEEE, 200);
        fill_rect(sx - 12, base_y - 146, 4, 4, 0xEEEEEE, 200);
        {
            int8_t f = s_state.plant_frame;
            int16_t seed_offsets[][2] = {
                {-30 - f*2, -150 + f}, {20 + f*3, -160 - f}, {-40 + f, -140 - f*2},
                {35 - f, -155 + f*2}, {-20 - f*3, -165 + f}, {45 + f*2, -145 - f}
            };
            for (int s = 0; s < 6; s++) {
                int16_t seed_x = (int16_t)(sx + seed_offsets[s][0]);
                int16_t seed_y = (int16_t)(base_y + seed_offsets[s][1]);
                fill_rect(seed_x, seed_y, 3, 3, 0xFFFFFF, (uint8_t)(180 - s * 20));
                fill_rect(seed_x + 1, seed_y - 3, 1, 3, 0xDDDDDD, (uint8_t)(150 - s * 15));
            }
        }
        break;
    }

    /* ── Butterfly ── */
    s_butterfly_frame = (uint8_t)((s_butterfly_frame + 1) % 4);
    int8_t wing_off = (s_butterfly_frame < 2) ? 0 : 3;
    fill_rect(s_butterfly_x - 6, s_butterfly_y - wing_off, 6, 8 - wing_off, 0xFF88FF, 220);
    fill_rect(s_butterfly_x + 2, s_butterfly_y - wing_off, 6, 8 - wing_off, 0xFFAA88, 220);
    fill_rect(s_butterfly_x,     s_butterfly_y, 2, 6, 0x333333, 255);
    s_butterfly_x = (int16_t)(s_butterfly_x + s_butterfly_dx);
    s_butterfly_y = (int16_t)(s_butterfly_y + s_butterfly_dy);
    if (s_butterfly_x < 220 || s_butterfly_x > 680) s_butterfly_dx = (int8_t)(-s_butterfly_dx);
    if (s_butterfly_y < 120 || s_butterfly_y > base_y - 40) s_butterfly_dy = (int8_t)(-s_butterfly_dy);

    /* ── Bird ── */
    s_bird_x = (int16_t)(s_bird_x + 3);
    if (s_bird_x > SCENE_W + 30) { s_bird_x = -30; s_bird_y = (int16_t)(40 + (s_bird_y * 7) % 60); }
    fill_rect(s_bird_x, s_bird_y, 4, 2, 0x333333, 255);
    fill_rect(s_bird_x - 6, s_bird_y - 2, 6, 2, 0x333333, 255);
    fill_rect(s_bird_x + 4, s_bird_y - 2, 6, 2, 0x333333, 255);

    /* ── Particles ── */
    for (int i = 0; i < 8; i++) {
        if (!s_state.particles[i].active) continue;
        fill_rect(s_state.particles[i].x, s_state.particles[i].y,
                  6, 6, 0x44FF88, s_state.particles[i].alpha);
        fill_rect(s_state.particles[i].x - 2, s_state.particles[i].y - 2,
                  10, 10, 0x88FFAA, (uint8_t)(s_state.particles[i].alpha / 4));
    }

    /* ── Water drops ── */
    for (int i = 0; i < 12; i++) {
        if (!s_state.drops[i].active) continue;
        fill_rect(s_state.drops[i].x, s_state.drops[i].y, 3, 8, 0x44AAFF, 200);
        fill_rect(s_state.drops[i].x, s_state.drops[i].y, 2, 4, 0x88CCFF, 255);
    }

    /* ── Wet soil ── */
    if (s_state.wet_timer > 0) {
        uint8_t wet_alpha = (uint8_t)(s_state.wet_timer > 1000 ? 80 : s_state.wet_timer * 80 / 1000);
        fill_rect(root_x - 40, base_y + 2, 80, 12, 0x3A2A00, wet_alpha);
        fill_rect(root_x - 30, base_y - 2, 60, 6, 0x4A3A0A, (uint8_t)(wet_alpha / 2));
    }

    /* ── Plant shadow ── */
    {
        int8_t  sw[] = {-4, -1, 4, 1};
        int16_t shadow_off = (int16_t)(sw[s_state.plant_frame] * 2);
        fill_rect(root_x - 20 + shadow_off, base_y + 4, 50, 6, 0x000000, 30);
        fill_rect(root_x - 14 + shadow_off, base_y + 2, 38, 4, 0x000000, 20);
    }

    /* ── Foreground grass ── */
    fill_rect(0,   base_y - 4, 16, 14, 0x3A8A1A, 255);
    fill_rect(4,   base_y - 10, 8, 10, 0x4AAA2A, 255);
    fill_rect(12,  base_y - 6, 10, 10, 0x3A8A1A, 255);
    fill_rect(SCENE_W - 20, base_y - 6, 20, 14, 0x3A8A1A, 255);
    fill_rect(SCENE_W - 16, base_y - 12, 10, 12, 0x4AAA2A, 255);
    fill_rect(SCENE_W - 8,  base_y - 8, 8, 12, 0x3A8A1A, 255);
    fill_rect(20, base_y - 14, 4, 14, 0x4A9A2A, 255);
    fill_rect(16, base_y - 20, 12, 8, 0xFF5577, 255);
    fill_rect(18, base_y - 22, 8, 4, 0xFF99AA, 255);
    fill_rect(SCENE_W - 35, base_y - 16, 4, 16, 0x4A9A2A, 255);
    fill_rect(SCENE_W - 39, base_y - 22, 12, 8, 0xFFAA33, 255);
    fill_rect(SCENE_W - 37, base_y - 24, 8, 4, 0xFFCC66, 255);

    /* ── Evolution flash ── */
    if (s_state.evolve_timer > 0) {
        uint8_t flash = (uint8_t)(s_state.evolve_timer > 400
                        ? (s_state.evolve_timer - 400) * 120 / 400
                        : s_state.evolve_timer * 120 / 400);
        fill_rect(root_x - 50, base_y - 175, 100, 5, 0xFFFFFF, (uint8_t)(flash * 2 / 3));
        fill_rect(root_x - 55, base_y - 170, 110, 150, 0xFFFFFF, flash);
        fill_rect(root_x - 50, base_y - 20, 100, 5, 0xFFFFFF, (uint8_t)(flash * 2 / 3));
        fill_rect(root_x - 30, base_y - 150, 60, 110, 0xFFFFFF, (uint8_t)(flash * 3 / 4));
    }

    /* ── Energy bar full glow ── */
    if (s_state.energy_x10 >= 1000) {
        uint8_t pulse = (uint8_t)(80 + (s_state.plant_frame * 30));
        fill_rect(root_x - 30, base_y - 180, 60, 4, 0xFFDD44, pulse);
        fill_rect(root_x - 20, base_y - 185, 40, 3, 0xFFDD44, (uint8_t)(pulse / 2));
    }

    lv_obj_invalidate(s_canvas_scene);
    s_state.last_draw_us = (uint32_t)(esp_timer_get_time() - draw_start_us);
}
```

---

### Task 7: Update main.c — use nav layer + add long-press detection

**Files:**
- Modify: `lvgl9.5-demo/main/main.c`

- [ ] **Step 1: Update includes — line 31**

Change `#include "garden_ui.h"` to:
```c
#include "garden_nav.h"
#include "garden_page.h"
#include "garden_focus.h"
```

- [ ] **Step 2: Replace garden_ui_init call — lines 385-390**

Replace the block:
```c
  // 🌱 Garden UI Demo
  ESP_LOGI(TAG, "🌱 启动 Garden UI Demo...");
  if (lvgl_port_lock(0)) {
    garden_ui_init();
    lvgl_port_unlock();
  }
```
With:
```c
  // 🌱 Multi-page navigation init
  ESP_LOGI(TAG, "🌱 初始化多页面导航...");
  if (lvgl_port_lock(0)) {
    /* Init navigation hub */
    garden_nav_init(lv_screen_active());

    /* Register pages: index 0=leftmost, 1=HOME */
    garden_page_def_t page_focus  = { "focus",  garden_focus_create,  garden_focus_destroy,  garden_focus_tick,  garden_focus_on_button,  garden_focus_set_active };
    garden_page_def_t page_garden = { "garden", garden_page_create,   garden_page_destroy,   garden_page_tick,   garden_page_on_button,   garden_page_set_active };
    /* Placeholders registered in later tasks */

    garden_nav_register(0, &page_focus);
    garden_nav_register(1, &page_garden);

    /* Wire cross-page callback: focus done → garden energy */
    garden_focus_set_done_cb(garden_page_add_energy);

    lvgl_port_unlock();
  }
```

- [ ] **Step 3: Replace main loop tick — lines 415-423**

Replace the `garden_ui_tick(elapsed_ms)` call in the main loop with:
```c
      garden_nav_tick(elapsed_ms);
```

- [ ] **Step 4: Add long-press detection in button handler — lines 396-412**

Replace the button detection block with long-press support:

```c
    // 🔘 按钮检测 (低电平使能, 带消抖 + 长按)
    bool btn_now = gpio_get_level(BUTTON_GPIO);
    if (btn_last == true && btn_now == false) {
      // ⬇️ 下降沿 — 开始计时
      vTaskDelay(pdMS_TO_TICKS(30)); // 消抖
      if (gpio_get_level(BUTTON_GPIO) == 0) {
        uint32_t press_ms = 0;
        bool long_press = false;
        while (gpio_get_level(BUTTON_GPIO) == 0) {
          vTaskDelay(pdMS_TO_TICKS(50));
          press_ms += 50;
          if (press_ms >= 500) {
            long_press = true;
            break;
          }
        }
        if (lvgl_port_lock(0)) {
          garden_nav_button(long_press ? 1 : 0);
          lvgl_port_unlock();
        }
        /* 如果是长按, 等待松开 */
        if (long_press) {
          while (gpio_get_level(BUTTON_GPIO) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
          }
        }
      }
    }
    btn_last = btn_now;
```

---

### Task 8: Update CMakeLists.txt

**Files:**
- Modify: `lvgl9.5-demo/main/CMakeLists.txt`

- [ ] **Step 1: Replace source file list**

Replace line 2:
```
idf_component_register(SRCS "main.c" "garden_ui.c" "garden_focus.c"
```
With:
```
idf_component_register(SRCS "main.c" "garden_nav.c" "garden_page.c" "garden_focus.c"
                    "garden_ai.c" "garden_device.c" "garden_album.c"
```

Note: The placeholder .c files don't exist yet but will be created in Phase 2. Adding them now avoids a second CMakeLists edit.

---

### Task 9: Create placeholder page modules

**Files:**
- Create: `lvgl9.5-demo/main/garden_ai.h`, `garden_ai.c`
- Create: `lvgl9.5-demo/main/garden_device.h`, `garden_device.c`
- Create: `lvgl9.5-demo/main/garden_album.h`, `garden_album.c`

- [ ] **Step 1: Write garden_ai.h**

```c
#ifndef GARDEN_AI_H
#define GARDEN_AI_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

lv_obj_t * garden_ai_create(lv_obj_t *parent);
void       garden_ai_destroy(lv_obj_t *page);
void       garden_ai_tick(uint32_t elapsed_ms);
bool       garden_ai_on_button(uint8_t type);
void       garden_ai_set_active(bool active);

#endif
```

- [ ] **Step 2: Write garden_ai.c**

```c
#include "garden_ai.h"

lv_obj_t * garden_ai_create(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, 1280, 452);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x1A2A3A), 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(page);
    lv_label_set_text(label, "AI Chat\n\nComing soon...");
    lv_obj_set_style_text_color(label, lv_color_hex(0xAADDAA), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    return page;
}

void garden_ai_destroy(lv_obj_t *page) {
    if (page) lv_obj_delete(page);
}

void garden_ai_tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
}

bool garden_ai_on_button(uint8_t type) {
    (void)type;
    return false;
}

void garden_ai_set_active(bool active) {
    (void)active;
}
```

- [ ] **Step 3: Write garden_device.h and garden_device.c** (same pattern as AI, different title: "Device Center")

garden_device.h — identical structure to garden_ai.h, renamed symbols.

garden_device.c — same as garden_ai.c but:
- bg color: `0x1A2A2A`
- label text: `"Device Center\n\nComing soon..."`
- label color: `0xAADDDD`

- [ ] **Step 4: Write garden_album.h and garden_album.c** (same pattern, title: "Album")

garden_album.h — identical structure.

garden_album.c — same as garden_ai.c but:
- bg color: `0x2A1A2A`
- label text: `"Album\n\nComing soon..."`
- label color: `0xDDAADD`

---

### Task 10: Register all pages in main.c

**Files:**
- Modify: `lvgl9.5-demo/main/main.c`

- [ ] **Step 1: Add includes for placeholder pages**

Add after the existing page includes:
```c
#include "garden_ai.h"
#include "garden_device.h"
#include "garden_album.h"
```

- [ ] **Step 2: Add page registrations**

Add after `garden_nav_register(1, &page_garden);`:
```c
    garden_page_def_t page_ai     = { "ai",     garden_ai_create,     garden_ai_destroy,     garden_ai_tick,     garden_ai_on_button,     garden_ai_set_active };
    garden_page_def_t page_device = { "device", garden_device_create, garden_device_destroy, garden_device_tick, garden_device_on_button, garden_device_set_active };
    garden_page_def_t page_album  = { "album",  garden_album_create,  garden_album_destroy,  garden_album_tick,  garden_album_on_button,  garden_album_set_active };

    garden_nav_register(2, &page_ai);
    garden_nav_register(3, &page_device);
    garden_nav_register(4, &page_album);
```

---

### Task 11: Transition polish — fix click vs drag conflict

**Files:**
- Modify: `lvgl9.5-demo/main/garden_nav.c`

The nav layer's screen-level PRESSING handler may interfere with CLICKED events on child widgets (like the garden canvas water click and focus start button).

- [ ] **Step 1: In nav_drag_cb, only engage drag after deadzone exceeded. Also, track whether we dragged so CLICKED handlers can check.**

The current `DRAG_DEADZONE` (10px) already provides basic protection. CLICKED events are only suppressed if the drag actually started. With a 10px deadzone, short taps (<10px movement) still fire CLICKED.

This is already correct in the garden_nav.c code above. No additional changes needed — the deadzone ensures taps still work.

- [ ] **Step 2: Verify the garden canvas CLICKED event for watering still fires**

The canvas has `LV_EVENT_CLICKED` — this fires when PRESSED + RELEASED happen without significant movement. Since the nav layer only takes over after DRAG_DEADZONE (10px), taps under 10px movement will still trigger canvas CLICKED.

This means watering via touch still works. GPIO47 button watering also works via `garden_nav_button(0)`.

---

### Task 12: Commit all changes

- [ ] **Step 1: Stage and commit**

```bash
git -C "Z:/ai-desktop-energy--garden" add lvgl9.5-demo/main/garden_nav.c lvgl9.5-demo/main/garden_nav.h
git -C "Z:/ai-desktop-energy--garden" add lvgl9.5-demo/main/garden_page.c lvgl9.5-demo/main/garden_page.h
git -C "Z:/ai-desktop-energy--garden" add lvgl9.5-demo/main/garden_ai.c lvgl9.5-demo/main/garden_ai.h
git -C "Z:/ai-desktop-energy--garden" add lvgl9.5-demo/main/garden_device.c lvgl9.5-demo/main/garden_device.h
git -C "Z:/ai-desktop-energy--garden" add lvgl9.5-demo/main/garden_album.c lvgl9.5-demo/main/garden_album.h
git -C "Z:/ai-desktop-energy--garden" add -u lvgl9.5-demo/main/
git -C "Z:/ai-desktop-energy--garden" commit -m "feat: refactor to multi-page navigation architecture

Add garden_nav hub for N-page carousel with coordinated drag+snap.
Each page implements 4-fn contract (create/destroy/tick/on_button).
Add 3 placeholder pages (ai/device/album) for future development.
Add GPIO47 long-press detection for global HOME navigation.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Pre-implementation notes

1. **File renames**: `garden_ui.c` → `garden_page.c` and `garden_ui.h` → `garden_page.h`. Use `git rm` for old names, write new files.
2. **Compilation**: User compiles on their own. After all changes, remind them to run `idf.py build` in `lvgl9.5-demo/`.
3. **No screen init changes**: LCD, MIPI, touch, backlight init in main.c remain untouched.
4. **LVGL font**: Only `lv_font_montserrat_14` used — no new fonts added.
5. **PPA / sysmon**: Not enabled — per project constraints.
