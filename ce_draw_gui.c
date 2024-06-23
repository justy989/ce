#include "ce_draw_gui.h"
#include "ce_app.h"

#if defined(DISPLAY_GUI)

#define STATUS_LINE_LEN 128

static void _draw_view_status(CeView_t* view, CeGui_t* gui, CeVim_t* vim, CeMacros_t* macros) {
     char line_buffer[STATUS_LINE_LEN];

     const char* vim_mode_string = NULL;
     if(vim){
          switch(vim->mode){
          default:
               vim_mode_string = "";
               break;
          case CE_VIM_MODE_NORMAL:
               vim_mode_string = "NORMAL ";
               break;
          case CE_VIM_MODE_INSERT:
               vim_mode_string = "INSERT ";
               break;
          case CE_VIM_MODE_VISUAL:
               vim_mode_string = "VISUAL ";
               break;
          case CE_VIM_MODE_VISUAL_LINE:
               vim_mode_string = "VISUAL LINE ";
               break;
          case CE_VIM_MODE_VISUAL_BLOCK:
               vim_mode_string = "VISUAL BLOCK ";
               break;
          case CE_VIM_MODE_REPLACE:
               vim_mode_string = "REPLACE ";
               break;
          }
     }

     const char* status_str = ce_buffer_status_get_str(view->buffer->status);
     if (status_str) {
         if(ce_macros_is_recording(macros)){
             snprintf(line_buffer, STATUS_LINE_LEN, "%s%s%s RECORDING %c", vim_mode_string,
                      status_str, view->buffer->name, macros->recording);
         } else {
             snprintf(line_buffer, STATUS_LINE_LEN, "%s%s%s", vim_mode_string, status_str,
                      view->buffer->name);
         }
     } else {
         snprintf(line_buffer, STATUS_LINE_LEN, "%s%s", vim_mode_string, view->buffer->name);
     }

     SDL_Color text_color;
     text_color.r = 255;
     text_color.g = 255;
     text_color.b = 255;
     text_color.a = 255;

     SDL_Rect text_rect;
     text_rect.x = view->rect.left * (gui->font_point_size / 2);
     text_rect.y = view->rect.bottom * (gui->font_point_size + gui->font_line_separation);
     SDL_Surface* line_surface = TTF_RenderText_Blended(gui->font, line_buffer, text_color);
     text_rect.w = line_surface->w;
     text_rect.h = line_surface->h;

     SDL_BlitSurface(line_surface, NULL, gui->window_surface, &text_rect);
     SDL_FreeSurface(line_surface);
}

static void _draw_view(CeView_t* view, CeGui_t* gui, CeVim_t* vim, CeMacros_t* macros) {
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
     text_rect.h = 0;

     for(int64_t y = 0; y < view_height; y++){
         CeRune_t rune = 1;
         int64_t x = 0;
         int64_t line_index = y + row_min;
         if (line_index >= view->buffer->line_count) {
             break;
         }
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

     _draw_view_status(view, gui, vim, macros);
}

static void _draw_layout(CeLayout_t* layout, CeGui_t* gui, CeVim_t* vim, CeMacros_t* macros) {
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
     {
         _draw_view(&layout->view, gui, vim, macros);
     } break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               _draw_layout(layout->list.layouts[i], gui, vim, macros);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          _draw_layout(layout->tab.root, gui, vim, macros);
          break;
     }
}

void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
    CeLayout_t* tab_list_layout = app->tab_list_layout;
    CeLayout_t* tab_layout = tab_list_layout->tab_list.current;

    SDL_FillRect(gui->window_surface, NULL, SDL_MapRGB(gui->window_surface->format, 0x00, 0x00, 0x00));

    if (tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) {

    } else if (app->input_complete_func) {
        CePoint_t cursor = view_cursor_on_screen(&app->input_view,
                                                 app->config_options.tab_width,
                                                 app->config_options.line_number);
        // TODO: Consolidate with the code below.
        SDL_Rect cursor_rect;
        cursor_rect.x = (gui->font_point_size / 2) * cursor.x;
        cursor_rect.y = (gui->font_point_size + gui->font_line_separation) * cursor.y;
        cursor_rect.w = (gui->font_point_size / 2);
        cursor_rect.h = gui->font_point_size;
        SDL_FillRect(gui->window_surface, &cursor_rect, SDL_MapRGB(gui->window_surface->format, 0xFF, 0xFF, 0xFF));
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


    if(app->input_complete_func){
        _draw_view(&app->input_view, gui, &app->vim, &app->macros);
    }

    _draw_layout(tab_layout, gui, &app->vim, &app->macros);

    SDL_UpdateWindowSurface(gui->window);
}
#else
void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
}
#endif
