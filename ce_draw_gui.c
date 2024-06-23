#include "ce_draw_gui.h"
#include "ce_app.h"

#if defined(DISPLAY_GUI)

#define STATUS_LINE_LEN 128

static SDL_Color color_from_index(CeConfigOptions_t* config_options, int index, bool foreground) {
    SDL_Color result;
    if(index == COLOR_DEFAULT){
        if (foreground) {
            index = COLOR_FOREGROUND;
        } else {
            index = COLOR_BACKGROUND;
        }
    }
    result.r = config_options->color_defs[index].red;
    result.g = config_options->color_defs[index].green;
    result.b = config_options->color_defs[index].blue;
    result.a = 255;
    return result;
}

static int64_t _text_pixel_x(int64_t char_pos, CeGui_t* gui) {
    return (char_pos * (gui->font_point_size / 2));
}

static int64_t _text_pixel_y(int64_t char_pos, CeGui_t* gui) {
    return (char_pos * (gui->font_point_size + gui->font_line_separation));
}

static SDL_Rect rect_from_view(CeView_t* view, CeGui_t* gui) {
     SDL_Rect result;
     result.x = _text_pixel_x(view->rect.left, gui);
     result.y = _text_pixel_y(view->rect.top, gui);
     result.w = _text_pixel_x(view->rect.right - view->rect.left, gui);
     result.h = _text_pixel_y(view->rect.bottom - view->rect.top, gui);
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

static void _draw_cursor(CePoint_t* cursor, CeConfigOptions_t* config_options, CeGui_t* gui) {
     SDL_Color color = color_from_index(config_options,
                                        COLOR_FOREGROUND,
                                        false);
     uint32_t color_packed = SDL_MapRGB(gui->window_surface->format,
                                        color.r,
                                        color.g,
                                        color.b);
     SDL_Rect left_wall;
     left_wall.x = _text_pixel_x(cursor->x, gui) - 1;
     left_wall.y = _text_pixel_y(cursor->y, gui) - 1;
     left_wall.w = 1;
     left_wall.h = _text_pixel_y(1, gui) + 1;
     SDL_FillRect(gui->window_surface, &left_wall, color_packed);

     SDL_Rect right_wall;
     right_wall.x = _text_pixel_x(cursor->x + 1, gui) + 1;
     right_wall.y = _text_pixel_y(cursor->y, gui) - 1;
     right_wall.w = 1;
     right_wall.h = _text_pixel_y(1, gui) + 1;
     SDL_FillRect(gui->window_surface, &right_wall, color_packed);

     SDL_Rect top_wall;
     top_wall.x = _text_pixel_x(cursor->x, gui);
     top_wall.y = _text_pixel_y(cursor->y, gui) - 1;
     top_wall.w = _text_pixel_x(1, gui) + 1;
     top_wall.h = 1;
     SDL_FillRect(gui->window_surface, &top_wall, color_packed);

     SDL_Rect bottom_wall;
     bottom_wall.x = _text_pixel_x(cursor->x, gui);
     bottom_wall.y = _text_pixel_y(cursor->y + 1, gui) + 1;
     bottom_wall.w = _text_pixel_x(1, gui) + 1;
     bottom_wall.h = 1;
     SDL_FillRect(gui->window_surface, &bottom_wall, color_packed);
}

static void _draw_view_status(CeView_t* view, CeGui_t* gui, CeVim_t* vim, CeMacros_t* macros,
                              CeConfigOptions_t* config_options) {
     // Draw the status bar.
     SDL_Color ui_bg_color = color_from_index(config_options, config_options->ui_bg_color, false);
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

     // Draw the vim mode if there is one.
     const char* vim_mode_string = "";
     if(vim){
          SDL_Color text_color = color_from_index(config_options, config_options->ui_fg_color, true);
          switch(vim->mode){
          default:
               break;
          case CE_VIM_MODE_NORMAL:
               vim_mode_string = "NORMAL ";
               text_color = color_from_index(config_options, COLOR_BLUE, true);
               break;
          case CE_VIM_MODE_INSERT:
               vim_mode_string = "INSERT ";
               text_color = color_from_index(config_options, COLOR_GREEN, true);
               break;
          case CE_VIM_MODE_VISUAL:
               vim_mode_string = "VISUAL ";
               text_color = color_from_index(config_options, COLOR_YELLOW, true);
               break;
          case CE_VIM_MODE_VISUAL_LINE:
               vim_mode_string = "VISUAL LINE ";
               text_color = color_from_index(config_options, COLOR_BRIGHT_YELLOW, true);
               break;
          case CE_VIM_MODE_VISUAL_BLOCK:
               vim_mode_string = "VISUAL BLOCK ";
               text_color = color_from_index(config_options, COLOR_BRIGHT_YELLOW, true);
               break;
          case CE_VIM_MODE_REPLACE:
               vim_mode_string = "REPLACE ";
               text_color = color_from_index(config_options, COLOR_RED, true);
               break;
          }

          _draw_text_line(vim_mode_string,
                          _text_pixel_x(view->rect.left + 1, gui),
                          _text_pixel_y(view->rect.bottom, gui),
                          &text_color,
                          gui);
     }

     char line_buffer[STATUS_LINE_LEN];
     const char* status_str = ce_buffer_status_get_str(view->buffer->status);
     if(status_str){
         if(ce_macros_is_recording(macros)){
             snprintf(line_buffer, STATUS_LINE_LEN, "%s%s RECORDING %c",
                      status_str, view->buffer->name, macros->recording);
         }else{
             snprintf(line_buffer, STATUS_LINE_LEN, "%s%s", status_str, view->buffer->name);
         }
     }else{
         snprintf(line_buffer, STATUS_LINE_LEN, "%s", view->buffer->name);
     }


     SDL_Color text_color = color_from_index(config_options, config_options->ui_fg_color, true);

    _draw_text_line(line_buffer,
                    _text_pixel_x(view->rect.left + strlen(vim_mode_string) + 1, gui),
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
                       CeDrawColorList_t* syntax_color_list, CeConfigOptions_t* config_options,
                       int terminal_right) {
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

     // If this view doesn't extend to the right side of the window, draw a ui border.
     if(view->rect.right < terminal_right){
         SDL_Color border_color = color_from_index(config_options, config_options->ui_bg_color, false);
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

     CeDrawColorNode_t* current_syntax_color_node = NULL;
     CeDrawColorNode_t* next_syntax_color_node = NULL;

     SDL_Color text_color = color_from_index(config_options, config_options->ui_fg_color, true);

     if(syntax_color_list){
          current_syntax_color_node = syntax_color_list->head;
          if (current_syntax_color_node) {
               next_syntax_color_node = current_syntax_color_node->next;
          }
     }

     int64_t text_pixel_y = _text_pixel_y(view->rect.top, gui);

     for(int64_t draw_y = 0; draw_y < view_height; draw_y++){
          int64_t text_pixel_x = _text_pixel_x(view->rect.left, gui);

          CeRune_t rune = 1;
          int64_t buffer_x = 0;
          int64_t line_index = draw_y + row_min;

          if (!view->buffer->no_highlight_current_line &&
              line_index == view->cursor.y) {
               SDL_Color color = color_from_index(config_options,
                                                  COLOR_BLACK,
                                                  false);
               uint32_t color_packed = SDL_MapRGB(gui->window_surface->format,
                                                  color.r,
                                                  color.g,
                                                  color.b);
               SDL_Rect rect;
               rect.x = _text_pixel_x(view->rect.left, gui);
               rect.y = _text_pixel_y(view->rect.top + draw_y, gui);
               rect.w = _text_pixel_x(view->rect.right - view->rect.left, gui);
               rect.h = _text_pixel_y(1, gui);
               SDL_FillRect(gui->window_surface, &rect, color_packed);
          }

          if (line_index >= view->buffer->line_count) {
               break;
          }

          CePoint_t buffer_point = {buffer_x, line_index};
          CeDrawColorNode_t* original_next_syntax_color_node = next_syntax_color_node;
          while (next_syntax_color_node && !ce_point_after(next_syntax_color_node->point, buffer_point)) {
               original_next_syntax_color_node = next_syntax_color_node;
               next_syntax_color_node = next_syntax_color_node->next;
          }

          if (next_syntax_color_node != original_next_syntax_color_node) {
              current_syntax_color_node = original_next_syntax_color_node;
          }

          const char* line = view->buffer->lines[draw_y + row_min];
          size_t line_buffer_index = 0;

          while(rune > 0 && buffer_x < col_max){
               rune = ce_utf8_decode(line, &rune_len);
               if (buffer_x >= col_min) {
                    if (isprint((int)(rune))) {
                         line_buffer[line_buffer_index] = (char)(rune);
                    } else {
                         line_buffer[line_buffer_index] = ' ';
                    }
                    line_buffer_index++;
               }

               line += rune_len;
               buffer_x++;

               if (current_syntax_color_node) {
                   buffer_point = (CePoint_t){buffer_x, line_index};
                   original_next_syntax_color_node = next_syntax_color_node;
                   while (next_syntax_color_node && !ce_point_after(next_syntax_color_node->point, buffer_point)) {
                        original_next_syntax_color_node = next_syntax_color_node;
                        next_syntax_color_node = next_syntax_color_node->next;
                   }
                   if (next_syntax_color_node != original_next_syntax_color_node) {
                        line_buffer[line_buffer_index] = 0;
                        text_color = color_from_index(config_options, current_syntax_color_node->fg, true);
                        if (line_buffer_index > 0) {
                            if (current_syntax_color_node->bg != COLOR_BACKGROUND &&
                                current_syntax_color_node->bg != COLOR_DEFAULT &&
                                current_syntax_color_node->bg != CE_SYNTAX_USE_CURRENT_COLOR) {
                                 SDL_Color bg_color = color_from_index(config_options,
                                                                       current_syntax_color_node->bg,
                                                                       false);
                                 uint32_t bg_color_packed = SDL_MapRGB(gui->window_surface->format,
                                                                       bg_color.r,
                                                                       bg_color.g,
                                                                       bg_color.b);
                                 SDL_Rect bg_rect;
                                 bg_rect.x = text_pixel_x;
                                 bg_rect.y = text_pixel_y;
                                 bg_rect.w = _text_pixel_x(strlen(line_buffer), gui);
                                 bg_rect.h = _text_pixel_y(1, gui);
                                 SDL_FillRect(gui->window_surface, &bg_rect, bg_color_packed);
                            }
                            _draw_text_line(line_buffer, text_pixel_x, text_pixel_y, &text_color, gui);
                            text_pixel_x += _text_pixel_x(strlen(line_buffer), gui);
                        }
                        line_buffer_index = 0;
                        current_syntax_color_node = original_next_syntax_color_node;
                   }
               }
          }

          // Null terminate the string and draw any leftover characters.
          line_buffer[line_buffer_index] = 0;
          if(line_buffer_index > 0){
               buffer_point = (CePoint_t){buffer_x, line_index};
               if (current_syntax_color_node &&
                   (next_syntax_color_node == NULL || ce_point_after(next_syntax_color_node->point, buffer_point))) {
                    text_color = color_from_index(config_options, current_syntax_color_node->fg, true);

                    if (current_syntax_color_node->bg != COLOR_BACKGROUND &&
                        current_syntax_color_node->bg != COLOR_DEFAULT &&
                        current_syntax_color_node->bg != CE_SYNTAX_USE_CURRENT_COLOR) {
                         SDL_Color bg_color = color_from_index(config_options,
                                                               current_syntax_color_node->bg,
                                                               false);
                         uint32_t bg_color_packed = SDL_MapRGB(gui->window_surface->format,
                                                               bg_color.r,
                                                               bg_color.g,
                                                               bg_color.b);
                         SDL_Rect bg_rect;
                         bg_rect.x = text_pixel_x;
                         bg_rect.y = text_pixel_y;
                         bg_rect.w = _text_pixel_x(strlen(line_buffer), gui);
                         bg_rect.h = _text_pixel_y(1, gui);
                         SDL_FillRect(gui->window_surface, &bg_rect, bg_color_packed);
                    }
               }
               _draw_text_line(line_buffer, text_pixel_x, text_pixel_y, &text_color, gui);
          }

          text_pixel_y += (gui->font_point_size + gui->font_line_separation);
     }

     free(line_buffer);
}

static void _draw_layout(CeLayout_t* layout, CeGui_t* gui, CeVim_t* vim, CeVimVisualData_t* vim_visual,
                         CeMacros_t* macros, CeSyntaxDef_t* syntax_defs,
                         CeConfigOptions_t* config_options, int terminal_right,
                         CeLayout_t* current_layout) {
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          {
               CeAppBufferData_t* buffer_data = layout->view.buffer->app_data;
               CeDrawColorList_t syntax_color_list = {};
               if(buffer_data->syntax_function){
                    CeRangeList_t highlight_ranges = {};
                    if(layout == current_layout){
                         switch(vim->mode){
                         default:
                              break;
                         case CE_VIM_MODE_VISUAL:
                         {
                              CeRange_t range = {vim_visual->point, layout->view.cursor};
                              ce_range_sort(&range);
                              ce_range_list_insert(&highlight_ranges, range.start, range.end);
                         } break;
                         case CE_VIM_MODE_VISUAL_LINE:
                         {
                              CeRange_t range = {vim_visual->point, layout->view.cursor};
                              ce_range_sort(&range);
                              range.start.x = 0;
                              range.end.x = ce_utf8_last_index(layout->view.buffer->lines[range.end.y]) + 1;
                              ce_range_list_insert(&highlight_ranges, range.start, range.end);
                         } break;
                         case CE_VIM_MODE_VISUAL_BLOCK:
                         {
                              CeRange_t range = {vim_visual->point, layout->view.cursor};
                              if(range.start.x > range.end.x){
                                   int64_t tmp = range.start.x;
                                   range.start.x = range.end.x;
                                   range.end.x = tmp;
                              }
                              if(range.start.y > range.end.y){
                                   int64_t tmp = range.start.y;
                                   range.start.y = range.end.y;
                                   range.end.y = tmp;
                              }
                              for(int64_t i = range.start.y; i <= range.end.y; i++){
                                   CePoint_t start = {range.start.x, i};
                                   CePoint_t end = {range.end.x, i};
                                   ce_range_list_insert(&highlight_ranges, start, end);
                              }
                         } break;
                         }
                    }
                    buffer_data->syntax_function(&layout->view, &highlight_ranges, &syntax_color_list, syntax_defs,
                                                 layout->view.buffer->syntax_data);
                    ce_range_list_free(&highlight_ranges);
               }

               _draw_view(&layout->view, gui, vim, macros, &syntax_color_list, config_options, terminal_right);
               ce_draw_color_list_free(&syntax_color_list);
               _draw_view_status(&layout->view,
                                 gui,
                                 (layout == current_layout) ? vim : NULL,
                                 macros,
                                 config_options);
          }
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               _draw_layout(layout->list.layouts[i], gui, vim, vim_visual, macros, syntax_defs, config_options,
                            terminal_right, current_layout);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          _draw_layout(layout->tab.root, gui, vim, vim_visual, macros, syntax_defs, config_options, terminal_right,
                       current_layout);
          break;
     }
}

void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
     CeLayout_t* tab_list_layout = app->tab_list_layout;
     CeLayout_t* tab_layout = tab_list_layout->tab_list.current;

     SDL_Color background_color = color_from_index(&app->config_options,
                                                   COLOR_BACKGROUND,
                                                   false);
     uint32_t background_color_packed = SDL_MapRGB(gui->window_surface->format,
                                                   background_color.r,
                                                   background_color.g,
                                                   background_color.b);
     SDL_FillRect(gui->window_surface, NULL, background_color_packed);

     if(tab_list_layout->tab_list.tab_count > 1){
          // Draw the tab line ui background.
          SDL_Color border_color = color_from_index(&app->config_options,
                                                    app->config_options.ui_bg_color,
                                                    false);
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
                                                  app->config_options.ui_fg_color,
                                                  true);

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

     _draw_layout(tab_layout, gui, &app->vim, &app->visual, &app->macros, app->syntax_defs, &app->config_options, app->terminal_rect.right,
                  tab_layout->tab.current);

     if(app->input_complete_func){
          SDL_Rect view_rect = rect_from_view(&app->input_view, gui);
          SDL_FillRect(gui->window_surface, &view_rect, background_color_packed);

          _draw_view(&app->input_view, gui, NULL, &app->macros, NULL, &app->config_options, app->terminal_rect.right);
          _draw_view_status(&app->input_view,
                            gui,
                            &app->vim,
                            &app->macros,
                            &app->config_options);
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

          // Draw top border
          view_rect.x = _text_pixel_x(app->complete_view.rect.left, gui);
          view_rect.y = _text_pixel_y(app->complete_view.rect.top - 1, gui);
          view_rect.w = _text_pixel_x((app->complete_view.rect.right - app->complete_view.rect.left) + 1, gui);
          view_rect.h = _text_pixel_y(1, gui);
          if (view_rect.y >= 0) {
               SDL_Color border_color = color_from_index(&app->config_options,
                                                         app->config_options.ui_bg_color,
                                                         false);
               uint32_t border_color_packed = SDL_MapRGB(gui->window_surface->format,
                                                         border_color.r,
                                                         border_color.g,
                                                         border_color.b);
               SDL_FillRect(gui->window_surface, &view_rect, border_color_packed);
          }

          _draw_view(&app->complete_view, gui, NULL, &app->macros, NULL, &app->config_options, app->terminal_rect.right);
     }

     if (tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) {

     } else if (app->input_complete_func) {
          CePoint_t cursor = view_cursor_on_screen(&app->input_view,
                                                   app->config_options.tab_width,
                                                   app->config_options.line_number);
          _draw_cursor(&cursor, &app->config_options, gui);
     } else {
          CeView_t* view = &tab_layout->tab.current->view;
          CePoint_t cursor = view_cursor_on_screen(view,
                                                   app->config_options.tab_width,
                                                   app->config_options.line_number);
          _draw_cursor(&cursor, &app->config_options, gui);
     }

     SDL_UpdateWindowSurface(gui->window);
}
#else
void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
}
#endif
