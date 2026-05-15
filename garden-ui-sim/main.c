#include "lvgl.h"
#include "garden_ui.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define DISP_W 1280
#define DISP_H 452

static SDL_Window   *s_window   = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture  = NULL;

/* Shared mouse state for LVGL indev read callback */
static int16_t  s_mouse_x     = 0;
static int16_t  s_mouse_y     = 0;
static bool     s_mouse_down  = false;

/* ── LVGL display flush callback ── */
static void sdl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    SDL_Rect rect = { (int)area->x1, (int)area->y1, (int)w, (int)h };
    /* RGB565 = 2 bytes per pixel, stride = w * 2 */
    SDL_UpdateTexture(s_texture, &rect, px_map, (int)(w * 2));
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
    lv_display_flush_ready(disp);
}

/* ── LVGL pointer indev read callback ── */
static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->point.x = s_mouse_x;
    data->point.y = s_mouse_y;
    data->state   = s_mouse_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ── Button timing state ── */
static bool     s_enter_down    = false;
static uint32_t s_enter_down_ms = 0;
static uint32_t s_last_enter_up = 0;

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* SDL2 init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    s_window = SDL_CreateWindow(
        "Garden UI Sim — 1280x452",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        DISP_W, DISP_H,
        SDL_WINDOW_SHOWN
    );
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        return 1;
    }

    s_renderer = SDL_CreateRenderer(
        s_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    s_texture = SDL_CreateTexture(
        s_renderer,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        DISP_W, DISP_H
    );

    /* LVGL init */
    lv_init();

    /* Display */
    lv_display_t *disp = lv_display_create(DISP_W, DISP_H);
    static uint16_t buf1[DISP_W * 40];
    static uint16_t buf2[DISP_W * 40];
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, sdl_flush_cb);

    /* Pointer indev (mouse = touch) */
    lv_indev_t *mouse_indev = lv_indev_create();
    lv_indev_set_type(mouse_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse_indev, mouse_read_cb);

    /* Build garden UI */
    garden_ui_init();

    uint32_t last_tick = SDL_GetTicks();
    bool running = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {

            case SDL_QUIT:
                running = false;
                break;

            /* Mouse → touch */
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    s_mouse_x    = (int16_t)ev.button.x;
                    s_mouse_y    = (int16_t)ev.button.y;
                    s_mouse_down = true;
                    garden_ui_touch_event(s_mouse_x, s_mouse_y, true);
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    s_mouse_x    = (int16_t)ev.button.x;
                    s_mouse_y    = (int16_t)ev.button.y;
                    s_mouse_down = false;
                    garden_ui_touch_event(s_mouse_x, s_mouse_y, false);
                }
                break;

            case SDL_MOUSEMOTION:
                s_mouse_x = (int16_t)ev.motion.x;
                s_mouse_y = (int16_t)ev.motion.y;
                if (ev.motion.state & SDL_BUTTON_LMASK) {
                    garden_ui_touch_event(s_mouse_x, s_mouse_y, true);
                }
                break;

            /* Keyboard → encoder + button */
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_LEFT:
                    garden_ui_encoder_event(-1);
                    break;
                case SDLK_RIGHT:
                    garden_ui_encoder_event(+1);
                    break;
                case SDLK_RETURN:
                    if (!s_enter_down) {
                        s_enter_down    = true;
                        s_enter_down_ms = SDL_GetTicks();
                    }
                    break;
                case SDLK_ESCAPE:
                    running = false;
                    break;
                }
                break;

            case SDL_KEYUP:
                if (ev.key.keysym.sym == SDLK_RETURN && s_enter_down) {
                    uint32_t held = SDL_GetTicks() - s_enter_down_ms;
                    uint32_t now  = SDL_GetTicks();
                    if (held >= 500) {
                        garden_ui_button_event(1);          /* long press */
                    } else if (now - s_last_enter_up < 300) {
                        garden_ui_button_event(2);          /* double click */
                    } else {
                        garden_ui_button_event(0);          /* short press */
                    }
                    s_last_enter_up = now;
                    s_enter_down    = false;
                }
                break;
            }
        }

        uint32_t now     = SDL_GetTicks();
        uint32_t elapsed = now - last_tick;
        last_tick = now;

        garden_ui_tick(elapsed);
        lv_timer_handler();

        SDL_Delay(16); /* ~60 fps */
    }

    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}
