#include "ce_draw_gui.h"
#include "ce_app.h"

#if defined(DISPLAY_GUI)

#define STATUS_LINE_LEN 128

static SDL_Color color_from_index(CeConfigOptions_t* config_options, int index) {
    SDL_Color result;
    if (index == -1) { index = COLOR_FOREGROUND; }
    else if (index == -2) { index = COLOR_BACKGROUND; }
    result.r = config_options->color_defs[index].red;
    result.g = config_options->color_defs[index].green;
    result.b = config_options->color_defs[index].blue;
    result.a = 255;
    return result;
}

static int64_t _text_pixel_x(int64_t char_pos, CeGui_t* gui) {
    return char_pos * (gui->font_point_size / 2);
}

static int64_t _text_pixel_y(int64_t char_pos, CeGui_t* gui) {
    return char_pos * (gui->font_point_size + gui->font_line_separation);
}

static SDL_Rect rect_from_view(CeView_t* view, CeGui_t* gui) {
     SDL_Rect result;
     result.x = _text_pixel_x(view->rect.left, gui);
     result.y = _text_pixel_y(view->rect.top, gui);
     result.w = _text_pixel_x(view->rect.right - view->rect.left, gui);
     result.h = _text_pixel_y(view->rect.bottom - view->rect.top, gui);
     return result;
}

static SDL_Rect rect_from_cursor(CePoint_t* cursor, CeGui_t* gui) {
     SDL_Rect result;
     result.x = _text_pixel_x(cursor->x, gui);
     result.y = _text_pixel_y(cursor->y, gui);
     result.w = (gui->font_point_size / 2);
     result.h = gui->font_point_size;
     return result;
}

static void _draw_text_line(const char* line, int64_t pixel_x, int64_t pixel_y,
                            SDL_Color* text_color, CeGui_t* gui) {
     SDL_Surface* surface = TTF_RenderText_Blended(gui->font, line, *text_color);

     SDL_Rect rect;
     rect.x = pixel_x;
     rect.y = pixel_y;
     rect.w = surface->w;
     rect.h = surface->h;

     SDL_BlitSurface(surface, NULL, gui->window_surface, &rect);
     SDL_FreeSurface(surface);
}

static void _draw_view_status(CeView_t* view, CeGui_t* gui, CeVim_t* vim, CeMacros_t* macros,
                              CeConfigOptions_t* config_options) {
     char line_buffer[STATUS_LINE_LEN];

     const char* vim_mode_string = "";
     if(vim){
          switch(vim->mode){
          default:
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


     SDL_Color ui_bg_color = color_from_index(config_options, config_options->ui_bg_color);
     uint32_t background_color_packed = SDL_MapRGB(gui->window_surface->format,
                                                   ui_bg_color.r,
                                                   ui_bg_color.g,
                                                   ui_bg_color.b);
     SDL_Rect status_rect;
     status_rect.x = _text_pixel_x(view->rect.left, gui);
     status_rect.y = _text_pixel_y(view->rect.bottom, gui);
     status_rect.w = _text_pixel_x((view->rect.right - view->rect.left) + 1, gui);
     status_rect.h = _text_pixel_y(1, gui);
     SDL_FillRect(gui->window_surface, &status_rect, background_color_packed);

     SDL_Color text_color = color_from_index(config_options, config_options->ui_fg_color);

    _draw_text_line(line_buffer,
                    _text_pixel_x(view->rect.left, gui),
                    _text_pixel_y(view->rect.bottom, gui),
                    &text_color,
                    gui);

     // Draw the cursor pos in the bottom right.
     int64_t cursor_pose_string_len = snprintf(line_buffer, STATUS_LINE_LEN, "%ld, %ld",
                                               view->cursor.x + 1, view->cursor.y + 1);

    _draw_text_line(line_buffer,
                    _text_pixel_x(view->rect.right, gui) - _text_pixel_x(cursor_pose_string_len, gui),
                    _text_pixel_y(view->rect.bottom, gui),
                    &text_color,
                    gui);
}

static void _draw_view(CeView_t* view, CeGui_t* gui, CeVim_t* vim, CeMacros_t* macros,
                       CeConfigOptions_t* config_options, int terminal_right) {
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

     if (view->rect.right < terminal_right) {
         SDL_Color border_color = color_from_index(config_options, config_options->ui_bg_color);
         uint32_t background_color_packed = SDL_MapRGB(gui->window_surface->format,
                                                       border_color.r,
                                                       border_color.g,
                                                       border_color.b);
         SDL_Rect border_rect;
         border_rect.x = _text_pixel_x(view->rect.right, gui);
         border_rect.y = _text_pixel_y(view->rect.top, gui);
         border_rect.w = _text_pixel_x(1, gui);
         border_rect.h = _text_pixel_y(view_height, gui);
         SDL_FillRect(gui->window_surface, &border_rect, background_color_packed);
     }

     SDL_Color text_color = color_from_index(config_options, config_options->ui_fg_color);

     SDL_Rect text_rect;
     text_rect.x = _text_pixel_x(view->rect.left, gui);
     text_rect.y = _text_pixel_y(view->rect.top, gui);

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
}

static void _draw_layout(CeLayout_t* layout, CeGui_t* gui, CeVim_t* vim, CeMacros_t* macros,
                         CeConfigOptions_t* config_options, int terminal_right,
                         CeLayout_t* current_layout) {
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
         _draw_view(&layout->view, gui, vim, macros, config_options, terminal_right);
         _draw_view_status(&layout->view,
                           gui,
                           (layout == current_layout) ? vim : NULL,
                           macros,
                           config_options);
         break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               _draw_layout(layout->list.layouts[i], gui, vim, macros, config_options,
                            terminal_right, current_layout);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          _draw_layout(layout->tab.root, gui, vim, macros, config_options, terminal_right,
                       current_layout);
          break;
     }
}

void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
     CeLayout_t* tab_list_layout = app->tab_list_layout;
     CeLayout_t* tab_layout = tab_list_layout->tab_list.current;

     SDL_Color background_color = color_from_index(&app->config_options,
                                                   app->syntax_defs[CE_SYNTAX_COLOR_NORMAL].bg);
     uint32_t background_color_packed = SDL_MapRGB(gui->window_surface->format,
                                                   background_color.r,
                                                   background_color.g,
                                                   background_color.b);
     SDL_FillRect(gui->window_surface, NULL, background_color_packed);

     if(tab_list_layout->tab_list.tab_count > 1){
          // Draw the tab line ui background.
          SDL_Color border_color = color_from_index(&app->config_options,
                                                   app->config_options.ui_bg_color);
          uint32_t border_color_packed = SDL_MapRGB(gui->window_surface->format,
                                                    border_color.r,
                                                    border_color.g,
                                                    border_color.b);

          SDL_Rect border_rect;
          border_rect.x = 0;
          border_rect.y = 0;
          border_rect.w = _text_pixel_x(app->terminal_rect.right, gui);
          border_rect.h = _text_pixel_y(1, gui);

          SDL_FillRect(gui->window_surface, &border_rect, border_color_packed);

          SDL_Color text_color = color_from_index(&app->config_options,
                                                  app->config_options.ui_fg_color);

          int64_t text_pixel_x = _text_pixel_x(1, gui);

          for(int64_t i = 0; i < tab_list_layout->tab_list.tab_count; i++){
               const char* buffer_name = tab_list_layout->tab_list.tabs[i]->tab.current->view.buffer->name;

               if(tab_list_layout->tab_list.tabs[i] == tab_list_layout->tab_list.current){
                  SDL_Rect selected_border_rect;
                  selected_border_rect.x = text_pixel_x - _text_pixel_x(1, gui);
                  selected_border_rect.y = 0;
                  selected_border_rect.w = _text_pixel_x(strlen(buffer_name) + 2, gui);
                  selected_border_rect.h = _text_pixel_y(1, gui);

                  SDL_FillRect(gui->window_surface, &selected_border_rect, background_color_packed);
               }

               _draw_text_line(buffer_name, text_pixel_x, 0, &text_color, gui);
               text_pixel_x += _text_pixel_x((strlen(buffer_name) + 2), gui);
          }
     }

     if (tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) {

     } else if (app->input_complete_func) {
          CePoint_t cursor = view_cursor_on_screen(&app->input_view,
                                                   app->config_options.tab_width,
                                                   app->config_options.line_number);
          // TODO: Consolidate with the code below.
          SDL_Rect cursor_rect = rect_from_cursor(&cursor, gui);
          SDL_FillRect(gui->window_surface, &cursor_rect, SDL_MapRGB(gui->window_surface->format, 0x00, 0x00, 0xFF));
     } else {
          CeView_t* view = &tab_layout->tab.current->view;

          CePoint_t cursor = view_cursor_on_screen(view,
                                                   app->config_options.tab_width,
                                                   app->config_options.line_number);
          SDL_Rect cursor_rect = rect_from_cursor(&cursor, gui);
          SDL_FillRect(gui->window_surface, &cursor_rect, SDL_MapRGB(gui->window_surface->format, 0x00, 0x00, 0xFF));
     }

     _draw_layout(tab_layout, gui, &app->vim, &app->macros, &app->config_options, app->terminal_rect.right,
                  tab_layout->tab.current);

     if(app->input_complete_func){
          SDL_Rect view_rect = rect_from_view(&app->input_view, gui);
          SDL_FillRect(gui->window_surface, &view_rect, background_color_packed);

          _draw_view(&app->input_view, gui, NULL, &app->macros, &app->config_options, app->terminal_rect.right);
     }

     CeComplete_t* complete = ce_app_is_completing(app);
     if(complete && tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW && app->complete_list_buffer->line_count &&
        strlen(app->complete_list_buffer->lines[0])){
          CeLayout_t* view_layout = tab_layout->tab.current;
          app->complete_view.rect.left = view_layout->view.rect.left;
          app->complete_view.rect.right = view_layout->view.rect.right - 1;
          if(app->input_complete_func){
               app->complete_view.rect.bottom = app->input_view.rect.top;
          }else{
               app->complete_view.rect.bottom = view_layout->view.rect.bottom - 1;
          }
          int64_t lines_to_show = app->complete_list_buffer->line_count;
          if(lines_to_show > app->config_options.completion_line_limit){
               lines_to_show = app->config_options.completion_line_limit;
          }
          app->complete_view.rect.top = app->complete_view.rect.bottom - lines_to_show;
          if(app->complete_view.rect.top <= view_layout->view.rect.top){
               app->complete_view.rect.top = view_layout->view.rect.top + 1; // account for current view's status bar
          }
          app->complete_view.buffer = app->complete_list_buffer;
          app->complete_view.cursor.y = app->complete_list_buffer->cursor_save.y;
          app->complete_view.cursor.x = 0;
          app->complete_view.scroll.y = 0;
          app->complete_view.scroll.x = 0;
          ce_view_follow_cursor(&app->complete_view, 0, 0, 0); // NOTE: I don't think anyone wants their settings applied here

          SDL_Rect view_rect = rect_from_view(&app->complete_view, gui);
          SDL_FillRect(gui->window_surface, &view_rect, background_color_packed);

          _draw_view(&app->complete_view, gui, NULL, &app->macros, &app->config_options, app->terminal_rect.right);
     }

     SDL_UpdateWindowSurface(gui->window);
}
#else
void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
}
#endif
