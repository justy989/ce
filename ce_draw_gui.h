#pragma once

#if defined(DISPLAY_GUI)
  #include <SDL.h>
  #include <SDL_ttf.h>

  #define COLOR_BLACK 0
  #define COLOR_RED 1
  #define COLOR_GREEN 2
  #define COLOR_YELLOW 3
  #define COLOR_BLUE 4
  #define COLOR_MAGENTA 5
  #define COLOR_CYAN 6
  #define COLOR_WHITE 7

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

