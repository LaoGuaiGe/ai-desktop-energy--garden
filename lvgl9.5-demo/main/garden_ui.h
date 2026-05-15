#ifndef GARDEN_UI_H
#define GARDEN_UI_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Call once after lv_init() and display registration */
void garden_ui_init(void);

/* Encoder: +1 = right turn, -1 = left turn */
void garden_ui_encoder_event(int delta);

/* Button: 0=short press, 1=long press(>=500ms), 2=double click */
void garden_ui_button_event(uint8_t type);

/* Touch/mouse: screen coordinates + pressed state */
void garden_ui_touch_event(int16_t x, int16_t y, bool pressed);

/* Call every frame from main loop; elapsed_ms = ms since last call */
void garden_ui_tick(uint32_t elapsed_ms);

/* Set plant growth stage (0=seed, 1=sprout, 2=seedling, 3=mature, 4=bloom, 5=crystal) */
void garden_ui_set_stage(uint8_t stage);

#endif /* GARDEN_UI_H */
