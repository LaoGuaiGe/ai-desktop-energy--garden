#include "garden_focus.h"
#include <stdio.h>
#include <string.h>

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

static void update_labels(void);
static void garden_focus_start_pause(void);
static void garden_focus_reset(void);
static void start_pause_cb(lv_event_t *e);
static void reset_cb(lv_event_t *e);

/* ── Page contract ── */

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

/* ── Internal helpers ── */

static void update_labels(void) {
    uint32_t remaining_s = s_focus.remaining_ms / 1000U;
    uint32_t minutes = remaining_s / 60U;
    uint32_t seconds = remaining_s % 60U;
    char buf[32];

    if (s_focus.time_label) {
        snprintf(buf, sizeof(buf), "%02lu:%02lu",
                 (unsigned long)minutes, (unsigned long)seconds);
        lv_label_set_text(s_focus.time_label, buf);
    }
    if (s_focus.state_label) {
        lv_label_set_text(s_focus.state_label,
                          s_focus.remaining_ms == 0 ? "DONE" :
                          s_focus.running ? "FOCUSING" : "PAUSED");
    }
    if (s_focus.button_label) {
        lv_label_set_text(s_focus.button_label, s_focus.running ? "PAUSE" : "START");
        lv_obj_center(s_focus.button_label);
    }
    if (s_focus.progress) {
        uint32_t done = FOCUS_TOTAL_MS - s_focus.remaining_ms;
        lv_bar_set_value(s_focus.progress, (int32_t)(done * 100U / FOCUS_TOTAL_MS), LV_ANIM_OFF);
    }
}

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

static void start_pause_cb(lv_event_t *e) {
    (void)e;
    garden_focus_start_pause();
}

static void reset_cb(lv_event_t *e) {
    (void)e;
    garden_focus_reset();
}
