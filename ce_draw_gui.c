#include "ce_draw_gui.h"
#include "ce_app.h"

#include <string.h>

#if defined(DISPLAY_GUI)

#define STATUS_LINE_LEN 128

static SDL_Color color_from_index(CeConfigOptions_t* config_options, int index, bool foreground){
    SDL_Color result;
    if(index == CE_COLOR_DEFAULT){
        if (foreground) {
            index = CE_COLOR_FOREGROUND;
        } else {
            index = CE_COLOR_BACKGROUND;
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
     result.w = _text_pixel_x((view->rect.right - view->rect.left) + 1, gui);
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
                                        CE_COLOR_FOREGROUND,
                                        false);
     uint32_t color_packed = SDL_MapRGB(gui->window_surface->format,
                                        color.r,
                                        color.g,
                                        color.b);
     SDL_Rect left_wall;
     left_wall.x = _text_pixel_x(cursor->x, gui) - 1;
     left_wall.y = _text_pixel_y(cursor->y, gui) - 1;
     left_wall.w = 1;
     left_wall.h = _text_pixel_y(1, gui) + 2;
     SDL_FillRect(gui->window_surface, &left_wall, color_packed);

     SDL_Rect right_wall;
     right_wall.x = _text_pixel_x(cursor->x + 1, gui) + 1;
     right_wall.y = _text_pixel_y(cursor->y, gui) - 1;
     right_wall.w = 1;
     right_wall.h = _text_pixel_y(1, gui) + 2;
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

static void _append_search_highlight_ranges(const char* pattern, CeLayout_t* layout, CeVim_t* vim, CeRangeList_t* range_list) {
     int64_t pattern_len = ce_utf8_strlen(pattern);
     int64_t min = layout->view.scroll.y;
     int64_t max = min + (layout->view.rect.bottom - layout->view.rect.top);
     int64_t clamp_max = (layout->view.buffer->line_count - 1);
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);

     if(vim->search_mode == CE_VIM_SEARCH_MODE_FORWARD ||
        vim->search_mode == CE_VIM_SEARCH_MODE_BACKWARD){
          for(int64_t i = min; i <= max; i++){
               const char* itr = layout->view.buffer->lines[i];
               const char* match = strstr(itr, pattern);
               while(match){
                    CePoint_t start = {ce_utf8_strlen_between(layout->view.buffer->lines[i], match) - 1, i};
                    CePoint_t end = {start.x + (pattern_len - 1), i};
                    ce_range_list_insert(range_list, start, end);
                    itr = match + pattern_len;
                    match = strstr(itr, pattern);
               }
          }
     }else if(vim->search_mode == CE_VIM_SEARCH_MODE_REGEX_FORWARD ||
              vim->search_mode == CE_VIM_SEARCH_MODE_REGEX_BACKWARD){
          CeRegex_t regex = NULL;
          CeRegexResult_t regex_result = ce_regex_init(pattern,
                                                       &regex);
          if(regex_result.error_message == NULL){
               for(int64_t i = min; i <= max; i++){
                    char* itr = layout->view.buffer->lines[i];
                    int64_t prev_end_x = 0;
                    while(itr){
                         regex_result = ce_regex_match(regex, itr);
                         if(regex_result.error_message != NULL){
                              free(regex_result.error_message);
                              break;
                         }else{
                              if(regex_result.match_length > 0){
                                   CePoint_t start = {prev_end_x + regex_result.match_start, i};
                                   CePoint_t end = {start.x + (regex_result.match_length - 1), i};
                                   ce_range_list_insert(range_list, start, end);
                                   itr = ce_utf8_iterate_to(itr, regex_result.match_start + regex_result.match_length);
                                   prev_end_x = end.x + 1;
                              }else{
                                   break;
                              }
                         }
                    }
               }
               ce_regex_free(regex);
          }else{
               free(regex_result.error_message);
          }

     }
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
               text_color = color_from_index(config_options, CE_COLOR_BLUE, true);
               break;
          case CE_VIM_MODE_INSERT:
               vim_mode_string = "INSERT ";
               text_color = color_from_index(config_options, CE_COLOR_GREEN, true);
               break;
          case CE_VIM_MODE_VISUAL:
               vim_mode_string = "VISUAL ";
               text_color = color_from_index(config_options, CE_COLOR_YELLOW, true);
               break;
          case CE_VIM_MODE_VISUAL_LINE:
               vim_mode_string = "VISUAL LINE ";
               text_color = color_from_index(config_options, CE_COLOR_BRIGHT_YELLOW, true);
               break;
          case CE_VIM_MODE_VISUAL_BLOCK:
               vim_mode_string = "VISUAL BLOCK ";
               text_color = color_from_index(config_options, CE_COLOR_BRIGHT_YELLOW, true);
               break;
          case CE_VIM_MODE_REPLACE:
               vim_mode_string = "REPLACE ";
               text_color = color_from_index(config_options, CE_COLOR_RED, true);
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
     int64_t cursor_pose_string_len = snprintf(line_buffer, STATUS_LINE_LEN, "%" PRId64 ", %" PRId64, view->cursor.x + 1, view->cursor.y + 1);

    _draw_text_line(line_buffer,
                    _text_pixel_x(view->rect.right, gui) - _text_pixel_x(cursor_pose_string_len, gui),
                    _text_pixel_y(view->rect.bottom, gui),
                    &text_color,
                    gui);
}

static void _draw_view(CeView_t* view, CeGui_t* gui, CeVim_t* vim,
                       CeDrawColorList_t* syntax_color_list, CeSyntaxDef_t* syntax_defs,
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

     // figure out how wide the line number margin needs to be
     int line_number_size = 0;
     if(!view->buffer->no_line_numbers){
          line_number_size = ce_line_number_column_width(config_options->line_number,
                                                         view->buffer->line_count,
                                                         view->rect.top,
                                                         view->rect.bottom);
          if(line_number_size){
               col_max -= line_number_size;
               line_number_size--;
          }
     }

     CeDrawColorNode_t* current_syntax_color_node = NULL;
     CeDrawColorNode_t* next_syntax_color_node = NULL;

     SDL_Color text_color = color_from_index(config_options, CE_COLOR_FOREGROUND, true);

     CeDrawColorNode_t default_node;

     if(syntax_color_list){
          current_syntax_color_node = syntax_color_list->head;
          if (current_syntax_color_node) {
               next_syntax_color_node = current_syntax_color_node->next;
               if (current_syntax_color_node->point.x != 0 ||
                   current_syntax_color_node->point.y != 0) {
                    // If the first point in the buffer doesn't have a syntax node, then
                    // create a default and set it to the head of the list. This fixes buffers
                    // with no syntax highlighting until later in the buffer.
                    default_node.fg = CE_COLOR_FOREGROUND;
                    default_node.bg = CE_COLOR_BACKGROUND;
                    default_node.point.x = 0;
                    default_node.point.y = 0;
                    default_node.next = current_syntax_color_node;
                    next_syntax_color_node = current_syntax_color_node;
                    current_syntax_color_node = &default_node;
               }
          }
     }

     int64_t text_pixel_y = _text_pixel_y(view->rect.top, gui);

     for(int64_t draw_y = 0; draw_y < view_height; draw_y++){
          int64_t text_pixel_x = _text_pixel_x(view->rect.left, gui);

          CeRune_t rune = 1;
          int64_t buffer_x = 0;
          int64_t line_index = draw_y + row_min;

          // Highlight the current line if enabled.
          if (!view->buffer->no_highlight_current_line &&
              line_index == view->cursor.y) {
               SDL_Color color = color_from_index(config_options,
                                                  ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_CURRENT_LINE, CE_COLOR_BLACK),
                                                  false);
               uint32_t color_packed = SDL_MapRGB(gui->window_surface->format,
                                                  color.r,
                                                  color.g,
                                                  color.b);
               SDL_Rect rect;
               rect.x = _text_pixel_x(view->rect.left, gui);
               rect.y = _text_pixel_y(view->rect.top + draw_y, gui);
               rect.w = _text_pixel_x((view->rect.right - view->rect.left) + 1, gui);
               rect.h = _text_pixel_y(1, gui);
               SDL_FillRect(gui->window_surface, &rect, color_packed);
          }

          if (line_index >= view->buffer->line_count) {
               break;
          }

          // Add line numbers if enabled.
          if(!view->buffer->no_line_numbers && config_options->line_number > 0){
               SDL_Color color = color_from_index(config_options,
                                                  ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_LINE_NUMBER, CE_COLOR_BLACK),
                                                  true);
               // TODO: Respect background color.
               int current_line_number = line_index + 1;
               if(config_options->line_number == CE_LINE_NUMBER_RELATIVE ||
                  (config_options->line_number == CE_LINE_NUMBER_ABSOLUTE_AND_RELATIVE && view->cursor.y != line_index)){
                    current_line_number = abs((int)(view->cursor.y - line_index));
               }
               snprintf(line_buffer, line_buffer_len, "%*d ", line_number_size, current_line_number);
               _draw_text_line(line_buffer, text_pixel_x, text_pixel_y, &color, gui);
               text_pixel_x += _text_pixel_x(strlen(line_buffer), gui);
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
               if(buffer_x >= col_min){
                    if(isprint((int)(rune))){
                         line_buffer[line_buffer_index] = (char)(rune);
                         line_buffer_index++;
                    }else if(rune == CE_TAB){
                         for (int64_t t = 0; t < config_options->tab_width; t++) {
                              line_buffer[line_buffer_index] = ' ';
                              line_buffer_index++;
                         }
                    }else{
                         line_buffer[line_buffer_index] = ' ';
                         line_buffer_index++;
                    }
               }

               line += rune_len;
               buffer_x++;

               if(current_syntax_color_node) {
                   buffer_point = (CePoint_t){buffer_x, line_index};
                   original_next_syntax_color_node = next_syntax_color_node;
                   while(next_syntax_color_node &&
                         !ce_point_after(next_syntax_color_node->point, buffer_point)){
                        original_next_syntax_color_node = next_syntax_color_node;
                        next_syntax_color_node = next_syntax_color_node->next;
                   }
                   if(next_syntax_color_node != original_next_syntax_color_node){
                        line_buffer[line_buffer_index] = 0;
                        text_color = color_from_index(config_options, current_syntax_color_node->fg, true);
                        if(line_buffer_index > 0){
                            if(current_syntax_color_node->bg != CE_COLOR_BACKGROUND &&
                                current_syntax_color_node->bg != CE_COLOR_DEFAULT &&
                                current_syntax_color_node->bg != CE_SYNTAX_USE_CURRENT_COLOR){
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
               if(current_syntax_color_node &&
                  !ce_point_after(current_syntax_color_node->point, buffer_point) &&
                  (next_syntax_color_node == NULL || ce_point_after(next_syntax_color_node->point, buffer_point))){
                    text_color = color_from_index(config_options, current_syntax_color_node->fg, true);

                    if (current_syntax_color_node->bg != CE_COLOR_BACKGROUND &&
                        current_syntax_color_node->bg != CE_COLOR_DEFAULT &&
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

     free(line_buffer);
}

static void _draw_layout(CeLayout_t* layout, CeGui_t* gui, CeVim_t* vim, CeVimVisualData_t* vim_visual,
                         CeMacros_t* macros, CeSyntaxDef_t* syntax_defs,
                         CeConfigOptions_t* config_options, int terminal_right,
                         const char* highlight_pattern, CeLayout_t* current_layout) {
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
                    if(highlight_pattern){
                        _append_search_highlight_ranges(highlight_pattern, layout, vim, &highlight_ranges);
                    }
                    buffer_data->syntax_function(&layout->view, &highlight_ranges, &syntax_color_list, syntax_defs,
                                                 layout->view.buffer->syntax_data);
                    ce_range_list_free(&highlight_ranges);
               }

               _draw_view(&layout->view, gui, vim, &syntax_color_list, syntax_defs,
                          config_options, terminal_right);
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
               _draw_layout(layout->list.layouts[i], gui, vim, vim_visual, macros, syntax_defs,
                            config_options, terminal_right, highlight_pattern, current_layout);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          _draw_layout(layout->tab.root, gui, vim, vim_visual, macros, syntax_defs, config_options,
                       terminal_right, highlight_pattern, current_layout);
          break;
     }
}

void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
     CeLayout_t* tab_list_layout = app->tab_list_layout;
     CeLayout_t* tab_layout = tab_list_layout->tab_list.current;

     SDL_Color background_color = color_from_index(&app->config_options,
                                                   CE_COLOR_BACKGROUND,
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

     // Pass down a highlight if one is present.
     const char* highlight_pattern = NULL;
     if(app->highlight_search) {
          if(app->input_complete_func == search_input_complete_func &&
             app->input_view.buffer->line_count > 0 &&
             app->input_view.buffer->lines[0][0] > 0){
               highlight_pattern = app->input_view.buffer->lines[0];
          }else{
               const CeVimYank_t* yank = app->vim.yanks + ce_vim_register_index('/');
               if(yank->text){
                   highlight_pattern = yank->text;
               }
          }
     }

     _draw_layout(tab_layout, gui, &app->vim, &app->visual, &app->macros, app->syntax_defs, &app->config_options, app->terminal_rect.right,
                  highlight_pattern, tab_layout->tab.current);

     if(app->input_complete_func){
          ce_view_follow_cursor(&app->input_view, 0, 0, 0); // NOTE: I don't think anyone wants their settings applied here
          SDL_Rect view_rect = rect_from_view(&app->input_view, gui);
          SDL_FillRect(gui->window_surface, &view_rect, background_color_packed);

          _draw_view(&app->input_view, gui, NULL, NULL, app->syntax_defs,
                     &app->config_options, app->terminal_rect.right);
          _draw_view_status(&app->input_view,
                            gui,
                            &app->vim,
                            &app->macros,
                            &app->config_options);

          // Draw a top border over the input view.
          view_rect.x = _text_pixel_x(app->input_view.rect.left, gui);
          view_rect.y = _text_pixel_y(app->input_view.rect.top - 1, gui);
          view_rect.w = _text_pixel_x((app->input_view.rect.right - app->input_view.rect.left) + 1, gui);
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
     }

     CeComplete_t* complete = ce_app_is_completing(app);
     if(complete && tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW && app->complete_list_buffer->line_count &&
        strlen(app->complete_list_buffer->lines[0])){
          CeLayout_t* view_layout = tab_layout->tab.current;
          app->complete_view.rect.left = view_layout->view.rect.left;
          app->complete_view.rect.right = view_layout->view.rect.right;
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

          CeDrawColorList_t draw_color_list = {};
          CeRangeList_t highlight_ranges = {};
          CeAppBufferData_t* buffer_data = app->complete_view.buffer->app_data;
          if (buffer_data->syntax_function) {
               buffer_data->syntax_function(&app->complete_view, &highlight_ranges, &draw_color_list, app->syntax_defs,
                                            app->complete_view.buffer->syntax_data);
               ce_range_list_free(&highlight_ranges);
          }

          _draw_view(&app->complete_view, gui, NULL, &draw_color_list,
                     app->syntax_defs, &app->config_options, app->terminal_rect.right);
     }

     if(app->clangd_completion.start.x >= 0 &&
        app->clangd_completion.start.y >= 0){
          SDL_Rect view_rect = rect_from_view(&app->clangd_completion.view, gui);

          SDL_Color border_color = color_from_index(&app->config_options,
                                                    app->config_options.ui_bg_color,
                                                    false);
          uint32_t border_color_packed = SDL_MapRGB(gui->window_surface->format,
                                                    border_color.r,
                                                    border_color.g,
                                                    border_color.b);
          SDL_Rect border_rect = view_rect;
          border_rect.x -= _text_pixel_x(1, gui);
          border_rect.y -= _text_pixel_x(1, gui); // intentionally x !
          border_rect.w += _text_pixel_x(1, gui);
          border_rect.h += _text_pixel_x(1, gui); // intentionally x !

          SDL_FillRect(gui->window_surface, &border_rect, border_color_packed);
          SDL_FillRect(gui->window_surface, &view_rect, background_color_packed);

          app->clangd_completion.view.cursor.x = 0;
          app->clangd_completion.view.cursor.y = ce_complete_current_match(app->clangd_completion.complete);
          app->clangd_completion.view.scroll.y = 0;
          app->clangd_completion.view.scroll.x = 0;
          ce_view_follow_cursor(&app->clangd_completion.view, 0, 0, 0);

          CeDrawColorList_t draw_color_list = {};
          CeRangeList_t highlight_ranges = {};
          CeAppBufferData_t* buffer_data = app->clangd_completion.buffer->app_data;
          if (buffer_data->syntax_function) {
               buffer_data->syntax_function(&app->clangd_completion.view, &highlight_ranges, &draw_color_list, app->syntax_defs,
                                            app->clangd_completion.buffer->syntax_data);
               ce_range_list_free(&highlight_ranges);
          }

          _draw_view(&app->clangd_completion.view, gui, NULL, &draw_color_list,
                     app->syntax_defs, &app->config_options, app->terminal_rect.right);
     }

     if(app->message_mode){
          CeDrawColorList_t draw_color_list = {};
          ce_draw_color_list_insert(&draw_color_list, CE_COLOR_RED, app->config_options.ui_bg_color, (CePoint_t){0, 0});
          _draw_view(&app->message_view, gui, NULL, &draw_color_list,
                     app->syntax_defs, &app->config_options, app->terminal_rect.right);
     }

     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW){
          // TODO: Draw selecting the parent layout.
     }else if(app->input_complete_func) {
          CePoint_t cursor = view_cursor_on_screen(&app->input_view,
                                                   app->config_options.tab_width,
                                                   app->config_options.line_number);
          _draw_cursor(&cursor, &app->config_options, gui);
     }else{
          CeView_t* view = &tab_layout->tab.current->view;
          CePoint_t cursor = view_cursor_on_screen(view,
                                                   app->config_options.tab_width,
                                                   app->config_options.line_number);
          _draw_cursor(&cursor, &app->config_options, gui);
     }

     SDL_UpdateWindowSurface(gui->window);
}

int gui_load_font(CeGui_t* gui, const char* font_filepath, int font_point_size, int font_line_separation) {
     if (gui->font == NULL) {
          TTF_CloseFont(gui->font);
     }

     ce_log("loading font: %s at size: %d\n", font_filepath, font_point_size);

     gui->font_point_size = font_point_size;
     gui->font_line_separation = font_line_separation;
     gui->font = TTF_OpenFont(font_filepath, gui->font_point_size);
     if (gui->font == NULL) {
         ce_log("TTF_OpenFont() failed: %s\n", TTF_GetError());
         return 1;
     }

     return 0;
}

#else
void ce_draw_gui(struct CeApp_t* app, CeGui_t* gui) {
}
#endif
