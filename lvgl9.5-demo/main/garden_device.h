#ifndef GARDEN_DEVICE_H
#define GARDEN_DEVICE_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

lv_obj_t * garden_device_create(lv_obj_t *parent);
void       garden_device_destroy(lv_obj_t *page);
void       garden_device_tick(uint32_t elapsed_ms);
bool       garden_device_on_button(uint8_t type);
void       garden_device_set_active(bool active);

#endif
