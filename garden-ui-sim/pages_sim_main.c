#include "lvgl.h"
#include "garden_ai.h"
#include "garden_album.h"
#include "garden_device.h"
#include "garden_focus.h"
#include "garden_nav.h"
#include "garden_page.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define DISP_W 1280
#define DISP_H 452

static SDL_Window   *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture  *s_texture;

static int16_t s_mouse_x;
static int16_t s_mouse_y;
static bool    s_mouse_down;

static void sdl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    SDL_Rect rect = { (int)area->x1, (int)area->y1, (int)w, (int)h };

    SDL_UpdateTexture(s_texture, &rect, px_map, (int)(w * 2));
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
    lv_display_flush_ready(disp);
}

static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->point.x = s_mouse_x;
    data->point.y = s_mouse_y;
    data->state = s_mouse_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void build_pages(void) {
    garden_nav_init(lv_screen_active());

    static const garden_page_def_t page_focus  = { "focus",  garden_focus_create,  garden_focus_destroy,  garden_focus_tick,  garden_focus_on_button,  garden_focus_set_active };
    static const garden_page_def_t page_garden = { "garden", garden_page_create,   garden_page_destroy,   garden_page_tick,   garden_page_on_button,   garden_page_set_active };
    static const garden_page_def_t page_ai     = { "ai",     garden_ai_create,     garden_ai_destroy,     garden_ai_tick,     garden_ai_on_button,     garden_ai_set_active };
    static const garden_page_def_t page_device = { "device", garden_device_create, garden_device_destroy, garden_device_tick, garden_device_on_button, garden_device_set_active };
    static const garden_page_def_t page_album  = { "album",  garden_album_create,  garden_album_destroy,  garden_album_tick,  garden_album_on_button,  garden_album_set_active };

    garden_nav_register(0, &page_focus);
    garden_nav_register(1, &page_garden);
    garden_nav_register(2, &page_ai);
    garden_nav_register(3, &page_device);
    garden_nav_register(4, &page_album);

    garden_focus_set_done_cb(garden_page_add_energy);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    s_window = SDL_CreateWindow("Garden Pages Sim - 1280x452",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                DISP_W, DISP_H, SDL_WINDOW_SHOWN);
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        return 1;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        return 1;
    }

    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565,
                                  SDL_TEXTUREACCESS_STREAMING, DISP_W, DISP_H);
    if (!s_texture) {
        fprintf(stderr, "SDL_CreateTexture error: %s\n", SDL_GetError());
        return 1;
    }

    lv_init();
    lv_tick_set_cb(SDL_GetTicks);

    lv_display_t *disp = lv_display_create(DISP_W, DISP_H);
    static uint16_t buf1[DISP_W * 48];
    static uint16_t buf2[DISP_W * 48];
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, sdl_flush_cb);

    lv_indev_t *mouse = lv_indev_create();
    lv_indev_set_type(mouse, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse, mouse_read_cb);

    build_pages();

    bool running = true;
    uint32_t last = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    s_mouse_x = (int16_t)ev.button.x;
                    s_mouse_y = (int16_t)ev.button.y;
                    s_mouse_down = true;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    s_mouse_x = (int16_t)ev.button.x;
                    s_mouse_y = (int16_t)ev.button.y;
                    s_mouse_down = false;
                }
                break;
            case SDL_MOUSEMOTION:
                s_mouse_x = (int16_t)ev.motion.x;
                s_mouse_y = (int16_t)ev.motion.y;
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (ev.key.keysym.sym == SDLK_SPACE) garden_nav_button(0);
                if (ev.key.keysym.sym == SDLK_HOME) garden_nav_go_home(true);
                break;
            default:
                break;
            }
        }

        uint32_t now = SDL_GetTicks();
        uint32_t elapsed = now - last;
        last = now;

        garden_nav_tick(elapsed);
        lv_timer_handler();
        SDL_Delay(8);
    }

    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}
