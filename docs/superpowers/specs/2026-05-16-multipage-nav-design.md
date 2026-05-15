# Multi-Page Navigation Architecture

## Goal

Refactor the current 2-page (garden + focus) hardcoded UI into a clean N-page navigation system where each page is an independent module.

## Principles

1. **分层解耦**: Navigation ≠ Page content. A page module doesn't know about other pages.
2. **模块契约**: Every page follows the same 4-function contract. Adding a page = create a .c/.h pair + register it.
3. **可移植**: Platform-specific code stays in main.c. All page modules are pure LVGL — they run on any LVGL platform (ESP32, PC simulator, etc.).

## Architecture

```
main.c                          ← Platform: display, touch, GPIO, LVGL port
  └─ garden_nav.c               ← Navigation: page registry, swipe, snap animation
       ├─ garden_page.c/.h      ← Page 1 (HOME): garden scene
       ├─ garden_focus.c/.h     ← Page 0: pomodoro timer
       ├─ garden_ai.c/.h        ← Page 2: AI chat (placeholder)
       ├─ garden_device.c/.h    ← Page 3: device center (placeholder)
       └─ garden_album.c/.h     ← Page 4: collection album (placeholder)
```

### Layer responsibilities

| Layer | Does | Does NOT |
|-------|------|----------|
| main.c | HW init, LVGL port, GPIO polling, 20ms loop | Any UI logic |
| garden_nav.c | Page register, swipe/drag, snap animation, tick dispatch | Any page content |
| garden_page.c | Garden canvas, plant drawing, watering | Other pages, nav logic |
| garden_focus.c | Timer countdown, start/pause | Other pages, nav logic |

## Page contract

Every page module exposes one struct:

```c
typedef struct {
    const char *name;
    lv_obj_t *  (*create)(lv_obj_t *parent);   // return fullscreen page obj
    void        (*destroy)(lv_obj_t *page);
    void        (*tick)(uint32_t elapsed_ms);   // called ~50Hz, skip heavy work when inactive
    bool        (*on_button)(uint8_t type);     // 0=short, 1=long; return true if handled
} garden_page_def_t;
```

That's it. A developer creating a new page just:
1. Copy an existing page module as template
2. Implement the 4 functions
3. Call `garden_nav_register(index, &my_page_def)` in `garden_nav_init()`

## Page layout (horizontal carousel)

Pages are arranged left-to-right by index. Garden is HOME (index 1, center).

```
[0:FOCUS]  [1:GARDEN]  [2:AI]  [3:DEVICE]  [4:ALBUM]
    ↑           ↑
  index 0    HOME (startup page)
```

- Swipe left → next page (higher index)
- Swipe right → previous page (lower index)
- GPIO47 short press → current page's `on_button(0)`
- GPIO47 long press → `garden_nav_go_home()` from anywhere

## Transition (drag + snap)

During drag: the current page and its neighbor slide together as one continuous surface. All other pages stay hidden.

On release:
- Dragged > 20% of screen width → snap to new page
- Otherwise → spring back to current page

Animation: LVGL `lv_anim` with ease_out, ~200ms, both pages animated in sync.

## Input routing

```
main.c
  ├─ touch swipe/drag  → garden_nav (handle drag/snap)
  ├─ touch click       → current page (content interaction)
  ├─ GPIO47 short      → current page on_button(0)
  └─ GPIO47 long       → garden_nav_go_home()
```

Encoder interface reserved (`garden_nav_encoder`, `garden_nav_encoder_press`) but not wired — no hardware available.

## Performance

- During drag: garden canvas redraw paused (page tick detects inactive state)
- Non-neighbor pages: `LV_OBJ_FLAG_HIDDEN` → skipped by LVGL renderer
- Placeholder pages: minimal content (title label only), near-zero overhead during drag
- Canvas stays as-is (RGB565 900×452 with background cache + memcpy per frame) — no change to drawing pipeline

## File changes

### New files
- `garden_nav.c` / `garden_nav.h` — navigation hub (~150 lines)
- `garden_ai.c` / `garden_ai.h` — placeholder page (~20 lines)
- `garden_device.c` / `garden_device.h` — placeholder page (~20 lines)  
- `garden_album.c` / `garden_album.h` — placeholder page (~20 lines)

### Modified files
- `garden_ui.c` → renamed to `garden_page.c` — garden as a page module; remove nav logic
- `garden_ui.h` → renamed to `garden_page.h` — simplified, no more global page-switching API
- `garden_focus.c` — adapt init to create pattern, add `on_button` handler
- `garden_focus.h` — expose create/destroy for page_def_t
- `main.c` — use `garden_nav_init`, add long-press detection, route events through nav
- `CMakeLists.txt` — add new source files

### Removed
- `PAGE_GARDEN` / `PAGE_FOCUS` enum
- `set_page()`, `page_drag_cb()`, `page_gesture_cb()` from garden_ui
- `garden_ui_encoder_event()`, `garden_ui_button_event()` global API

## Non-goals

- PPA hardware acceleration (helps rotation but not widget repositioning — the transition bottleneck)
- Page thumbnail/snapshot caching (over-engineering for current needs)
- Encoder full implementation (no hardware)
- fancy transition effects (crossfade, parallax) — keep it simple: horizontal slide

## Implementation order

1. Extract navigation: create `garden_nav.c`, adapt `garden_ui` → `garden_page`, adapt `garden_focus`
2. Add placeholders: `garden_ai`, `garden_device`, `garden_album`
3. Optimize transitions: coordinated slide + snap tuning
