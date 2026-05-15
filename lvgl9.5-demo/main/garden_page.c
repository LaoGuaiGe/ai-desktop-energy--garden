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
    bool       active;
    particle_t particles[8];
    waterdrop_t drops[12];
} garden_state_t;

static garden_state_t s_state;

/* ── LVGL handles ── */
static lv_obj_t *s_page;
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

/* Butterfly / bird */
static int16_t s_butterfly_x = 200, s_butterfly_y = 180;
static int8_t  s_butterfly_dx = 2, s_butterfly_dy = -1;
static uint8_t s_butterfly_frame = 0;
static int16_t s_bird_x = -30, s_bird_y = 60;

/* ── Forward declarations ── */
static void build_page(lv_obj_t *parent);
static void draw_background(void);
static void draw_scene(void);
static void spawn_particles(int count);
static void water_click_cb(lv_event_t *e);
static inline uint16_t rgb888_to_565(uint32_t c);
static void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color, uint8_t alpha);

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

static void water_click_cb(lv_event_t *e) {
    (void)e;
    garden_page_on_button(0);
}

static void build_page(lv_obj_t *parent) {
    s_page = lv_obj_create(parent);
    lv_obj_set_pos(s_page, 0, 0);
    lv_obj_set_size(s_page, DISP_W, DISP_H);
    lv_obj_set_style_bg_color(s_page, lv_color_hex(0x1A3A1A), 0);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);

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

/* ── Pixel rendering ── */

static inline uint16_t rgb888_to_565(uint32_t c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >>  8) & 0xFF;
    uint8_t b =  c        & 0xFF;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                      uint32_t color, uint8_t alpha) {
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

    /* Sky gradient */
    fill_rect(0, 0,           SCENE_W, SCENE_H / 3,     0x5BAFE6, 255);
    fill_rect(0, SCENE_H / 3, SCENE_W, SCENE_H / 3,     0x87CEEB, 255);
    fill_rect(0, SCENE_H * 2 / 3, SCENE_W, SCENE_H / 3, 0xA8DCEF, 255);

    /* Sun */
    fill_rect(780, 20, 40, 40, 0xFFEE44, 255);
    fill_rect(776, 28, 48, 24, 0xFFEE44, 255);
    fill_rect(788, 16, 24, 48, 0xFFEE44, 255);
    fill_rect(770, 36, 6, 8, 0xFFDD44, 180);
    fill_rect(824, 36, 6, 8, 0xFFDD44, 180);
    fill_rect(796, 10, 8, 6, 0xFFDD44, 180);
    fill_rect(796, 64, 8, 6, 0xFFDD44, 180);

    /* Distant hills */
    for (int16_t i = 0; i < SCENE_W; i += 6) {
        int16_t h = (int16_t)(30 + 15 * ((i * 7 + 13) % 11) / 10);
        fill_rect(i, base_y - h - 60, 6, h, 0x6AAA5A, 255);
    }
    fill_rect(50,  base_y - 100, 120, 20, 0x6AAA5A, 255);
    fill_rect(250, base_y - 110, 150, 25, 0x5A9A4A, 255);
    fill_rect(500, base_y - 95,  130, 18, 0x6AAA5A, 255);
    fill_rect(700, base_y - 105, 140, 22, 0x5A9A4A, 255);

    /* Background trees */
    for (int tx = 80; tx < SCENE_W - 50; tx += 140) {
        int16_t th = (int16_t)(40 + (tx * 3) % 20);
        fill_rect(tx, base_y - 60 - th, 12, th, 0x3A7A2A, 255);
        fill_rect(tx - 10, base_y - 60 - th - 10, 32, 20, 0x4A8A3A, 255);
        fill_rect(tx - 6,  base_y - 60 - th - 20, 24, 14, 0x4A8A3A, 255);
    }

    /* Grass ground */
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

    /* Fences */
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

    /* Flowers */
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

    /* Mushrooms */
    fill_rect(160, base_y - 10, 4, 10, 0xEEDDCC, 255);
    fill_rect(155, base_y - 16, 14, 8, 0xFF4444, 255);
    fill_rect(157, base_y - 18, 4, 4, 0xFFFFFF, 255);
    fill_rect(163, base_y - 16, 4, 4, 0xFFFFFF, 255);
    fill_rect(175, base_y - 8, 3, 8, 0xEEDDCC, 255);
    fill_rect(172, base_y - 12, 10, 6, 0xFF6644, 255);
    fill_rect(174, base_y - 13, 3, 3, 0xFFFFFF, 255);

    /* Stones */
    fill_rect(400, base_y + 2, 12, 8, 0x999999, 255);
    fill_rect(402, base_y,     8, 4, 0xAAAAAA, 255);
    fill_rect(500, base_y + 4, 10, 6, 0x888888, 255);
    fill_rect(350, base_y + 6, 8, 5, 0x777777, 255);

    /* Path */
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

    /* Clouds */
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

    /* Plant (stages 0-5) */
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

    /* Butterfly */
    s_butterfly_frame = (uint8_t)((s_butterfly_frame + 1) % 4);
    int8_t wing_off = (s_butterfly_frame < 2) ? 0 : 3;
    fill_rect(s_butterfly_x - 6, s_butterfly_y - wing_off, 6, 8 - wing_off, 0xFF88FF, 220);
    fill_rect(s_butterfly_x + 2, s_butterfly_y - wing_off, 6, 8 - wing_off, 0xFFAA88, 220);
    fill_rect(s_butterfly_x,     s_butterfly_y, 2, 6, 0x333333, 255);
    s_butterfly_x = (int16_t)(s_butterfly_x + s_butterfly_dx);
    s_butterfly_y = (int16_t)(s_butterfly_y + s_butterfly_dy);
    if (s_butterfly_x < 220 || s_butterfly_x > 680) s_butterfly_dx = (int8_t)(-s_butterfly_dx);
    if (s_butterfly_y < 120 || s_butterfly_y > base_y - 40) s_butterfly_dy = (int8_t)(-s_butterfly_dy);

    /* Bird */
    s_bird_x = (int16_t)(s_bird_x + 3);
    if (s_bird_x > SCENE_W + 30) { s_bird_x = -30; s_bird_y = (int16_t)(40 + (s_bird_y * 7) % 60); }
    fill_rect(s_bird_x, s_bird_y, 4, 2, 0x333333, 255);
    fill_rect(s_bird_x - 6, s_bird_y - 2, 6, 2, 0x333333, 255);
    fill_rect(s_bird_x + 4, s_bird_y - 2, 6, 2, 0x333333, 255);

    /* Particles */
    for (int i = 0; i < 8; i++) {
        if (!s_state.particles[i].active) continue;
        fill_rect(s_state.particles[i].x, s_state.particles[i].y,
                  6, 6, 0x44FF88, s_state.particles[i].alpha);
        fill_rect(s_state.particles[i].x - 2, s_state.particles[i].y - 2,
                  10, 10, 0x88FFAA, (uint8_t)(s_state.particles[i].alpha / 4));
    }

    /* Water drops */
    for (int i = 0; i < 12; i++) {
        if (!s_state.drops[i].active) continue;
        fill_rect(s_state.drops[i].x, s_state.drops[i].y, 3, 8, 0x44AAFF, 200);
        fill_rect(s_state.drops[i].x, s_state.drops[i].y, 2, 4, 0x88CCFF, 255);
    }

    /* Wet soil */
    if (s_state.wet_timer > 0) {
        uint8_t wet_alpha = (uint8_t)(s_state.wet_timer > 1000 ? 80 : s_state.wet_timer * 80 / 1000);
        fill_rect(root_x - 40, base_y + 2, 80, 12, 0x3A2A00, wet_alpha);
        fill_rect(root_x - 30, base_y - 2, 60, 6, 0x4A3A0A, (uint8_t)(wet_alpha / 2));
    }

    /* Plant shadow */
    {
        int8_t  sw[] = {-4, -1, 4, 1};
        int16_t shadow_off = (int16_t)(sw[s_state.plant_frame] * 2);
        fill_rect(root_x - 20 + shadow_off, base_y + 4, 50, 6, 0x000000, 30);
        fill_rect(root_x - 14 + shadow_off, base_y + 2, 38, 4, 0x000000, 20);
    }

    /* Foreground grass */
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

    /* Evolution flash */
    if (s_state.evolve_timer > 0) {
        uint8_t flash = (uint8_t)(s_state.evolve_timer > 400
                        ? (s_state.evolve_timer - 400) * 120 / 400
                        : s_state.evolve_timer * 120 / 400);
        fill_rect(root_x - 50, base_y - 175, 100, 5, 0xFFFFFF, (uint8_t)(flash * 2 / 3));
        fill_rect(root_x - 55, base_y - 170, 110, 150, 0xFFFFFF, flash);
        fill_rect(root_x - 50, base_y - 20, 100, 5, 0xFFFFFF, (uint8_t)(flash * 2 / 3));
        fill_rect(root_x - 30, base_y - 150, 60, 110, 0xFFFFFF, (uint8_t)(flash * 3 / 4));
    }

    /* Energy bar full glow */
    if (s_state.energy_x10 >= 1000) {
        uint8_t pulse = (uint8_t)(80 + (s_state.plant_frame * 30));
        fill_rect(root_x - 30, base_y - 180, 60, 4, 0xFFDD44, pulse);
        fill_rect(root_x - 20, base_y - 185, 40, 3, 0xFFDD44, (uint8_t)(pulse / 2));
    }

    lv_obj_invalidate(s_canvas_scene);
    s_state.last_draw_us = (uint32_t)(esp_timer_get_time() - draw_start_us);
}
