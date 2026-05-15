#include "garden_ui.h"
#include <stdio.h>
#include <string.h>

/* ── Layout constants ── */
#define DISP_W   1280
#define DISP_H   452
#define LEFT_W   120
#define RIGHT_W  260
#define SCENE_W  (DISP_W - LEFT_W - RIGHT_W)   /* 900 */
#define SCENE_H  DISP_H                          /* 452 */
#define SCENE_X  LEFT_W

/* ── Particle ── */
typedef struct {
    int16_t  x, y;
    int16_t  vy;      /* negative = upward, px per tick */
    uint8_t  alpha;   /* 0-255, fades as it rises */
    bool     active;
} particle_t;

/* ── Garden state ── */
typedef struct {
    uint8_t    plant_stage;   /* 0-5, fixed 3 (mature) in demo */
    uint16_t   energy_x10;   /* energy * 10, initial 450 = 45.0 */
    uint8_t    streak_days;  /* fixed 7 in demo */
    uint8_t    plant_frame;  /* 0-3 pixel animation frame */
    uint32_t   frame_timer;  /* ms accumulator for plant anim */
    uint32_t   burst_timer;  /* ms remaining for watering burst */
    particle_t particles[8];
} garden_state_t;

static garden_state_t s_state;

/* ── LVGL handles ── */
static lv_obj_t *s_canvas_scene;  /* single 900×452 canvas: sky+plant+particles */
static lv_obj_t *s_bar_energy;
static lv_obj_t *s_label_level;
static lv_obj_t *s_label_streak;

/* Cloud x positions (wrap at SCENE_W) */
static int16_t s_cloud1_x = 80;
static int16_t s_cloud2_x = 380;

/* Canvas buffer: RGB565, 900×452 = 813600 bytes */
static uint8_t s_scene_buf[SCENE_W * SCENE_H * 2];

/* ── Forward declarations ── */
static void build_layout(void);
static void draw_scene(void);
static void spawn_particles(int count);
static void water_cb(lv_event_t *e);

/* ── Public API ── */

void garden_ui_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.plant_stage = 3;
    s_state.energy_x10  = 450;
    s_state.streak_days = 7;
    spawn_particles(4);
    build_layout();
    draw_scene();
}

void garden_ui_encoder_event(int delta) {
    printf("[encoder] delta=%d (page switch reserved in demo)\n", delta);
}

void garden_ui_button_event(uint8_t type) {
    if (type == 0) {
        s_state.energy_x10 += 100;
        if (s_state.energy_x10 > 1000) s_state.energy_x10 = 1000;
        lv_bar_set_value(s_bar_energy, s_state.energy_x10 / 10, LV_ANIM_ON);
        s_state.burst_timer = 1000;
        spawn_particles(8);
    }
}

void garden_ui_touch_event(int16_t x, int16_t y, bool pressed) {
    /* LVGL pointer indev handles widget hit-testing; nothing extra needed */
    (void)x; (void)y; (void)pressed;
}

void garden_ui_tick(uint32_t elapsed_ms) {
    /* Plant animation: 4 frames × 400ms */
    s_state.frame_timer += elapsed_ms;
    if (s_state.frame_timer >= 400) {
        s_state.frame_timer = 0;
        s_state.plant_frame = (s_state.plant_frame + 1) % 4;
    }

    /* Burst timer */
    if (s_state.burst_timer > elapsed_ms)
        s_state.burst_timer -= elapsed_ms;
    else
        s_state.burst_timer = 0;

    /* Move clouds */
    s_cloud1_x = (int16_t)(s_cloud1_x + 1);
    s_cloud2_x = (int16_t)(s_cloud2_x + 1);
    if (s_cloud1_x > SCENE_W + 40) s_cloud1_x = -40;
    if (s_cloud2_x > SCENE_W + 40) s_cloud2_x = -40;

    /* Update particles */
    for (int i = 0; i < 8; i++) {
        if (!s_state.particles[i].active) continue;
        s_state.particles[i].y = (int16_t)(s_state.particles[i].y + s_state.particles[i].vy);
        s_state.particles[i].alpha = (uint8_t)(s_state.particles[i].alpha > 5
                                     ? s_state.particles[i].alpha - 5 : 0);
        if (s_state.particles[i].y < 0 || s_state.particles[i].alpha == 0) {
            /* Respawn at plant base */
            s_state.particles[i].x     = (int16_t)(SCENE_W / 2 - 20 + i * 10);
            s_state.particles[i].y     = (int16_t)(SCENE_H - 80);
            s_state.particles[i].vy    = (int16_t)(-(2 + (i % 3)));
            s_state.particles[i].alpha = 200;
        }
    }

    draw_scene();
}

/* ── Private helpers ── */

static void spawn_particles(int count) {
    for (int i = 0; i < 8 && i < count; i++) {
        s_state.particles[i].x      = (int16_t)(SCENE_W / 2 - 20 + i * 12);
        s_state.particles[i].y      = (int16_t)(SCENE_H - 80);
        s_state.particles[i].vy     = (int16_t)(-(2 + (i % 4)));
        s_state.particles[i].alpha  = (uint8_t)(180 + i * 9);
        s_state.particles[i].active = true;
    }
}

static void water_cb(lv_event_t *e) {
    (void)e;
    garden_ui_button_event(0);
}

static void build_layout(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x2A4A2A), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Left status bar ── */
    lv_obj_t *left = lv_obj_create(scr);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_size(left, LEFT_W, DISP_H);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x3A6A2A), 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_set_style_pad_all(left, 8, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(left, 10, 0);

    lv_obj_t *l_temp = lv_label_create(left);
    lv_label_set_text(l_temp, "26 C");
    lv_obj_set_style_text_color(l_temp, lv_color_hex(0xFFFF88), 0);
    lv_obj_set_style_text_font(l_temp, &lv_font_montserrat_14, 0);

    lv_obj_t *l_dev = lv_label_create(left);
    lv_label_set_text(l_dev, "DEV: 2");
    lv_obj_set_style_text_color(l_dev, lv_color_hex(0xAAFFAA), 0);
    lv_obj_set_style_text_font(l_dev, &lv_font_montserrat_14, 0);

    lv_obj_t *l_net = lv_label_create(left);
    lv_label_set_text(l_net, "ONLINE");
    lv_obj_set_style_text_color(l_net, lv_color_hex(0xAAFFAA), 0);
    lv_obj_set_style_text_font(l_net, &lv_font_montserrat_14, 0);

    lv_obj_t *l_hub = lv_label_create(left);
    lv_label_set_text(l_hub, "HUB ***");
    lv_obj_set_style_text_color(l_hub, lv_color_hex(0xFFDD88), 0);
    lv_obj_set_style_text_font(l_hub, &lv_font_montserrat_14, 0);

    /* ── Center scene canvas ── */
    s_canvas_scene = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas_scene, s_scene_buf, SCENE_W, SCENE_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas_scene, SCENE_X, 0);
    lv_obj_add_flag(s_canvas_scene, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas_scene, water_cb, LV_EVENT_CLICKED, NULL);

    /* ── Right info bar ── */
    lv_obj_t *right = lv_obj_create(scr);
    lv_obj_set_pos(right, SCENE_X + SCENE_W, 0);
    lv_obj_set_size(right, RIGHT_W, DISP_H);
    lv_obj_set_style_bg_color(right, lv_color_hex(0x3A6A2A), 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_set_style_pad_all(right, 12, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(right, 12, 0);

    lv_obj_t *e_lbl = lv_label_create(right);
    lv_label_set_text(e_lbl, "Energy");
    lv_obj_set_style_text_color(e_lbl, lv_color_hex(0xFFFF88), 0);
    lv_obj_set_style_text_font(e_lbl, &lv_font_montserrat_16, 0);

    s_bar_energy = lv_bar_create(right);
    lv_obj_set_size(s_bar_energy, RIGHT_W - 24, 18);
    lv_bar_set_range(s_bar_energy, 0, 100);
    lv_bar_set_value(s_bar_energy, s_state.energy_x10 / 10, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_energy, lv_color_hex(0x1A3A1A), 0);
    lv_obj_set_style_bg_color(s_bar_energy, lv_color_hex(0x44FF44),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_energy, 2, 0);
    lv_obj_set_style_radius(s_bar_energy, 2, LV_PART_INDICATOR);

    s_label_level = lv_label_create(right);
    lv_label_set_text(s_label_level, "Lv.3 PLANT");
    lv_obj_set_style_text_color(s_label_level, lv_color_hex(0xFFFFAA), 0);
    lv_obj_set_style_text_font(s_label_level, &lv_font_montserrat_16, 0);

    s_label_streak = lv_label_create(right);
    lv_label_set_text(s_label_streak, "Streak: 7d");
    lv_obj_set_style_text_color(s_label_streak, lv_color_hex(0xAAFFAA), 0);
    lv_obj_set_style_text_font(s_label_streak, &lv_font_montserrat_14, 0);

    /* Water button */
    lv_obj_t *btn = lv_btn_create(right);
    lv_obj_set_size(btn, RIGHT_W - 24, 48);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFDD44), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFBB22), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, water_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "WATER");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(btn_lbl);
}

/* Draw everything onto the single scene canvas */
static void draw_scene(void) {
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = 0;

    /* Sky background */
    r.bg_color = lv_color_hex(0x87CEEB);
    r.bg_opa   = LV_OPA_COVER;
    lv_canvas_draw_rect(s_canvas_scene, 0, 0, SCENE_W, SCENE_H, &r);

    /* Grass strip */
    r.bg_color = lv_color_hex(0x5A8A3A);
    lv_canvas_draw_rect(s_canvas_scene, 0, SCENE_H - 44, SCENE_W, 8, &r);

    /* Ground strip */
    r.bg_color = lv_color_hex(0x8B6914);
    lv_canvas_draw_rect(s_canvas_scene, 0, SCENE_H - 36, SCENE_W, 36, &r);

    /* Cloud 1 */
    r.bg_color = lv_color_hex(0xFFFFFF);
    r.bg_opa   = LV_OPA_COVER;
    lv_canvas_draw_rect(s_canvas_scene, s_cloud1_x,      30, 32, 12, &r);
    lv_canvas_draw_rect(s_canvas_scene, s_cloud1_x + 8,  18, 20, 16, &r);
    lv_canvas_draw_rect(s_canvas_scene, s_cloud1_x + 24, 26, 16, 10, &r);

    /* Cloud 2 */
    lv_canvas_draw_rect(s_canvas_scene, s_cloud2_x,      55, 40, 12, &r);
    lv_canvas_draw_rect(s_canvas_scene, s_cloud2_x + 10, 44, 24, 14, &r);

    /* ── Pixel art plant (4-frame sway) ── */
    int8_t sway[] = {-3, 0, 3, 0};
    int16_t sx = (int16_t)(SCENE_W / 2 + sway[s_state.plant_frame]);
    int16_t base_y = SCENE_H - 44; /* top of grass */

    /* Stem */
    r.bg_color = lv_color_hex(0x6AAA2A);
    r.bg_opa   = LV_OPA_COVER;
    lv_canvas_draw_rect(s_canvas_scene, sx - 4, base_y - 90, 8, 90, &r);

    /* Left leaf */
    r.bg_color = lv_color_hex(0x44CC44);
    lv_canvas_draw_rect(s_canvas_scene, sx - 32, base_y - 70, 28, 12, &r);
    r.bg_color = lv_color_hex(0x228822);
    lv_canvas_draw_rect(s_canvas_scene, sx - 32, base_y - 70, 10, 8, &r);

    /* Right leaf */
    r.bg_color = lv_color_hex(0x44CC44);
    lv_canvas_draw_rect(s_canvas_scene, sx + 4, base_y - 55, 28, 12, &r);
    r.bg_color = lv_color_hex(0x228822);
    lv_canvas_draw_rect(s_canvas_scene, sx + 22, base_y - 55, 10, 8, &r);

    /* Flower petals */
    r.bg_color = lv_color_hex(0xFF88AA);
    r.bg_opa   = LV_OPA_COVER;
    lv_canvas_draw_rect(s_canvas_scene, sx - 22, base_y - 118, 16, 16, &r);
    lv_canvas_draw_rect(s_canvas_scene, sx + 6,  base_y - 118, 16, 16, &r);
    lv_canvas_draw_rect(s_canvas_scene, sx - 8,  base_y - 132, 16, 16, &r);
    lv_canvas_draw_rect(s_canvas_scene, sx - 8,  base_y - 104, 16, 16, &r);

    /* Flower center */
    r.bg_color = lv_color_hex(0xFFDD44);
    lv_canvas_draw_rect(s_canvas_scene, sx - 14, base_y - 122, 28, 28, &r);
    r.bg_color = lv_color_hex(0xFF6644);
    lv_canvas_draw_rect(s_canvas_scene, sx - 8,  base_y - 116, 16, 16, &r);

    /* ── Particles ── */
    for (int i = 0; i < 8; i++) {
        if (!s_state.particles[i].active) continue;
        r.bg_color = lv_color_hex(0x44FF88);
        r.bg_opa   = s_state.particles[i].alpha;
        lv_canvas_draw_rect(s_canvas_scene,
                            s_state.particles[i].x,
                            s_state.particles[i].y,
                            6, 6, &r);
    }
}
