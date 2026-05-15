#ifndef GARDEN_PAGE_H
#define GARDEN_PAGE_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Page contract functions — called by garden_nav */
lv_obj_t * garden_page_create(lv_obj_t *parent);
void       garden_page_destroy(lv_obj_t *page);
void       garden_page_tick(uint32_t elapsed_ms);
bool       garden_page_on_button(uint8_t type);

/* Cross-page API */
void garden_page_add_energy(uint16_t energy_x10);  /* focus done → energy reward */
void garden_page_set_active(bool active);           /* nav layer: pause canvas during drag */

#endif
