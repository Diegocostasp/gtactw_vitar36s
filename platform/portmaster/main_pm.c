#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <GLES2/gl2.h>

#include "so_util.h"

#define LOAD_ADDRESS 0x60000000

so_module gtactw_mod;

const char *pm_data_path(void) {
    const char *env = getenv("GTACTW_DATA_PATH");
    if (env && env[0]) {
        return env;
    }

    return "./data";
}

void fatal_error(const char *fmt, ...) {
    va_list args;

    printf("FATAL: ");

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
    fflush(stdout);

    exit(1);
}

int pm_sdl_init(void);
void pm_sdl_swap(void);

int main(int argc, char **argv) {
    printf("====================================\n");
    printf("GTA CTW PortMaster loader start\n");
    printf("====================================\n");

    printf("Data path: %s\n", pm_data_path());

    char so_path[1024];
    snprintf(so_path, sizeof(so_path), "%s/libCTW.so", pm_data_path());

    if (access(so_path, R_OK) != 0) {
        printf("ERROR: Could not read %s\n", so_path);
        printf("Put libCTW.so in the data folder.\n");
        return 1;
    }

    if (pm_sdl_init() != 0) {
        printf("ERROR: SDL/GLES init failed\n");
        return 1;
    }

    printf("Trying to load: %s\n", so_path);

    int ret = so_load(&gtactw_mod, so_path, LOAD_ADDRESS);

    printf("so_load ret = %d\n", ret);

    if (ret < 0) {
        printf("ERROR: so_load failed\n");
        return 1;
    }

    printf("libCTW.so loaded.\n");
    printf("Next step will be so_relocate/so_resolve.\n");

    while (1) {
        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                return 0;
            }

            if (e.type == SDL_KEYDOWN) {
                return 0;
            }

            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                return 0;
            }
        }

        glViewport(0, 0, 640, 480);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        pm_sdl_swap();

        SDL_Delay(16);
    }

    return 0;
}
