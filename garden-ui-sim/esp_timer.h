#ifndef ESP_TIMER_H
#define ESP_TIMER_H

#include <SDL2/SDL.h>
#include <stdint.h>

static inline int64_t esp_timer_get_time(void) {
    return (int64_t)SDL_GetTicks() * 1000;
}

#endif
