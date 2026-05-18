#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 16 (RGB565) */
#define LV_COLOR_DEPTH 16

/* Memory */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE   (2 * 1024U * 1024U)

/* HAL tick — use SDL_GetTicks() */
#define LV_TICK_CUSTOM                  1
#define LV_TICK_CUSTOM_INCLUDE          <SDL2/SDL.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR    (SDL_GetTicks())

/* Fonts */
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/* Widgets */
#define LV_USE_CANVAS  1
#define LV_USE_BAR     1
#define LV_USE_LABEL   1
#define LV_USE_BTN     1
#define LV_USE_FLEX    1

/* Animation */
#define LV_USE_ANIM    1

/* Logging */
#define LV_USE_LOG          1
#define LV_LOG_LEVEL        LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF       1

/* Draw */
#define LV_DRAW_BUF_STRIDE_ALIGN  1

#endif /* LV_CONF_H */
