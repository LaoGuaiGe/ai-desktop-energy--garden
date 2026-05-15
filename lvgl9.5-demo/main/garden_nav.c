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

    /* Screen-level styling (was in old build_layout, required for correct rendering) */
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1A3A1A), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

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
