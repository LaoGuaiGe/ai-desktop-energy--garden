#ifndef GARDEN_FOCUS_H
#define GARDEN_FOCUS_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Page contract — called by garden_nav */
lv_obj_t * garden_focus_create(lv_obj_t *parent);
void       garden_focus_destroy(lv_obj_t *page);
void       garden_focus_tick(uint32_t elapsed_ms);
bool       garden_focus_on_button(uint8_t type);

/* Cross-page callback: nav layer wires this to garden_page_add_energy */
typedef void (*garden_focus_done_cb_t)(uint16_t reward_x10);
void garden_focus_set_done_cb(garden_focus_done_cb_t cb);

/* Optional: nav layer notifies focus page of active state */
void garden_focus_set_active(bool active);

#endif
