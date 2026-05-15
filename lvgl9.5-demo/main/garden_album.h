#ifndef GARDEN_ALBUM_H
#define GARDEN_ALBUM_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

lv_obj_t * garden_album_create(lv_obj_t *parent);
void       garden_album_destroy(lv_obj_t *page);
void       garden_album_tick(uint32_t elapsed_ms);
bool       garden_album_on_button(uint8_t type);
void       garden_album_set_active(bool active);

#endif
