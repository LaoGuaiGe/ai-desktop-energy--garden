#ifndef ESP_HEAP_CAPS_H
#define ESP_HEAP_CAPS_H

#include <stddef.h>

#define MALLOC_CAP_INTERNAL 0x01
#define MALLOC_CAP_SPIRAM   0x02

static inline size_t heap_caps_get_free_size(unsigned int caps) {
    (void)caps;
    return 0;
}

#endif
