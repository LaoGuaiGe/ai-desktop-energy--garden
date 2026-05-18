#include "garden_device.h"
#include <stdio.h>
#include <string.h>

#define DISP_W 1280
#define DISP_H 452
#define CAT_COUNT 7
#define ITEM_COUNT 5

typedef struct {
    const char *name;
    const char *items[ITEM_COUNT];
    const char *values[ITEM_COUNT];
} device_category_t;

typedef struct {
    lv_obj_t *page;
    lv_obj_t *cat_rows[CAT_COUNT];
    lv_obj_t *cat_labels[CAT_COUNT];
    lv_obj_t *item_labels[ITEM_COUNT];
    lv_obj_t *value_labels[ITEM_COUNT];
    lv_obj_t *title_label;
    lv_obj_t *status_label;
    lv_obj_t *hint_label;
    uint8_t selected;
    uint32_t tick_ms;
    bool active;
    bool pulse;
} device_page_t;

static device_page_t s_dev;

static const device_category_t s_categories[CAT_COUNT] = {
    {
        "DISPLAY",
        { "Brightness", "Night Mode", "Animation", "Sleep Timer", "FPS Target" },
        { "70%", "OFF", "HIGH", "10 MIN", "45 FPS" }
    },
    {
        "GARDEN",
        { "Eco Mode", "Weather FX", "Particles", "Mature Alert", "Auto Hint" },
        { "FLOWER", "ON", "MED", "ON", "ON" }
    },
    {
        "FOCUS",
        { "Default Time", "Finish Alert", "Vibration", "Reward Rule", "Auto Harvest" },
        { "25 MIN", "ON", "TRIPLE", "1 MIN = 1 TOM", "ASK" }
    },
    {
        "AI GARDENER",
        { "AI Mode", "Voice Input", "MCP Control", "Personality", "Fallback" },
        { "AUTO", "ASR SIM", "CONFIRM", "GENTLE", "LOCAL" }
    },
    {
        "NETWORK",
        { "WiFi", "Weather API", "AI API", "BLE Pairing", "Last Sync" },
        { "SIM ONLINE", "READY", "LOCAL", "WAITING", "12:40" }
    },
    {
        "HARDWARE",
        { "Host Link", "USB-A1", "USB-A2", "USB-C", "Sensors" },
        { "ON", "ON", "OFF", "ON", "TEMP/HUM OK" }
    },
    {
        "ABOUT",
        { "Project", "Board", "Display", "Graphics", "Build" },
        { "AI GARDEN HUB", "ESP32-P4 + C6", "1280x452 DSI", "LVGL 9.5", "SIM v0.1" }
    }
};

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);
static lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color);
static void build_categories(lv_obj_t *parent);
static void build_setting_rows(lv_obj_t *parent);
static void refresh_category(void);
static void category_event_cb(lv_event_t *e);
static bool select_next_category(void);

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x3A5A65), 0);
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

static void build_categories(lv_obj_t *parent) {
    for (int i = 0; i < CAT_COUNT; i++) {
        lv_obj_t *row = panel(parent, 0, 48 + i * 44, 178, 36, 0x102024);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(row, category_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        s_dev.cat_rows[i] = row;

        s_dev.cat_labels[i] = label(row, s_categories[i].name, 0xA8BFA8);
        lv_obj_set_pos(s_dev.cat_labels[i], 14, 11);
    }
}

static void build_setting_rows(lv_obj_t *parent) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        lv_obj_t *row = panel(parent, 24, 74 + i * 54, 548, 40, 0x101A24);
        lv_obj_set_style_border_color(row, lv_color_hex(0x24465A), 0);
        s_dev.item_labels[i] = label(row, "", 0xEAFBF6);
        lv_obj_set_pos(s_dev.item_labels[i], 16, 13);
        s_dev.value_labels[i] = label(row, "", 0x53F0BA);
        lv_obj_align(s_dev.value_labels[i], LV_ALIGN_RIGHT_MID, -18, 0);
    }
}

static void refresh_category(void) {
    const device_category_t *cat = &s_categories[s_dev.selected];
    char title[64];
    snprintf(title, sizeof(title), "%s SETTINGS", cat->name);
    lv_label_set_text(s_dev.title_label, title);

    for (int i = 0; i < CAT_COUNT; i++) {
        bool sel = i == s_dev.selected;
        lv_obj_set_style_bg_color(s_dev.cat_rows[i], lv_color_hex(sel ? 0x1C5B52 : 0x102024), 0);
        lv_obj_set_style_border_color(s_dev.cat_rows[i], lv_color_hex(sel ? 0x53F0BA : 0x27424A), 0);
        lv_obj_set_style_text_color(s_dev.cat_labels[i], lv_color_hex(sel ? 0xEAFBF6 : 0xA8BFA8), 0);
    }

    for (int i = 0; i < ITEM_COUNT; i++) {
        lv_label_set_text(s_dev.item_labels[i], cat->items[i]);
        lv_label_set_text(s_dev.value_labels[i], cat->values[i]);
    }

    if (s_dev.selected == 6) {
        lv_label_set_text(s_dev.hint_label, "Open source desktop companion: USB hub + living digital garden.");
    } else if (s_dev.selected == 3) {
        lv_label_set_text(s_dev.hint_label, "MCP can later allow the gardener to scan, water, fertilize, and harvest.");
    } else {
        lv_label_set_text(s_dev.hint_label, "Values are simulated now. Later they can be persisted in NVS.");
    }
}

static void category_event_cb(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    s_dev.selected = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (s_dev.selected >= CAT_COUNT) s_dev.selected = 0;
    refresh_category();
}

static bool select_next_category(void) {
    s_dev.selected = (uint8_t)((s_dev.selected + 1) % CAT_COUNT);
    refresh_category();
    return true;
}

lv_obj_t * garden_device_create(lv_obj_t *parent) {
    memset(&s_dev, 0, sizeof(s_dev));
    s_dev.active = true;

    s_dev.page = lv_obj_create(parent);
    lv_obj_set_pos(s_dev.page, 0, 0);
    lv_obj_set_size(s_dev.page, DISP_W, DISP_H);
    lv_obj_set_style_bg_color(s_dev.page, lv_color_hex(0x091318), 0);
    lv_obj_set_style_border_width(s_dev.page, 0, 0);
    lv_obj_set_style_radius(s_dev.page, 0, 0);
    lv_obj_set_style_pad_all(s_dev.page, 0, 0);
    lv_obj_clear_flag(s_dev.page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = panel(s_dev.page, 0, 0, DISP_W, 48, 0x0D2028);
    lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
    label(top, "DEVICE CENTER", 0xEAFBF6);
    lv_obj_set_pos(lv_obj_get_child(top, 0), 22, 16);
    lv_obj_t *chip = label(top, "ESP32-P4 + C6  |  LVGL 9.5  |  1280x452 MIPI DSI", 0x53F0BA);
    lv_obj_set_pos(chip, 220, 16);
    lv_obj_t *ver = label(top, "SIM v0.1", 0xFFD166);
    lv_obj_align(ver, LV_ALIGN_RIGHT_MID, -26, 0);

    lv_obj_t *left = panel(s_dev.page, 16, 64, 202, 330, 0x0E1C22);
    lv_obj_set_style_border_color(left, lv_color_hex(0x2D5A6A), 0);
    label(left, "SETTINGS", 0xEAFBF6);
    lv_obj_set_pos(lv_obj_get_child(left, 0), 18, 18);
    build_categories(left);

    lv_obj_t *center = panel(s_dev.page, 236, 64, 596, 330, 0x101923);
    lv_obj_set_style_border_color(center, lv_color_hex(0x2D5A7A), 0);
    s_dev.title_label = label(center, "DISPLAY SETTINGS", 0xEAFBF6);
    lv_obj_set_pos(s_dev.title_label, 24, 22);
    build_setting_rows(center);
    s_dev.hint_label = label(center, "Values are simulated now. Later they can be persisted in NVS.", 0xA8BFA8);
    lv_obj_set_pos(s_dev.hint_label, 24, 292);
    lv_obj_set_width(s_dev.hint_label, 548);
    lv_label_set_long_mode(s_dev.hint_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *right = panel(s_dev.page, 850, 64, 414, 330, 0x151E19);
    lv_obj_set_style_border_color(right, lv_color_hex(0x537A3A), 0);
    label(right, "PROJECT INFO", 0xEAFBF6);
    lv_obj_set_pos(lv_obj_get_child(right, 0), 22, 20);

    lv_obj_t *brand = label(right, "AI DESKTOP ENERGY GARDEN", 0x53F0BA);
    lv_obj_set_pos(brand, 22, 60);
    lv_obj_t *desc = label(right,
        "A living USB-HUB companion.\n"
        "Garden growth visualizes focus,\n"
        "device activity, weather, and AI\n"
        "gardener actions.",
        0xD8F7D8);
    lv_obj_set_pos(desc, 22, 92);
    lv_obj_set_width(desc, 360);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);

    lv_obj_t *stack = label(right,
        "MVP: flower ecosystem\n"
        "Input: encoder + button + voice\n"
        "AI: cloud or local fallback\n"
        "Storage: NVS settings later",
        0xA8BFA8);
    lv_obj_set_pos(stack, 22, 206);
    lv_obj_set_width(stack, 360);
    lv_label_set_long_mode(stack, LV_LABEL_LONG_WRAP);

    lv_obj_t *bottom = panel(s_dev.page, 16, 410, 1248, 28, 0x0D2028);
    lv_obj_set_style_border_color(bottom, lv_color_hex(0x24465A), 0);
    s_dev.status_label = label(bottom, "USB-A1 ON | USB-A2 OFF | USB-C ON | HOST ON | WIFI SIM | AI LOCAL | TEMP 24C HUM 58%", 0xA8BFA8);
    lv_obj_set_pos(s_dev.status_label, 16, 7);

    refresh_category();
    return s_dev.page;
}

void garden_device_destroy(lv_obj_t *page) {
    if (page) lv_obj_delete(page);
    memset(&s_dev, 0, sizeof(s_dev));
}

void garden_device_tick(uint32_t elapsed_ms) {
    if (!s_dev.active || !s_dev.page) return;
    s_dev.tick_ms += elapsed_ms;
    if (s_dev.tick_ms >= 900) {
        s_dev.tick_ms = 0;
        s_dev.pulse = !s_dev.pulse;
        lv_obj_set_style_text_color(s_dev.status_label, lv_color_hex(s_dev.pulse ? 0xBDFBE7 : 0xA8BFA8), 0);
    }
}

bool garden_device_on_button(uint8_t type) {
    if (type == 0) return select_next_category();
    return false;
}

void garden_device_set_active(bool active) {
    s_dev.active = active;
}
