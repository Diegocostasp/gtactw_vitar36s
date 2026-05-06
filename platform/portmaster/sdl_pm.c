#include <stdio.h>

#include <SDL2/SDL.h>
#include <GLES2/gl2.h>

static SDL_Window *window = NULL;
static SDL_GLContext glctx = NULL;

int pm_sdl_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    window = SDL_CreateWindow(
        "GTA CTW",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640,
        480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN
    );

    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    glctx = SDL_GL_CreateContext(window);

    if (!glctx) {
        printf("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GL_SetSwapInterval(1);

    printf("SDL/GLES initialized\n");

    return 0;
}

void pm_sdl_swap(void) {
    if (window) {
        SDL_GL_SwapWindow(window);
    }
}
