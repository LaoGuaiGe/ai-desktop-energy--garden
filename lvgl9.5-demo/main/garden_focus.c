#include "garden_focus.h"
#include "garden_nav.h"
#include <stdio.h>
#include <string.h>

#define DISP_W         1280
#define DISP_H         452
#define TOP_H          48
#define BOTTOM_H       60
#define DEFAULT_MINUTES 25U
#define MIN_MINUTES     1U
#define MAX_MINUTES     60U
#define FLOWER_COUNT    18
#define MINUTE_MS       (60U * 1000U)

typedef struct {
    lv_obj_t *page;
    lv_obj_t *time_px[6][7][5];
    lv_obj_t *time_colon[2][2];
    lv_obj_t *status_icon[4][4];
    lv_obj_t *tomato_count_label;
    lv_obj_t *state_label;
    lv_obj_t *button_label;
    lv_obj_t *bottom_hint_label;
    lv_obj_t *time_set_label;
    lv_obj_t *time_slider;
    lv_obj_t *harvest_halo;
    lv_obj_t *harvest_tomato;
    lv_obj_t *flying_tomato;
    lv_obj_t *flowers[FLOWER_COUNT];
    garden_focus_done_cb_t done_cb;
    uint32_t selected_minutes;
    uint32_t total_ms;
    uint32_t remaining_ms;
    uint32_t harvested_minutes;
    uint32_t harvest_pending;
    bool running;
    bool harvest_ready;
    bool harvesting;
} garden_focus_t;

static garden_focus_t s_focus;

static void update_labels(void);
static void build_pixel_time(lv_obj_t *parent);
static void set_pixel_digit(int index, uint8_t value);
static void set_pixel_style(lv_obj_t *px, bool on);
static lv_obj_t *create_hud_panel(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg);
static lv_obj_t *create_hud_label(lv_obj_t *parent, const char *text, lv_color_t color);
static void draw_cell(lv_obj_t *parent, int x, int y, int size, lv_color_t color, lv_opa_t opa);
static void draw_bitmap(lv_obj_t *parent, int x, int y, int size, const uint8_t *rows, int row_count, lv_color_t color);
static void draw_tomato_icon(lv_obj_t *parent, int x, int y);
static lv_obj_t *create_flying_tomato(lv_obj_t *parent);
static lv_obj_t *create_harvest_halo(lv_obj_t *parent);
static lv_obj_t *create_harvest_tomato(lv_obj_t *parent);
static void show_harvest_ready(void);
static void hide_harvest_ready(void);
static void harvest_ready_cb(lv_event_t *e);
static void tomato_scale_anim_cb(void *obj, int32_t value);
static void halo_opa_anim_cb(void *obj, int32_t value);
static void halo_size_anim_cb(void *obj, int32_t value);
static void draw_energy_icon(lv_obj_t *parent, int x, int y);
static void draw_small_garden(lv_obj_t *parent);
static void create_flower(lv_obj_t *parent, int index, int x, int y);
static void update_flower_progress(void);
static void update_tomato_count_label(void);
static void start_harvest(uint32_t minutes);
static void launch_next_tomato(void);
static void harvest_x_anim_cb(void *obj, int32_t value);
static void harvest_y_anim_cb(void *obj, int32_t value);
static void harvest_anim_completed_cb(lv_anim_t *a);
static lv_obj_t *create_particle(lv_obj_t *parent, int x, int y);
static void particle_y_anim_cb(void *obj, int32_t value);
static void particle_opa_anim_cb(void *obj, int32_t value);
static void start_particle_anim(lv_obj_t *particle, int delay, int height);
static void build_status_icon(lv_obj_t *parent, int x, int y);
static void set_status_icon_style(bool running, bool done);
static void garden_focus_start_pause(void);
static void garden_focus_reset(void);
static void time_slider_cb(lv_event_t *e);
static void start_pause_cb(lv_event_t *e);
static void reset_cb(lv_event_t *e);

lv_obj_t * garden_focus_create(lv_obj_t *parent) {
    s_focus.selected_minutes = DEFAULT_MINUTES;
    s_focus.total_ms = s_focus.selected_minutes * MINUTE_MS;
    s_focus.remaining_ms = s_focus.total_ms;
    s_focus.harvested_minutes = 0;
    s_focus.harvest_pending = 0;
    s_focus.running = false;
    s_focus.harvest_ready = false;
    s_focus.harvesting = false;
    s_focus.done_cb = NULL;

    s_focus.page = lv_obj_create(parent);
    lv_obj_set_pos(s_focus.page, 0, 0);
    lv_obj_set_size(s_focus.page, DISP_W, DISP_H);
    lv_obj_set_style_bg_color(s_focus.page, lv_color_hex(0x14222A), 0);
    lv_obj_set_style_border_width(s_focus.page, 0, 0);
    lv_obj_set_style_radius(s_focus.page, 0, 0);
    lv_obj_set_style_pad_all(s_focus.page, 0, 0);
    lv_obj_clear_flag(s_focus.page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = create_hud_panel(s_focus.page, 0, 0, DISP_W, TOP_H, lv_color_hex(0x102128));
    draw_tomato_icon(top, 22, 11);
    s_focus.tomato_count_label = create_hud_label(top, "00:00", lv_color_hex(0xEAFBF6));
    lv_obj_set_pos(s_focus.tomato_count_label, 62, 16);

    build_status_icon(top, 196, 12);
    s_focus.state_label = create_hud_label(top, "READY", lv_color_hex(0xFFDD88));
    lv_obj_set_pos(s_focus.state_label, 236, 16);

    draw_energy_icon(top, 380, 10);
    lv_obj_t *energy = create_hud_label(top, "+15 ENERGY", lv_color_hex(0xA6E3E9));
    lv_obj_set_pos(energy, 424, 16);

    lv_obj_t *task = create_hud_label(top, "DEEP WORK", lv_color_hex(0xB8F3D4));
    lv_obj_align(task, LV_ALIGN_RIGHT_MID, -28, 0);

    build_pixel_time(s_focus.page);

    lv_obj_t *garden = create_hud_panel(s_focus.page, 160, 268, 960, 116, lv_color_hex(0x152C31));
    draw_small_garden(garden);
    s_focus.harvest_halo = create_harvest_halo(s_focus.page);
    s_focus.harvest_tomato = create_harvest_tomato(s_focus.page);
    s_focus.flying_tomato = create_flying_tomato(s_focus.page);
    lv_obj_add_flag(s_focus.harvest_halo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_focus.harvest_tomato, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_focus.flying_tomato, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *bottom = create_hud_panel(s_focus.page, 0, DISP_H - BOTTOM_H, DISP_W, BOTTOM_H, lv_color_hex(0x102128));

    s_focus.bottom_hint_label = create_hud_label(bottom, "SET FOCUS TIME", lv_color_hex(0x88AAB0));
    lv_obj_set_pos(s_focus.bottom_hint_label, 38, 22);

    s_focus.time_set_label = create_hud_label(bottom, "25 MIN", lv_color_hex(0xEAFBF6));
    lv_obj_set_pos(s_focus.time_set_label, 238, 22);

    s_focus.time_slider = lv_slider_create(bottom);
    lv_obj_set_size(s_focus.time_slider, 610, 18);
    lv_obj_set_pos(s_focus.time_slider, 340, 21);
    lv_slider_set_range(s_focus.time_slider, MIN_MINUTES, MAX_MINUTES);
    lv_slider_set_value(s_focus.time_slider, (int32_t)s_focus.selected_minutes, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_focus.time_slider, lv_color_hex(0x0A171B), 0);
    lv_obj_set_style_bg_color(s_focus.time_slider, lv_color_hex(0x66D9A8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_focus.time_slider, lv_color_hex(0xEAFBF6), LV_PART_KNOB);
    lv_obj_set_style_radius(s_focus.time_slider, 3, 0);
    lv_obj_set_style_radius(s_focus.time_slider, 3, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_focus.time_slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(s_focus.time_slider, time_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *start_btn = lv_btn_create(bottom);
    lv_obj_set_size(start_btn, 92, 56);
    lv_obj_set_pos(start_btn, 1042, 2);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x66D9A8), 0);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x38B67E), LV_STATE_PRESSED);
    lv_obj_set_style_radius(start_btn, 6, 0);
    lv_obj_add_event_cb(start_btn, start_pause_cb, LV_EVENT_CLICKED, NULL);
    s_focus.button_label = lv_label_create(start_btn);
    lv_label_set_text(s_focus.button_label, "START");
    lv_obj_set_style_text_color(s_focus.button_label, lv_color_hex(0x0A171B), 0);
    lv_obj_set_style_text_font(s_focus.button_label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_focus.button_label);

    lv_obj_t *reset_btn = lv_btn_create(bottom);
    lv_obj_set_size(reset_btn, 92, 48);
    lv_obj_set_pos(reset_btn, 1156, 6);
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
    (void)active;
}

void garden_focus_tick(uint32_t elapsed_ms) {
    if (!s_focus.running) return;

    if (s_focus.remaining_ms > elapsed_ms) {
        s_focus.remaining_ms -= elapsed_ms;
    } else {
        s_focus.remaining_ms = 0;
        s_focus.running = false;
        if (s_focus.done_cb) {
            s_focus.done_cb(150);
        }
        start_harvest(s_focus.selected_minutes);
    }
    update_labels();
}

static lv_obj_t *create_hud_panel(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, bg, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x2A5962), 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t *create_hud_label(lv_obj_t *parent, const char *text, lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    return label;
}

static void draw_cell(lv_obj_t *parent, int x, int y, int size, lv_color_t color, lv_opa_t opa) {
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_size(cell, size, size);
    lv_obj_set_style_radius(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_bg_color(cell, color, 0);
    lv_obj_set_style_bg_opa(cell, opa, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
}

static void draw_bitmap(lv_obj_t *parent, int x, int y, int size, const uint8_t *rows, int row_count, lv_color_t color) {
    for (int row = 0; row < row_count; row++) {
        for (int col = 0; col < 5; col++) {
            if (rows[row] & (1U << (4 - col))) {
                draw_cell(parent, x + col * size, y + row * size, size, color, LV_OPA_COVER);
            }
        }
    }
}

static void draw_tomato_icon(lv_obj_t *parent, int x, int y) {
    static const uint8_t tomato[5] = { 0x0A, 0x1F, 0x1F, 0x1F, 0x0E };
    draw_bitmap(parent, x, y + 4, 5, tomato, 5, lv_color_hex(0xF35D55));
    draw_cell(parent, x + 10, y, 5, lv_color_hex(0x7FD36B), LV_OPA_COVER);
    draw_cell(parent, x + 15, y + 5, 5, lv_color_hex(0x7FD36B), LV_OPA_COVER);
}

static lv_obj_t *create_flying_tomato(lv_obj_t *parent) {
    lv_obj_t *tomato = lv_obj_create(parent);
    lv_obj_set_size(tomato, 36, 36);
    lv_obj_set_style_bg_opa(tomato, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tomato, 0, 0);
    lv_obj_set_style_pad_all(tomato, 0, 0);
    lv_obj_clear_flag(tomato, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tomato, LV_OBJ_FLAG_CLICKABLE);

    draw_tomato_icon(tomato, 5, 4);
    lv_obj_set_style_shadow_width(tomato, 12, 0);
    lv_obj_set_style_shadow_color(tomato, lv_color_hex(0xF35D55), 0);
    lv_obj_set_style_shadow_opa(tomato, LV_OPA_60, 0);
    return tomato;
}

static lv_obj_t *create_harvest_halo(lv_obj_t *parent) {
    lv_obj_t *halo = lv_obj_create(parent);
    lv_obj_set_pos(halo, 580, 264);
    lv_obj_set_size(halo, 120, 120);
    lv_obj_set_style_bg_opa(halo, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(halo, 5, 0);
    lv_obj_set_style_border_color(halo, lv_color_hex(0xFFD166), 0);
    lv_obj_set_style_border_opa(halo, LV_OPA_80, 0);
    lv_obj_set_style_radius(halo, 60, 0);
    lv_obj_set_style_pad_all(halo, 0, 0);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_CLICKABLE);
    return halo;
}

static lv_obj_t *create_harvest_tomato(lv_obj_t *parent) {
    lv_obj_t *tomato = lv_obj_create(parent);
    lv_obj_set_pos(tomato, 594, 278);
    lv_obj_set_size(tomato, 92, 92);
    lv_obj_set_style_bg_opa(tomato, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tomato, 0, 0);
    lv_obj_set_style_pad_all(tomato, 0, 0);
    lv_obj_clear_flag(tomato, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tomato, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(tomato, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(tomato, harvest_ready_cb, LV_EVENT_CLICKED, NULL);

    static const uint8_t big_tomato[7] = { 0x0A, 0x1F, 0x1F, 0x1F, 0x1F, 0x0E, 0x04 };
    draw_bitmap(tomato, 18, 16, 12, big_tomato, 7, lv_color_hex(0xF35D55));
    draw_cell(tomato, 42, 4, 12, lv_color_hex(0x7FD36B), LV_OPA_COVER);
    draw_cell(tomato, 54, 16, 12, lv_color_hex(0x7FD36B), LV_OPA_COVER);
    draw_cell(tomato, 42, 40, 12, lv_color_hex(0xFF8A76), LV_OPA_COVER);
    lv_obj_set_style_shadow_width(tomato, 26, 0);
    lv_obj_set_style_shadow_color(tomato, lv_color_hex(0xF35D55), 0);
    lv_obj_set_style_shadow_opa(tomato, LV_OPA_70, 0);
    return tomato;
}

static void show_harvest_ready(void) {
    if (!s_focus.harvest_tomato || !s_focus.harvest_halo) return;

    lv_obj_set_pos(s_focus.harvest_halo, 580, 264);
    lv_obj_set_size(s_focus.harvest_halo, 120, 120);
    lv_obj_set_style_radius(s_focus.harvest_halo, 60, 0);
    lv_obj_set_pos(s_focus.harvest_tomato, 594, 278);
    lv_obj_clear_flag(s_focus.harvest_halo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_focus.harvest_tomato, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_focus.harvest_halo);
    lv_obj_move_foreground(s_focus.harvest_tomato);

    lv_anim_delete(s_focus.harvest_tomato, tomato_scale_anim_cb);
    lv_anim_delete(s_focus.harvest_halo, halo_opa_anim_cb);
    lv_anim_delete(s_focus.harvest_halo, halo_size_anim_cb);

    lv_anim_t pop;
    lv_anim_init(&pop);
    lv_anim_set_var(&pop, s_focus.harvest_tomato);
    lv_anim_set_exec_cb(&pop, tomato_scale_anim_cb);
    lv_anim_set_values(&pop, 120, 256);
    lv_anim_set_duration(&pop, 520);
    lv_anim_set_path_cb(&pop, lv_anim_path_overshoot);
    lv_anim_start(&pop);

    lv_anim_t glow;
    lv_anim_init(&glow);
    lv_anim_set_var(&glow, s_focus.harvest_halo);
    lv_anim_set_exec_cb(&glow, halo_opa_anim_cb);
    lv_anim_set_values(&glow, LV_OPA_30, LV_OPA_90);
    lv_anim_set_duration(&glow, 620);
    lv_anim_set_playback_duration(&glow, 620);
    lv_anim_set_repeat_count(&glow, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&glow, lv_anim_path_ease_in_out);
    lv_anim_start(&glow);

    lv_anim_t pulse;
    lv_anim_init(&pulse);
    lv_anim_set_var(&pulse, s_focus.harvest_halo);
    lv_anim_set_exec_cb(&pulse, halo_size_anim_cb);
    lv_anim_set_values(&pulse, 112, 132);
    lv_anim_set_duration(&pulse, 760);
    lv_anim_set_playback_duration(&pulse, 760);
    lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&pulse, lv_anim_path_ease_in_out);
    lv_anim_start(&pulse);
}

static void hide_harvest_ready(void) {
    if (s_focus.harvest_tomato) {
        lv_anim_delete(s_focus.harvest_tomato, tomato_scale_anim_cb);
        lv_obj_add_flag(s_focus.harvest_tomato, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_focus.harvest_halo) {
        lv_anim_delete(s_focus.harvest_halo, halo_opa_anim_cb);
        lv_anim_delete(s_focus.harvest_halo, halo_size_anim_cb);
        lv_obj_add_flag(s_focus.harvest_halo, LV_OBJ_FLAG_HIDDEN);
    }
}

static void harvest_ready_cb(lv_event_t *e) {
    (void)e;
    if (!s_focus.harvest_ready || s_focus.harvesting) return;

    hide_harvest_ready();
    s_focus.harvest_ready = false;
    s_focus.harvesting = true;
    update_labels();
    launch_next_tomato();
}

static void tomato_scale_anim_cb(void *obj, int32_t value) {
    lv_obj_set_style_transform_scale((lv_obj_t *)obj, (int32_t)value, 0);
}

static void halo_opa_anim_cb(void *obj, int32_t value) {
    lv_obj_set_style_border_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void halo_size_anim_cb(void *obj, int32_t value) {
    lv_obj_t *halo = (lv_obj_t *)obj;
    lv_obj_set_size(halo, value, value);
    lv_obj_set_pos(halo, 640 - value / 2, 324 - value / 2);
    lv_obj_set_style_radius(halo, value / 2, 0);
}

static void draw_energy_icon(lv_obj_t *parent, int x, int y) {
    static const uint8_t bolt[6] = { 0x04, 0x0C, 0x1E, 0x06, 0x0C, 0x08 };
    draw_bitmap(parent, x, y, 5, bolt, 6, lv_color_hex(0x53F0BA));
}

static void build_status_icon(lv_obj_t *parent, int x, int y) {
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            lv_obj_t *cell = lv_obj_create(parent);
            s_focus.status_icon[row][col] = cell;
            lv_obj_set_pos(cell, x + col * 6, y + row * 6);
            lv_obj_set_size(cell, 5, 5);
            lv_obj_set_style_radius(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

static void set_status_icon_style(bool running, bool done) {
    static const uint8_t ready[4] = { 0x6, 0x9, 0x9, 0x6 };
    static const uint8_t play[4]  = { 0x8, 0xC, 0xE, 0xC };
    static const uint8_t check[4] = { 0x1, 0x3, 0x6, 0xC };
    const uint8_t *rows = done ? check : (running ? play : ready);
    lv_color_t on = done ? lv_color_hex(0xFFD166) : (running ? lv_color_hex(0x66D9A8) : lv_color_hex(0xFFDD88));

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            lv_obj_t *cell = s_focus.status_icon[row][col];
            if (!cell) continue;
            bool active = (rows[row] & (1U << (3 - col))) != 0;
            lv_obj_set_style_bg_color(cell, active ? on : lv_color_hex(0x071215), 0);
            lv_obj_set_style_bg_opa(cell, active ? LV_OPA_COVER : LV_OPA_30, 0);
        }
    }
}

static void draw_small_garden(lv_obj_t *parent) {
    for (int i = 0; i < FLOWER_COUNT; i++) {
        int ground_x = 32 + i * 50;
        int grass_y = 74 - (i % 3) * 4;

        draw_cell(parent, ground_x, 88, 42, lv_color_hex(0x36513D), LV_OPA_COVER);
        draw_cell(parent, ground_x + 4, grass_y + 10, 7, lv_color_hex(0x4F9960), LV_OPA_COVER);
        draw_cell(parent, ground_x + 13, grass_y + 3, 7, lv_color_hex(0x6FCA78), LV_OPA_COVER);
        draw_cell(parent, ground_x + 22, grass_y + 9, 7, lv_color_hex(0x5FAF6D), LV_OPA_COVER);
        draw_cell(parent, ground_x + 31, grass_y + 5, 7, lv_color_hex(0x7CD982), LV_OPA_COVER);

        create_flower(parent, i, ground_x + 2, 42 - (i % 2) * 4);
    }

    for (int i = 0; i < 8; i++) {
        int x = 250 + i * 64;
        int y = 24 + (i % 3) * 10;
        lv_obj_t *particle = create_particle(parent, x, y);
        start_particle_anim(particle, i * 140, 18 + (i % 3) * 6);
    }
}

static void create_flower(lv_obj_t *parent, int index, int x, int y) {
    lv_obj_t *flower = lv_obj_create(parent);
    s_focus.flowers[index] = flower;
    lv_obj_set_pos(flower, x, y);
    lv_obj_set_size(flower, 42, 56);
    lv_obj_set_style_bg_opa(flower, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(flower, 0, 0);
    lv_obj_set_style_pad_all(flower, 0, 0);
    lv_obj_clear_flag(flower, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(flower, LV_OBJ_FLAG_CLICKABLE);

    draw_cell(flower, 18, 26, 7, lv_color_hex(0x62BE72), LV_OPA_COVER);
    draw_cell(flower, 18, 33, 7, lv_color_hex(0x62BE72), LV_OPA_COVER);
    draw_cell(flower, 18, 40, 7, lv_color_hex(0x4F9960), LV_OPA_COVER);
    draw_cell(flower, 10, 33, 8, lv_color_hex(0x7CD982), LV_OPA_COVER);
    draw_cell(flower, 25, 31, 8, lv_color_hex(0x7CD982), LV_OPA_COVER);
    draw_cell(flower, 17, 7, 9, lv_color_hex(0xFFD166), LV_OPA_COVER);
    draw_cell(flower, 8, 16, 9, lv_color_hex(0xF97878), LV_OPA_COVER);
    draw_cell(flower, 17, 16, 9, lv_color_hex(0xFFF2A8), LV_OPA_COVER);
    draw_cell(flower, 26, 16, 9, lv_color_hex(0xF97878), LV_OPA_COVER);
    draw_cell(flower, 17, 25, 9, lv_color_hex(0xF6A85F), LV_OPA_COVER);
    lv_obj_add_flag(flower, LV_OBJ_FLAG_HIDDEN);
}

static void update_flower_progress(void) {
    uint32_t done_ms = s_focus.total_ms - s_focus.remaining_ms;
    uint32_t visible = 0;

    if (s_focus.total_ms > 0) {
        visible = (done_ms * FLOWER_COUNT + s_focus.total_ms - 1U) / s_focus.total_ms;
    }
    if (visible > FLOWER_COUNT) visible = FLOWER_COUNT;

    for (uint32_t i = 0; i < FLOWER_COUNT; i++) {
        if (!s_focus.flowers[i]) continue;
        if (i < visible) {
            lv_obj_clear_flag(s_focus.flowers[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_focus.flowers[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void update_tomato_count_label(void) {
    if (!s_focus.tomato_count_label) return;

    uint32_t hours = s_focus.harvested_minutes / 60U;
    uint32_t minutes = s_focus.harvested_minutes % 60U;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes);
    lv_label_set_text(s_focus.tomato_count_label, buf);
}

static void start_harvest(uint32_t minutes) {
    if (minutes == 0 || s_focus.harvesting || s_focus.harvest_ready) return;

    s_focus.harvest_pending = minutes;
    s_focus.harvest_ready = true;
    show_harvest_ready();
}

static void launch_next_tomato(void) {
    if (!s_focus.flying_tomato) return;

    if (s_focus.harvest_pending == 0) {
        s_focus.harvesting = false;
        lv_obj_add_flag(s_focus.flying_tomato, LV_OBJ_FLAG_HIDDEN);
        update_labels();
        return;
    }

    lv_anim_delete(s_focus.flying_tomato, harvest_x_anim_cb);
    lv_anim_delete(s_focus.flying_tomato, harvest_y_anim_cb);

    lv_obj_set_pos(s_focus.flying_tomato, 620, 288);
    lv_obj_clear_flag(s_focus.flying_tomato, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_focus.flying_tomato);

    lv_anim_t ax;
    lv_anim_init(&ax);
    lv_anim_set_var(&ax, s_focus.flying_tomato);
    lv_anim_set_exec_cb(&ax, harvest_x_anim_cb);
    lv_anim_set_values(&ax, 620, 20);
    lv_anim_set_duration(&ax, 520);
    lv_anim_set_path_cb(&ax, lv_anim_path_ease_in_out);
    lv_anim_start(&ax);

    lv_anim_t ay;
    lv_anim_init(&ay);
    lv_anim_set_var(&ay, s_focus.flying_tomato);
    lv_anim_set_exec_cb(&ay, harvest_y_anim_cb);
    lv_anim_set_values(&ay, 288, 8);
    lv_anim_set_duration(&ay, 520);
    lv_anim_set_path_cb(&ay, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&ay, harvest_anim_completed_cb);
    lv_anim_start(&ay);
}

static void harvest_x_anim_cb(void *obj, int32_t value) {
    lv_obj_set_x((lv_obj_t *)obj, (lv_coord_t)value);
}

static void harvest_y_anim_cb(void *obj, int32_t value) {
    lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)value);
}

static void harvest_anim_completed_cb(lv_anim_t *a) {
    (void)a;
    if (s_focus.harvest_pending > 0) {
        s_focus.harvest_pending--;
        s_focus.harvested_minutes++;
        update_tomato_count_label();
    }
    launch_next_tomato();
}

static lv_obj_t *create_particle(lv_obj_t *parent, int x, int y) {
    lv_obj_t *particle = lv_obj_create(parent);
    lv_obj_set_pos(particle, x, y);
    lv_obj_set_size(particle, 6, 6);
    lv_obj_set_style_radius(particle, 0, 0);
    lv_obj_set_style_pad_all(particle, 0, 0);
    lv_obj_set_style_border_width(particle, 0, 0);
    lv_obj_set_style_bg_color(particle, lv_color_hex(0x53F0BA), 0);
    lv_obj_set_style_bg_opa(particle, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(particle, 10, 0);
    lv_obj_set_style_shadow_color(particle, lv_color_hex(0x26FFBA), 0);
    lv_obj_set_style_shadow_opa(particle, LV_OPA_50, 0);
    lv_obj_clear_flag(particle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(particle, LV_OBJ_FLAG_CLICKABLE);
    return particle;
}

static void particle_y_anim_cb(void *obj, int32_t value) {
    lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)value);
}

static void particle_opa_anim_cb(void *obj, int32_t value) {
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
    lv_obj_set_style_shadow_opa((lv_obj_t *)obj, (lv_opa_t)(value / 2), 0);
}

static void start_particle_anim(lv_obj_t *particle, int delay, int height) {
    int32_t y = lv_obj_get_y(particle);

    lv_anim_t move;
    lv_anim_init(&move);
    lv_anim_set_var(&move, particle);
    lv_anim_set_exec_cb(&move, particle_y_anim_cb);
    lv_anim_set_values(&move, y, y - height);
    lv_anim_set_duration(&move, 1100);
    lv_anim_set_delay(&move, delay);
    lv_anim_set_playback_duration(&move, 1100);
    lv_anim_set_repeat_count(&move, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&move, lv_anim_path_ease_in_out);
    lv_anim_start(&move);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, particle);
    lv_anim_set_exec_cb(&fade, particle_opa_anim_cb);
    lv_anim_set_values(&fade, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&fade, 700);
    lv_anim_set_delay(&fade, delay);
    lv_anim_set_playback_duration(&fade, 700);
    lv_anim_set_repeat_count(&fade, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_in_out);
    lv_anim_start(&fade);
}

static void set_pixel_style(lv_obj_t *px, bool on) {
    lv_obj_set_style_bg_color(px, on ? lv_color_hex(0xCFFFF6) : lv_color_hex(0x050E13), 0);
    lv_obj_set_style_bg_opa(px, on ? LV_OPA_COVER : LV_OPA_20, 0);
    lv_obj_set_style_border_width(px, 2, 0);
    lv_obj_set_style_border_color(px, on ? lv_color_hex(0x011014) : lv_color_hex(0x0A1A20), 0);
    lv_obj_set_style_border_opa(px, on ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(px, on ? 24 : 0, 0);
    lv_obj_set_style_shadow_color(px, lv_color_hex(0x26FFBA), 0);
    lv_obj_set_style_shadow_opa(px, on ? LV_OPA_90 : LV_OPA_TRANSP, 0);
}

static void build_pixel_time(lv_obj_t *parent) {
    const int px = 24;
    const int step = 25;
    const int digit_w = step * 5;
    const int colon_w = 30;
    const int digit_gap = 16;
    const int colon_gap = 18;
    const int total_w = digit_w * 6 + digit_gap * 3 + colon_w * 2 + colon_gap * 4;
    const int start_x = (DISP_W - total_w) / 2;
    const int start_y = 72;
    int x = start_x;
    int digit_x[6];
    int colon_x[2];

    digit_x[0] = x; x += digit_w + digit_gap;
    digit_x[1] = x; x += digit_w + colon_gap;
    colon_x[0] = x; x += colon_w + colon_gap;
    digit_x[2] = x; x += digit_w + digit_gap;
    digit_x[3] = x; x += digit_w + colon_gap;
    colon_x[1] = x; x += colon_w + colon_gap;
    digit_x[4] = x; x += digit_w + digit_gap;
    digit_x[5] = x;

    for (int d = 0; d < 6; d++) {
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                lv_obj_t *cell = lv_obj_create(parent);
                s_focus.time_px[d][row][col] = cell;
                lv_obj_set_pos(cell, digit_x[d] + col * step, start_y + row * step);
                lv_obj_set_size(cell, px, px);
                lv_obj_set_style_radius(cell, 0, 0);
                lv_obj_set_style_pad_all(cell, 0, 0);
                lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
                set_pixel_style(cell, false);
            }
        }
    }

    for (int c = 0; c < 2; c++) {
        for (int dot = 0; dot < 2; dot++) {
            lv_obj_t *cell = lv_obj_create(parent);
            s_focus.time_colon[c][dot] = cell;
            lv_obj_set_pos(cell, colon_x[c] + 4, start_y + 42 + dot * 54);
            lv_obj_set_size(cell, px, px);
            lv_obj_set_style_radius(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
            set_pixel_style(cell, true);
        }
    }
}

static void set_pixel_digit(int index, uint8_t value) {
    static const uint8_t glyphs[10][7] = {
        { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },
        { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
        { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
        { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E },
        { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
        { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E },
        { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },
        { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
        { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }
    };

    if (index < 0 || index >= 6 || value > 9) return;
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            lv_obj_t *cell = s_focus.time_px[index][row][col];
            if (cell) {
                bool on = (glyphs[value][row] & (1U << (4 - col))) != 0;
                set_pixel_style(cell, on);
            }
        }
    }
}

static void update_labels(void) {
    uint32_t remaining_s = s_focus.remaining_ms / 1000U;
    uint32_t hours = remaining_s / 3600U;
    uint32_t minutes = (remaining_s / 60U) % 60U;
    uint32_t seconds = remaining_s % 60U;
    bool done = s_focus.remaining_ms == 0;

    set_pixel_digit(0, (uint8_t)(hours / 10U));
    set_pixel_digit(1, (uint8_t)(hours % 10U));
    set_pixel_digit(2, (uint8_t)(minutes / 10U));
    set_pixel_digit(3, (uint8_t)(minutes % 10U));
    set_pixel_digit(4, (uint8_t)(seconds / 10U));
    set_pixel_digit(5, (uint8_t)(seconds % 10U));

    for (int c = 0; c < 2; c++) {
        for (int dot = 0; dot < 2; dot++) {
            if (s_focus.time_colon[c][dot]) {
                bool colon_on = !s_focus.running || (((lv_tick_get() / 500U) & 1U) == 0U);
                set_pixel_style(s_focus.time_colon[c][dot], colon_on);
                if (colon_on) {
                    lv_obj_set_style_bg_color(s_focus.time_colon[c][dot],
                                              s_focus.running ? lv_color_hex(0x66D9A8) : lv_color_hex(0xBFFFF0), 0);
                }
            }
        }
    }

    set_status_icon_style(s_focus.running, done);
    update_flower_progress();
    update_tomato_count_label();

    if (s_focus.state_label) {
        lv_label_set_text(s_focus.state_label,
                          s_focus.harvesting ? "HARVEST" :
                          s_focus.harvest_ready ? "READY PICK" :
                          done ? "DONE" :
                          s_focus.running ? "FOCUSING" : "READY");
    }
    if (s_focus.bottom_hint_label) {
        lv_label_set_text(s_focus.bottom_hint_label,
                          s_focus.harvesting ? "HARVESTING TOMATOES" :
                          s_focus.harvest_ready ? "TAP TOMATO TO HARVEST" :
                          done ? "TOMATO READY" : "SET FOCUS TIME");
    }
    if (s_focus.button_label) {
        lv_label_set_text(s_focus.button_label,
                          s_focus.harvesting ? "WAIT" :
                          s_focus.harvest_ready ? "PICK" :
                          done ? "NEXT" :
                          s_focus.running ? "PAUSE" : "START");
        lv_obj_center(s_focus.button_label);
    }
    if (s_focus.time_set_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu MIN", (unsigned long)s_focus.selected_minutes);
        lv_label_set_text(s_focus.time_set_label, buf);
    }
    if (s_focus.time_slider) {
        lv_obj_t *knob = s_focus.time_slider;
        lv_slider_set_value(s_focus.time_slider, (int32_t)s_focus.selected_minutes, LV_ANIM_OFF);
        if (s_focus.running || s_focus.harvesting || s_focus.harvest_ready) {
            lv_obj_add_state(knob, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(knob, LV_STATE_DISABLED);
        }
    }
}

static void garden_focus_start_pause(void) {
    if (s_focus.harvesting) return;
    if (s_focus.harvest_ready) {
        harvest_ready_cb(NULL);
        return;
    }

    if (s_focus.remaining_ms == 0) {
        s_focus.remaining_ms = s_focus.total_ms;
    }
    s_focus.running = !s_focus.running;
    update_labels();
}

static void garden_focus_reset(void) {
    hide_harvest_ready();
    if (s_focus.flying_tomato) {
        lv_anim_delete(s_focus.flying_tomato, harvest_x_anim_cb);
        lv_anim_delete(s_focus.flying_tomato, harvest_y_anim_cb);
        lv_obj_add_flag(s_focus.flying_tomato, LV_OBJ_FLAG_HIDDEN);
    }
    s_focus.harvest_ready = false;
    s_focus.harvesting = false;
    s_focus.harvest_pending = 0;
    s_focus.running = false;
    s_focus.remaining_ms = s_focus.total_ms;
    update_labels();
}

static void time_slider_cb(lv_event_t *e) {
    if (s_focus.running || s_focus.harvesting || s_focus.harvest_ready) return;

    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    if (value < (int32_t)MIN_MINUTES) value = (int32_t)MIN_MINUTES;
    if (value > (int32_t)MAX_MINUTES) value = (int32_t)MAX_MINUTES;

    s_focus.selected_minutes = (uint32_t)value;
    s_focus.total_ms = s_focus.selected_minutes * MINUTE_MS;
    s_focus.remaining_ms = s_focus.total_ms;
    update_labels();
}

static void start_pause_cb(lv_event_t *e) {
    (void)e;
    if (garden_nav_was_dragging()) return;
    garden_focus_start_pause();
}

static void reset_cb(lv_event_t *e) {
    (void)e;
    if (garden_nav_was_dragging()) return;
    garden_focus_reset();
}
