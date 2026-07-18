#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font8x8_basic.h"

#define AXIS_THRESHOLD 16000
#define GLYPH_SCALE 3
#define GLYPH_PX (8 * GLYPH_SCALE)

static void draw_char(SDL_Renderer *renderer, char c, int x, int y) {
    if ((unsigned char)c > 127) {
        c = '?';
    }
    const char *glyph = font8x8_basic[(int)c];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((glyph[row] >> col) & 1) {
                SDL_Rect px = {x + col * GLYPH_SCALE, y + row * GLYPH_SCALE, GLYPH_SCALE, GLYPH_SCALE};
                SDL_RenderFillRect(renderer, &px);
            }
        }
    }
}

static void draw_text(SDL_Renderer *renderer, const char *text, int x, int y) {
    int cursor_x = x;
    for (const char *p = text; *p; p++) {
        draw_char(renderer, *p, cursor_x, y);
        cursor_x += GLYPH_PX;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <label>[|command] [label[|command]...]\n", argv[0]);
        return 2;
    }

    int choice_count = argc - 1;
    char **labels = malloc(choice_count * sizeof(char *));
    char **commands = malloc(choice_count * sizeof(char *));
    for (int i = 0; i < choice_count; i++) {
        char *arg = argv[i + 1];
        char *pipe = strchr(arg, '|');
        if (pipe) {
            *pipe = '\0';
            labels[i] = arg;
            commands[i] = pipe + 1;
        } else {
            labels[i] = arg;
            commands[i] = arg;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }

    SDL_DisplayMode display_mode;
    if (SDL_GetCurrentDisplayMode(0, &display_mode) != 0) {
        display_mode.w = 640;
        display_mode.h = 480;
    }

    SDL_Window *window = SDL_CreateWindow("Picker",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        display_mode.w, display_mode.h,
        SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }

    SDL_GameController *controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                break;
            }
        }
    }

    int selected = 0;
    int axis_neutral = 1;
    int running = 1;
    int accepted = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = 0;
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        selected = (selected - 1 + choice_count) % choice_count;
                    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        selected = (selected + 1) % choice_count;
                    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        accepted = 1;
                        running = 0;
                    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                        running = 0;
                    }
                    break;
                case SDL_CONTROLLERAXISMOTION:
                    if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                        if (axis_neutral && event.caxis.value < -AXIS_THRESHOLD) {
                            selected = (selected - 1 + choice_count) % choice_count;
                            axis_neutral = 0;
                        } else if (axis_neutral && event.caxis.value > AXIS_THRESHOLD) {
                            selected = (selected + 1) % choice_count;
                            axis_neutral = 0;
                        } else if (event.caxis.value > -AXIS_THRESHOLD && event.caxis.value < AXIS_THRESHOLD) {
                            axis_neutral = 1;
                        }
                    }
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_UP) {
                        selected = (selected - 1 + choice_count) % choice_count;
                    } else if (event.key.keysym.sym == SDLK_DOWN) {
                        selected = (selected + 1) % choice_count;
                    } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE) {
                        accepted = 1;
                        running = 0;
                    } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                        running = 0;
                    }
                    break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        int row_height = GLYPH_PX + 24;
        int total_height = choice_count * row_height;
        int start_y = (display_mode.h - total_height) / 2;

        for (int i = 0; i < choice_count; i++) {
            int text_width = (int)strlen(labels[i]) * GLYPH_PX;
            int x = (display_mode.w - text_width) / 2;
            int y = start_y + i * row_height;
            if (i == selected) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
            }
            draw_text(renderer, labels[i], x, y);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (controller) {
        SDL_GameControllerClose(controller);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (accepted) {
        printf("%s\n", commands[selected]);
    }

    free(labels);
    free(commands);

    return accepted ? 0 : 1;
}
