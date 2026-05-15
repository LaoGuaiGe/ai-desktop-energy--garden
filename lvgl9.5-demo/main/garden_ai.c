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
