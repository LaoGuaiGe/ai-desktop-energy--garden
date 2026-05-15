#ifndef GARDEN_AI_H
#define GARDEN_AI_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

lv_obj_t * garden_ai_create(lv_obj_t *parent);
void       garden_ai_destroy(lv_obj_t *page);
void       garden_ai_tick(uint32_t elapsed_ms);
bool       garden_ai_on_button(uint8_t type);
void       garden_ai_set_active(bool active);

#endif
