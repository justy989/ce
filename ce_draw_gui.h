#pragma once

// TODO:
// - display message
// - select parent layout, I don't remember what you do with that though.
// - regex search highlight
// - Display when line extends passed the view

#if defined(DISPLAY_GUI)
  #include <SDL2/SDL.h>
  #include <SDL2/SDL_ttf.h>

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

int gui_load_font(CeGui_t* gui, const char* font_filepath, int font_point_size, int font_line_separation);

#else
typedef struct{
    int dummy;
}CeGui_t;
#endif

struct CeApp_t;

void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui);

