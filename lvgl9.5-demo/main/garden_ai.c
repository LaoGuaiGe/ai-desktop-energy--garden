#include "garden_ai.h"
#include <stdio.h>
#include <string.h>

#define DISP_W 1280
#define DISP_H 452

#define FP_SHIFT 8
#define TO_FP(x) ((int32_t)(x) << FP_SHIFT)
#define FROM_FP(x) ((int16_t)(((x) + (1 << (FP_SHIFT - 1))) >> FP_SHIFT))

typedef enum {
    AI_EXPR_NORMAL = 0,
    AI_EXPR_HAPPY,
    AI_EXPR_EXPECT,
    AI_EXPR_CAUTIOUS,
    AI_EXPR_PROUD,
    AI_EXPR_HELPLESS,
    AI_EXPR_SLEEPY,
    AI_EXPR_COUNT
} ai_expr_t;

typedef struct {
    int16_t eye_w;
    int16_t eye_h;
    int16_t eye_gap;
    int16_t eye_y;
    int16_t brow_y;
    int16_t brow_l;
    int16_t brow_r;
    int16_t mouth_w;
    int16_t mouth_curve;
    int16_t left_pct;
    int16_t right_pct;
} face_preset_t;

typedef struct {
    int32_t eye_w;
    int32_t eye_h;
    int32_t eye_gap;
    int32_t eye_y;
    int32_t brow_y;
    int32_t brow_l;
    int32_t brow_r;
    int32_t mouth_w;
    int32_t mouth_curve;
    int32_t left_pct;
    int32_t right_pct;
} face_state_t;

typedef struct {
    lv_obj_t *page;
    lv_obj_t *face_panel;
    lv_obj_t *left_eye;
    lv_obj_t *right_eye;
    lv_obj_t *left_brow;
    lv_obj_t *right_brow;
    lv_obj_t *mouth_l;
    lv_obj_t *mouth_m;
    lv_obj_t *mouth_r;
    lv_obj_t *status_label;
    lv_obj_t *mood_label;
    lv_obj_t *mic_label;
    lv_obj_t *user_text;
    lv_obj_t *ai_text;
    lv_obj_t *tool_text;
    lv_obj_t *dot_label;
    lv_obj_t *action_label;
    lv_obj_t *voice_btn_label;
    face_state_t cur;
    face_state_t tgt;
    ai_expr_t expr;
    bool active;
    bool thinking;
    uint8_t voice_index;
    uint32_t frame_ms;
    uint32_t blink_wait_ms;
    uint32_t blink_timer_ms;
    uint32_t think_timer_ms;
    uint32_t dots_timer_ms;
    uint8_t dots;
    int16_t blink_pct;
} ai_page_t;

static ai_page_t s_ai;

static const face_preset_t s_presets[AI_EXPR_COUNT] = {
    [AI_EXPR_NORMAL]   = { 88, 72, 58,   0,   0,  0,  0, 54,  0, 100, 100 },
    [AI_EXPR_HAPPY]    = { 94, 34, 68,  10,   0,  0,  0, 72, 18, 100, 100 },
    [AI_EXPR_EXPECT]   = { 104, 82, 52, -10,  0,  0,  0, 42,  0, 100, 100 },
    [AI_EXPR_CAUTIOUS] = { 72, 58, 64,   0, -16, -8,  8, 42, -8, 100, 100 },
    [AI_EXPR_PROUD]    = { 96, 38, 62,  -2, -10,  6, -6, 80, 20, 100, 100 },
    [AI_EXPR_HELPLESS] = { 88, 32, 56,  -8, -16, -6,  6, 58, -4, 100, 100 },
    [AI_EXPR_SLEEPY]   = { 88, 12, 54,  12,   0,  0,  0, 46,  0, 100, 100 },
};

static const char *s_voice_samples[] = {
    "Check which plants are mature",
    "Water the selected plot",
    "Fertilize the tomato plot",
    "Harvest mature plants into storage"
};

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);
static lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color);
static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, const char *text, lv_event_cb_t cb, void *data);
static void set_expression(ai_expr_t expr);
static void snap_face(void);
static void update_face(void);
static void set_mood_text(const char *mood, const char *mic, const char *action);
static void start_voice_sim(void);
static void finish_thinking(void);
static void action_event_cb(lv_event_t *e);
static void voice_event_cb(lv_event_t *e);

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x3A6A58), 0);
    lv_obj_set_style_radius(obj, 4, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, const char *text, lv_event_cb_t cb, void *data) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x123B38), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C5B52), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x53F0BA), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, data);

    lv_obj_t *txt = label(btn, text, 0xEAFBF6);
    lv_obj_center(txt);
    return txt;
}

static void fp_from_preset(face_state_t *dst, const face_preset_t *src) {
    dst->eye_w = TO_FP(src->eye_w);
    dst->eye_h = TO_FP(src->eye_h);
    dst->eye_gap = TO_FP(src->eye_gap);
    dst->eye_y = TO_FP(src->eye_y);
    dst->brow_y = TO_FP(src->brow_y);
    dst->brow_l = TO_FP(src->brow_l);
    dst->brow_r = TO_FP(src->brow_r);
    dst->mouth_w = TO_FP(src->mouth_w);
    dst->mouth_curve = TO_FP(src->mouth_curve);
    dst->left_pct = TO_FP(src->left_pct);
    dst->right_pct = TO_FP(src->right_pct);
}

static void set_expression(ai_expr_t expr) {
    if (expr >= AI_EXPR_COUNT) expr = AI_EXPR_NORMAL;
    s_ai.expr = expr;
    fp_from_preset(&s_ai.tgt, &s_presets[expr]);
}

static void snap_face(void) {
    s_ai.cur = s_ai.tgt;
}

static int32_t lerp_fp(int32_t a, int32_t b) {
    return a + (((b - a) * 58) >> 8);
}

static void step_face(void) {
    s_ai.cur.eye_w = lerp_fp(s_ai.cur.eye_w, s_ai.tgt.eye_w);
    s_ai.cur.eye_h = lerp_fp(s_ai.cur.eye_h, s_ai.tgt.eye_h);
    s_ai.cur.eye_gap = lerp_fp(s_ai.cur.eye_gap, s_ai.tgt.eye_gap);
    s_ai.cur.eye_y = lerp_fp(s_ai.cur.eye_y, s_ai.tgt.eye_y);
    s_ai.cur.brow_y = lerp_fp(s_ai.cur.brow_y, s_ai.tgt.brow_y);
    s_ai.cur.brow_l = lerp_fp(s_ai.cur.brow_l, s_ai.tgt.brow_l);
    s_ai.cur.brow_r = lerp_fp(s_ai.cur.brow_r, s_ai.tgt.brow_r);
    s_ai.cur.mouth_w = lerp_fp(s_ai.cur.mouth_w, s_ai.tgt.mouth_w);
    s_ai.cur.mouth_curve = lerp_fp(s_ai.cur.mouth_curve, s_ai.tgt.mouth_curve);
    s_ai.cur.left_pct = lerp_fp(s_ai.cur.left_pct, s_ai.tgt.left_pct);
    s_ai.cur.right_pct = lerp_fp(s_ai.cur.right_pct, s_ai.tgt.right_pct);
}

static void set_rect(lv_obj_t *obj, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
}

static void update_face(void) {
    if (!s_ai.left_eye) return;

    int cx = 155;
    int cy = 145;
    int ew = FROM_FP(s_ai.cur.eye_w);
    int eh = FROM_FP(s_ai.cur.eye_h);
    int gap = FROM_FP(s_ai.cur.eye_gap);
    int ey = FROM_FP(s_ai.cur.eye_y);
    int l_pct = FROM_FP(s_ai.cur.left_pct);
    int r_pct = FROM_FP(s_ai.cur.right_pct);
    int leh = eh * l_pct / 100;
    int reh = eh * r_pct / 100;
    leh = leh * s_ai.blink_pct / 100;
    reh = reh * s_ai.blink_pct / 100;
    if (leh < 5) leh = 5;
    if (reh < 5) reh = 5;

    int lx = cx - gap / 2 - ew;
    int rx = cx + gap / 2;
    int ly = cy + ey - leh / 2;
    int ry = cy + ey - reh / 2;
    int eye_color = s_ai.thinking ? 0xBDFBE7 : 0x53F0BA;
    if (s_ai.expr == AI_EXPR_HELPLESS) eye_color = 0xFFD166;
    if (s_ai.expr == AI_EXPR_PROUD || s_ai.expr == AI_EXPR_HAPPY) eye_color = 0x9DFF9D;

    set_rect(s_ai.left_eye, lx, ly, ew, leh, eye_color, 10);
    set_rect(s_ai.right_eye, rx, ry, ew, reh, eye_color, 10);

    int brow_y = FROM_FP(s_ai.cur.brow_y);
    int brow_l = FROM_FP(s_ai.cur.brow_l);
    int brow_r = FROM_FP(s_ai.cur.brow_r);
    set_rect(s_ai.left_brow, lx - 3, ly - 18 + brow_y + brow_l, ew + 6, 5, 0xEAFBF6, 0);
    set_rect(s_ai.right_brow, rx - 3, ry - 18 + brow_y + brow_r, ew + 6, 5, 0xEAFBF6, 0);
    lv_obj_set_style_transform_rotation(s_ai.left_brow, (int16_t)(-brow_l * 10), 0);
    lv_obj_set_style_transform_rotation(s_ai.right_brow, (int16_t)(brow_r * 10), 0);

    int mw = FROM_FP(s_ai.cur.mouth_w);
    int mc = FROM_FP(s_ai.cur.mouth_curve);
    int mx = cx - mw / 2;
    int my = 245;
    int seg = mw / 3;
    if (seg < 8) seg = 8;
    set_rect(s_ai.mouth_l, mx, my, seg + 2, 6, 0xEAFBF6, 0);
    set_rect(s_ai.mouth_m, mx + seg, my + mc, seg + 2, 6, 0xEAFBF6, 0);
    set_rect(s_ai.mouth_r, mx + seg * 2, my, seg + 2, 6, 0xEAFBF6, 0);
}

static void set_mood_text(const char *mood, const char *mic, const char *action) {
    if (s_ai.mood_label) lv_label_set_text(s_ai.mood_label, mood);
    if (s_ai.mic_label) lv_label_set_text(s_ai.mic_label, mic);
    if (s_ai.action_label) lv_label_set_text(s_ai.action_label, action);
}

static void start_voice_sim(void) {
    const uint8_t idx = s_ai.voice_index % 4;
    char buf[128];

    snprintf(buf, sizeof(buf), "USER VOICE\n%s", s_voice_samples[idx]);
    lv_label_set_text(s_ai.user_text, buf);
    lv_label_set_text(s_ai.ai_text, "GARDENER\nTHINKING");
    lv_label_set_text(s_ai.tool_text, "TOOL RESULT\nwaiting for MCP action");
    lv_label_set_text(s_ai.voice_btn_label, "SIM VOICE");

    s_ai.voice_index = (uint8_t)((idx + 1) % 4);
    s_ai.thinking = true;
    s_ai.think_timer_ms = 1000;
    s_ai.dots = 0;
    set_expression(AI_EXPR_EXPECT);
    set_mood_text("MOOD: LISTENING", "MIC: ASR SIM", "ACTION: PARSING VOICE");

}

static void finish_thinking(void) {
    uint8_t last = (uint8_t)((s_ai.voice_index + 3) % 4);
    const char *reply = "I understand. Garden tools are ready.";
    const char *tool = "TOOL RESULT\nMCP ready: scan/water/fertilize/harvest";

    if (last == 0) {
        reply = "I will scan mature plants first.";
        tool = "TOOL RESULT\nscan_garden_state() ready";
        set_expression(AI_EXPR_CAUTIOUS);
        set_mood_text("MOOD: CHECKING", "MIC: READY", "ACTION: SCAN READY");
    } else if (last == 1) {
        reply = "Okay. I can water the selected plot.";
        tool = "TOOL RESULT\nwater_plot(selected) ready";
        set_expression(AI_EXPR_HAPPY);
        set_mood_text("MOOD: HELPFUL", "MIC: READY", "ACTION: WATER READY");
    } else if (last == 2) {
        reply = "I will check fertilizer before boosting tomato.";
        tool = "TOOL RESULT\nfertilize_plot(tomato) ready";
        set_expression(AI_EXPR_EXPECT);
        set_mood_text("MOOD: CAREFUL", "MIC: READY", "ACTION: FERT READY");
    } else {
        reply = "Mature plants can be harvested into storage.";
        tool = "TOOL RESULT\nharvest_mature_plants() ready";
        set_expression(AI_EXPR_PROUD);
        set_mood_text("MOOD: READY", "MIC: READY", "ACTION: HARVEST READY");
    }

    char ai_buf[160];
    snprintf(ai_buf, sizeof(ai_buf), "GARDENER\n%s", reply);
    lv_label_set_text(s_ai.ai_text, ai_buf);
    lv_label_set_text(s_ai.tool_text, tool);
    s_ai.thinking = false;
}

static void action_event_cb(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name) return;

    char user_buf[96];
    char ai_buf[128];
    char tool_buf[128];

    snprintf(user_buf, sizeof(user_buf), "USER VOICE\nSim command: %s", name);
    lv_label_set_text(s_ai.user_text, user_buf);

    if (strcmp(name, "SCAN") == 0) {
        snprintf(ai_buf, sizeof(ai_buf), "GARDENER\nI will check the garden state.");
        snprintf(tool_buf, sizeof(tool_buf), "TOOL RESULT\nscan_garden_state(): mature=2, dry=1");
        set_expression(AI_EXPR_CAUTIOUS);
        set_mood_text("MOOD: CHECKING", "MIC: READY", "ACTION: SCAN");
    } else if (strcmp(name, "HARVEST") == 0) {
        snprintf(ai_buf, sizeof(ai_buf), "GARDENER\nMature plants harvested in simulation.");
        snprintf(tool_buf, sizeof(tool_buf), "TOOL RESULT\nharvest_mature_plants(): +2 crops");
        set_expression(AI_EXPR_PROUD);
        set_mood_text("MOOD: PROUD", "MIC: READY", "ACTION: HARVEST");
    } else if (strcmp(name, "WATER") == 0) {
        snprintf(ai_buf, sizeof(ai_buf), "GARDENER\nThe selected plot has been watered.");
        snprintf(tool_buf, sizeof(tool_buf), "TOOL RESULT\nwater_plot(selected): done");
        set_expression(AI_EXPR_HAPPY);
        set_mood_text("MOOD: HAPPY", "MIC: READY", "ACTION: WATER");
    } else {
        snprintf(ai_buf, sizeof(ai_buf), "GARDENER\nFertilizer boost applied in simulation.");
        snprintf(tool_buf, sizeof(tool_buf), "TOOL RESULT\nfertilize_plot(selected): boost +33%%");
        set_expression(AI_EXPR_HAPPY);
        set_mood_text("MOOD: HAPPY", "MIC: READY", "ACTION: FERTILIZE");
    }

    lv_label_set_text(s_ai.ai_text, ai_buf);
    lv_label_set_text(s_ai.tool_text, tool_buf);
    s_ai.thinking = false;
}

static void voice_event_cb(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    start_voice_sim();
}

lv_obj_t * garden_ai_create(lv_obj_t *parent) {
    memset(&s_ai, 0, sizeof(s_ai));
    s_ai.active = true;
    s_ai.blink_pct = 100;
    s_ai.blink_wait_ms = 2600;

    s_ai.page = lv_obj_create(parent);
    lv_obj_set_pos(s_ai.page, 0, 0);
    lv_obj_set_size(s_ai.page, DISP_W, DISP_H);
    lv_obj_set_style_bg_color(s_ai.page, lv_color_hex(0x091414), 0);
    lv_obj_set_style_border_width(s_ai.page, 0, 0);
    lv_obj_set_style_radius(s_ai.page, 0, 0);
    lv_obj_set_style_pad_all(s_ai.page, 0, 0);
    lv_obj_clear_flag(s_ai.page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = panel(s_ai.page, 0, 0, DISP_W, 48, 0x0E2322);
    lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
    label(top, "AI GARDENER", 0xEAFBF6);
    lv_obj_set_pos(lv_obj_get_child(top, 0), 22, 16);
    s_ai.mood_label = label(top, "MOOD: NORMAL", 0x53F0BA);
    lv_obj_set_pos(s_ai.mood_label, 220, 16);
    s_ai.mic_label = label(top, "MIC: READY", 0xFFD166);
    lv_obj_set_pos(s_ai.mic_label, 420, 16);
    s_ai.action_label = label(top, "ACTION: IDLE", 0xA8BFA8);
    lv_obj_set_pos(s_ai.action_label, 620, 16);
    s_ai.dot_label = label(top, "", 0x53F0BA);
    lv_obj_set_pos(s_ai.dot_label, 1120, 16);

    s_ai.face_panel = panel(s_ai.page, 16, 64, 310, 330, 0x102128);
    lv_obj_set_style_border_color(s_ai.face_panel, lv_color_hex(0x53F0BA), 0);
    label(s_ai.face_panel, "LITTLE GREEN", 0xBDFBE7);
    lv_obj_set_pos(lv_obj_get_child(s_ai.face_panel, 0), 86, 18);

    s_ai.left_eye = lv_obj_create(s_ai.face_panel);
    s_ai.right_eye = lv_obj_create(s_ai.face_panel);
    s_ai.left_brow = lv_obj_create(s_ai.face_panel);
    s_ai.right_brow = lv_obj_create(s_ai.face_panel);
    s_ai.mouth_l = lv_obj_create(s_ai.face_panel);
    s_ai.mouth_m = lv_obj_create(s_ai.face_panel);
    s_ai.mouth_r = lv_obj_create(s_ai.face_panel);
    lv_obj_t *face_objs[] = { s_ai.left_eye, s_ai.right_eye, s_ai.left_brow, s_ai.right_brow,
                              s_ai.mouth_l, s_ai.mouth_m, s_ai.mouth_r };
    for (int i = 0; i < 7; i++) {
        lv_obj_clear_flag(face_objs[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(face_objs[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(face_objs[i], 0, 0);
    }

    lv_obj_t *chat = panel(s_ai.page, 344, 64, 630, 330, 0x101A24);
    lv_obj_set_style_border_color(chat, lv_color_hex(0x2D6A7A), 0);
    s_ai.user_text = label(chat, "USER VOICE\nWaiting for simulated voice input", 0xFFD166);
    lv_obj_set_pos(s_ai.user_text, 22, 24);
    lv_obj_set_width(s_ai.user_text, 580);
    lv_label_set_long_mode(s_ai.user_text, LV_LABEL_LONG_WRAP);

    s_ai.ai_text = label(chat, "GARDENER\nHi, I am Little Green. I can scan, water, fertilize, and harvest.", 0xEAFBF6);
    lv_obj_set_pos(s_ai.ai_text, 22, 126);
    lv_obj_set_width(s_ai.ai_text, 580);
    lv_label_set_long_mode(s_ai.ai_text, LV_LABEL_LONG_WRAP);

    s_ai.tool_text = label(chat, "TOOL RESULT\nMCP tool simulator ready", 0x8BE8C8);
    lv_obj_set_pos(s_ai.tool_text, 22, 242);
    lv_obj_set_width(s_ai.tool_text, 580);
    lv_label_set_long_mode(s_ai.tool_text, LV_LABEL_LONG_WRAP);

    lv_obj_t *right = panel(s_ai.page, 992, 64, 272, 330, 0x16221B);
    lv_obj_set_style_border_color(right, lv_color_hex(0x537A3A), 0);
    label(right, "ACTIONS", 0xEAFBF6);
    lv_obj_set_pos(lv_obj_get_child(right, 0), 22, 18);

    s_ai.voice_btn_label = button(right, 22, 54, 228, 42, "SIM VOICE", voice_event_cb, NULL);
    button(right, 22, 108, 228, 42, "SCAN GARDEN", action_event_cb, "SCAN");
    button(right, 22, 162, 228, 42, "HARVEST", action_event_cb, "HARVEST");
    button(right, 22, 216, 108, 42, "WATER", action_event_cb, "WATER");
    button(right, 142, 216, 108, 42, "FERT", action_event_cb, "FERT");

    lv_obj_t *hint = label(right, "Voice text is simulated now.\nHardware ASR can feed\nUSER VOICE later.", 0xA8BFA8);
    lv_obj_set_pos(hint, 22, 276);
    lv_obj_set_width(hint, 228);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

    lv_obj_t *bottom = panel(s_ai.page, 16, 410, 1248, 28, 0x0E2322);
    lv_obj_set_style_border_color(bottom, lv_color_hex(0x1E4A3F), 0);
    s_ai.status_label = label(bottom, "ASR SIM READY | MCP SIM READY | future tools: scan_garden_state / harvest / water / fertilize", 0xA8BFA8);
    lv_obj_set_pos(s_ai.status_label, 16, 7);

    set_expression(AI_EXPR_NORMAL);
    snap_face();
    update_face();
    return s_ai.page;
}

void garden_ai_destroy(lv_obj_t *page) {
    if (page) lv_obj_delete(page);
    memset(&s_ai, 0, sizeof(s_ai));
}

void garden_ai_tick(uint32_t elapsed_ms) {
    if (!s_ai.active || !s_ai.page) return;

    s_ai.frame_ms += elapsed_ms;
    s_ai.blink_timer_ms += elapsed_ms;
    s_ai.dots_timer_ms += elapsed_ms;

    if (s_ai.blink_timer_ms >= s_ai.blink_wait_ms && s_ai.blink_timer_ms < s_ai.blink_wait_ms + 80) {
        s_ai.blink_pct = 8;
    } else if (s_ai.blink_timer_ms >= s_ai.blink_wait_ms + 80) {
        s_ai.blink_pct = 100;
        s_ai.blink_timer_ms = 0;
        s_ai.blink_wait_ms = 2200 + ((s_ai.frame_ms / 37) % 1800);
    }

    if (s_ai.thinking) {
        if (s_ai.think_timer_ms > elapsed_ms) {
            s_ai.think_timer_ms -= elapsed_ms;
        } else {
            s_ai.think_timer_ms = 0;
            finish_thinking();
        }
        if (s_ai.dots_timer_ms >= 250) {
            s_ai.dots_timer_ms = 0;
            s_ai.dots = (uint8_t)((s_ai.dots + 1) % 4);
            char dots[12] = "";
            for (uint8_t i = 0; i < s_ai.dots; i++) strcat(dots, ".");
            lv_label_set_text(s_ai.dot_label, dots);
        }
    } else if (s_ai.dot_label) {
        lv_label_set_text(s_ai.dot_label, "");
    }

    step_face();
    update_face();
}

bool garden_ai_on_button(uint8_t type) {
    if (type == 0) {
        start_voice_sim();
        return true;
    }
    return false;
}

void garden_ai_set_active(bool active) {
    s_ai.active = active;
    if (active && s_ai.page) {
        set_expression(AI_EXPR_NORMAL);
    }
}
