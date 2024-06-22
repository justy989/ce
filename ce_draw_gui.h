#pragma once

#if defined(DISPLAY_GUI)
  #include <SDL.h>
  #include <SDL_ttf.h>

typedef struct{
    SDL_Window* window;
    SDL_Surface* window_surface;
    TTF_Font* font;

    const char* application_name;
    int window_width;
    int window_height;
    int font_point_size;
    int font_line_separation;
}CeGui_t;
#else
typedef struct{
    int dummy;
}CeGui_t;
#endif

struct CeApp_t;

void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui);

