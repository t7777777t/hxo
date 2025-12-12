#include "engine.h"
#include <SDL3/SDL.h>

static SDL_Window* g_window = nullptr;

extern "C" {

int engine_init(const char* title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    g_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    SDL_Log("Engine initialized: %s (%dx%d)", title, width, height);
    return 0;
}

void engine_shutdown(void) {
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
    SDL_Log("Engine shutdown complete");
}

bool engine_poll_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            return true;
        }
    }
    return false;
}

uint64_t engine_get_ticks(void) {
    return SDL_GetTicks();
}

} // extern "C"
