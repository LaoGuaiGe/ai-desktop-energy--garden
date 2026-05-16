#ifndef GARDEN_NAV_H
#define GARDEN_NAV_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Page contract — every page module implements these 5 functions */
typedef struct {
    const char *name;
    lv_obj_t *  (*create)(lv_obj_t *parent);
    void        (*destroy)(lv_obj_t *page);
    void        (*tick)(uint32_t elapsed_ms);
    bool        (*on_button)(uint8_t type);  /* 0=short press, 1=long press; return true if handled */
    void        (*set_active)(bool active);  /* optional — nav layer notifies page when it becomes active/inactive */
} garden_page_def_t;

#define GARDEN_NAV_MAX_PAGES 8
#define GARDEN_NAV_HOME_INDEX 1   /* garden page is HOME */

void garden_nav_init(lv_obj_t *screen);
void garden_nav_register(int index, const garden_page_def_t *def);
void garden_nav_go_home(bool animate);
void garden_nav_tick(uint32_t elapsed_ms);
void garden_nav_button(uint8_t type);       /* 0=short, 1=long */
bool garden_nav_was_dragging(void);         /* true if last touch was a drag — pages skip clicks */

/* Reserved for future encoder hardware */
void garden_nav_encoder(int delta);
void garden_nav_encoder_press(void);

#endif
