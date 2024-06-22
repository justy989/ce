#include "ce_draw_gui.h"
#include "ce_app.h"

#if defined(DISPLAY_GUI)
static void _draw_view(CeView_t* view, CeGui_t* gui) {
     if(view->buffer->line_count == 0){
         return;
     }

     int64_t view_width = ce_view_width(view);
     int64_t view_height = ce_view_height(view);
     int64_t row_min = view->scroll.y;
     int64_t col_min = view->scroll.x;
     int64_t col_max = col_min + view_width;

     int64_t rune_len = 0;

     int64_t line_buffer_len = (col_max - col_min) + 1;
     char* line_buffer = malloc(line_buffer_len);

     // TODO: Encode these and the font point size and window dimentions in CeGui_t.
     SDL_Color text_color;
     text_color.r = 255;
     text_color.g = 255;
     text_color.b = 255;
     text_color.a = 255;
     SDL_Rect text_rect;
     text_rect.x = (view->rect.left * (gui->font_point_size / 2));
     text_rect.y = view->rect.top * (gui->font_point_size + gui->font_line_separation);
     text_rect.w = 0;
     text_rect.y = 0;

     for(int64_t y = 0; y < view_height; y++){
         CeRune_t rune = 1;
         int64_t x = 0;
         const char* line = view->buffer->lines[y + row_min];

         while(rune > 0 && x < col_max){
              rune = ce_utf8_decode(line, &rune_len);
              if (x >= col_min) {
                  if (isprint((int)(rune))) {
                      line_buffer[x - col_min] = (char)(rune);
                  } else {
                      line_buffer[x - col_min] = ' ';
                  }
              }
              line += rune_len;
              x++;
         }

         if (x >= col_min) {
             line_buffer[x - col_min] = 0;
         } else {
             line_buffer[0] = 0;
         }

         if (strlen(line_buffer) > 0) {
             SDL_Surface* line_surface = TTF_RenderText_Blended(gui->font, line_buffer, text_color);

             text_rect.w = line_surface->w;
             text_rect.h = line_surface->h;
             SDL_BlitSurface(line_surface, NULL, gui->window_surface, &text_rect);

             SDL_FreeSurface(line_surface);
         }

         text_rect.y += (gui->font_point_size + gui->font_line_separation);
     }

     free(line_buffer);
}

static void _draw_layout(CeLayout_t* layout, CeGui_t* gui) {
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
     {
         _draw_view(&layout->view, gui);
     } break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               _draw_layout(layout->list.layouts[i], gui);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          _draw_layout(layout->tab.root, gui);
          break;
     }
}

void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
    CeLayout_t* tab_list_layout = app->tab_list_layout;
    CeLayout_t* tab_layout = tab_list_layout->tab_list.current;

    SDL_FillRect(gui->window_surface, NULL, SDL_MapRGB(gui->window_surface->format, 0x00, 0x00, 0x00));

    if (tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) {
    } else if (app->input_complete_func) {
    } else {
        CeView_t* view = &tab_layout->tab.current->view;

        CePoint_t cursor = view_cursor_on_screen(view,
                                                 app->config_options.tab_width,
                                                 app->config_options.line_number);
        SDL_Rect cursor_rect;
        cursor_rect.x = (gui->font_point_size / 2) * cursor.x;
        cursor_rect.y = (gui->font_point_size + gui->font_line_separation) * cursor.y;
        cursor_rect.w = (gui->font_point_size / 2);
        cursor_rect.h = gui->font_point_size;
        SDL_FillRect(gui->window_surface, &cursor_rect, SDL_MapRGB(gui->window_surface->format, 0xFF, 0xFF, 0xFF));
    }

    _draw_layout(tab_layout, gui);

    SDL_UpdateWindowSurface(gui->window);
}
#else
void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
}
#endif
