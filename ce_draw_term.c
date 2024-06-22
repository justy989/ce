#include "ce_draw_term.h"
#include "ce_app.h"

#include <stdlib.h>
#include <string.h>
#include <ncurses.h>

static void _draw_view(CeView_t* view, int64_t tab_width, CeLineNumber_t line_number, CeVisualLineDisplayType_t visual_line_display_type,
                       CeMultipleCursors_t* multiple_cursors, CeDrawColorList_t* draw_color_list, CeColorDefs_t* color_defs, CeSyntaxDef_t* syntax_defs,
                       int64_t terminal_right, CeRune_t show_line_extends_passed_view_as){
     int64_t view_width = ce_view_width(view);
     int64_t view_height = ce_view_height(view);
     int64_t row_min = view->scroll.y;
     int64_t col_min = view->scroll.x;
     int64_t col_max = col_min + view_width;

     char tab_str[tab_width + 1];
     memset(tab_str, ' ', tab_width);
     tab_str[tab_width] = 0;

     CeDrawColorNode_t* draw_color_node = draw_color_list->head;

     // figure out how wide the line number margin needs to be
     int line_number_size = 0;
     if(!view->buffer->no_line_numbers){
          line_number_size = ce_line_number_column_width(line_number, view->buffer->line_count, view->rect.top, view->rect.bottom);
          if(line_number_size){
               col_max -= line_number_size;
               line_number_size--;
          }
     }

     if(view->buffer->line_count >= 0){
          int last_bg = COLOR_DEFAULT;
          int last_fg = COLOR_DEFAULT;

          for(int64_t y = 0; y < view_height; y++){
               int64_t index = 0;
               int64_t x = 0;
               int64_t rune_len = 0;
               int64_t line_index = y + row_min;
               CeRune_t rune = 1;
               int64_t real_y = y + view->scroll.y;

               move(view->rect.top + y, view->rect.left);

               if(!view->buffer->no_line_numbers && line_number){
                    int fg = COLOR_DEFAULT;
                    int bg = COLOR_DEFAULT;
                    if(draw_color_node){
                         fg = draw_color_node->fg;
                         bg = draw_color_node->bg;
                    }
                    fg = ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_LINE_NUMBER, fg);
                    bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_LINE_NUMBER, bg);
                    int change_color_pair = ce_color_def_get(color_defs, fg, bg);
                    attron(COLOR_PAIR(change_color_pair));
                    int value = real_y + 1;
                    if(line_number == CE_LINE_NUMBER_RELATIVE || (line_number == CE_LINE_NUMBER_ABSOLUTE_AND_RELATIVE && view->cursor.y != real_y)){
                         value = abs((int)(view->cursor.y - real_y));
                    }
                    printw("%*d ", line_number_size, value);
               }

               standend();
               if(!view->buffer->no_highlight_current_line && real_y == view->cursor.y){
                    int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_CURRENT_LINE, COLOR_DEFAULT);
                    int change_color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, bg);
                    attron(COLOR_PAIR(change_color_pair));
               }else if(draw_color_node && ce_point_after((CePoint_t){index, y + view->scroll.y}, draw_color_node->point)){
                    int change_color_pair = ce_color_def_get(color_defs, draw_color_node->fg, draw_color_node->bg);
                    attron(COLOR_PAIR(change_color_pair));
               }

               if(line_index < view->buffer->line_count){
                    const char* line = view->buffer->lines[y + row_min];

                    while(rune > 0){
                         rune = ce_utf8_decode(line, &rune_len);

                         // check if we need to move to the next color
                         while(draw_color_node && !ce_point_after(draw_color_node->point, (CePoint_t){index, real_y})){
                              int bg = draw_color_node->bg;
                              if(!view->buffer->no_highlight_current_line && bg == COLOR_DEFAULT && real_y == view->cursor.y){
                                   bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_CURRENT_LINE, bg);
                              }

                              int change_color_pair = ce_color_def_get(color_defs, draw_color_node->fg, bg);
                              attron(COLOR_PAIR(change_color_pair));
                              last_bg = bg;
                              last_fg = draw_color_node->fg;
                              draw_color_node = draw_color_node->next;
                         }

                         const char* look_ahead = line + rune_len;
                         int64_t next_rune_len = 0;
                         CeRune_t next_rune = 1;

                         next_rune = ce_utf8_decode(look_ahead, &next_rune_len);

                         if(x > col_max){
                              x++;
                         }else if(show_line_extends_passed_view_as &&
                                  ((view->rect.right >= terminal_right && x == col_max) ||
                                   (view->rect.right < terminal_right && x == (col_max - 1))) &&
                                  next_rune != 0){
                              int new_bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_LINE_EXTENDS_PASSED_VIEW, COLOR_DEFAULT);
                              int new_fg = ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_LINE_EXTENDS_PASSED_VIEW, COLOR_DEFAULT);
                              int change_color_pair = ce_color_def_get(color_defs, new_fg, new_bg);
                              attron(COLOR_PAIR(change_color_pair));
                              addch(show_line_extends_passed_view_as);
                              standend();
                              x++;
                         }else if(x >= col_min && rune > 0){
                              bool showed_one_of_the_multiple_cursors = false;

                              if(multiple_cursors){
                                   for(int64_t m = 0; m < multiple_cursors->count; m++){
                                        if(real_y == multiple_cursors->cursors[m].y && x == multiple_cursors->cursors[m].x){
                                             int new_bg = 0;
                                             if(multiple_cursors->active){
                                                  new_bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_MULTIPLE_CURSOR_ACTIVE, last_bg);
                                             }else{
                                                  new_bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_MULTIPLE_CURSOR_INACTIVE, last_bg);
                                             }
                                             int change_color_pair = ce_color_def_get(color_defs, last_fg, new_bg);
                                             attron(COLOR_PAIR(change_color_pair));
                                             showed_one_of_the_multiple_cursors = true;
                                             break;
                                        }
                                   }
                              }

                              if(rune == CE_TAB){
                                   x += tab_width;
                                   addstr(tab_str);
                              }else if(rune >= 0x80){
                                   char utf8_string[CE_UTF8_SIZE + 1];
                                   int64_t bytes_written = 0;
                                   ce_utf8_encode(rune, utf8_string, CE_UTF8_SIZE, &bytes_written);
                                   utf8_string[bytes_written] = 0;
                                   addstr(utf8_string);
                                   x++;
                              }else{
                                   addch(rune);
                                   x++;
                              }

                              if(showed_one_of_the_multiple_cursors){
                                   int change_color_pair = ce_color_def_get(color_defs, last_fg, last_bg);
                                   attron(COLOR_PAIR(change_color_pair));
                              }
                         }else if(rune == CE_TAB){
                              x += tab_width;
                         }else{
                              x++;
                         }

                         line += rune_len;
                         index++;
                    }

                    x--;
               }

               if(x < col_min) x = col_min;
               switch(visual_line_display_type){
               default:
                    break;
               case CE_VISUAL_LINE_DISPLAY_TYPE_FULL_LINE:
                    for(; x <= col_max; x++) addch(' ');
                    break;
               case CE_VISUAL_LINE_DISPLAY_TYPE_INCLUDE_NEWLINE:
                    addch(' ');
                    x++;
               // intentional fall through
               case CE_VISUAL_LINE_DISPLAY_TYPE_EXCLUDE_NEWLINE:
                    standend();
                    if(!view->buffer->no_highlight_current_line && real_y == view->cursor.y){
                         int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_CURRENT_LINE, COLOR_DEFAULT);
                         int change_color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, bg);
                         attron(COLOR_PAIR(change_color_pair));
                    }
                    for(; x <= col_max; x++) addch(' ');
                    break;
               }
          }
     }else{
          standend();
     }
}

void _draw_view_status(CeView_t* view, CeVim_t* vim, CeMacros_t* macros, CeMultipleCursors_t* multiple_cursors,
                       CeColorDefs_t* color_defs, int64_t height_offset, int ui_fg_color, int ui_bg_color){
     // create bottom bar bg
     int64_t bottom = view->rect.bottom + height_offset;
     int color_pair = ce_color_def_get(color_defs, ui_fg_color, ui_bg_color);
     attron(COLOR_PAIR(color_pair));
     int64_t width = (view->rect.right - view->rect.left) + 1;
     move(bottom, view->rect.left);
     for(int64_t i = 0; i < width; ++i){
          addch(' ');
     }

     // set the mode line
     int vim_mode_fg = ui_fg_color;
     const char* vim_mode_string = NULL;

     if(vim){
          switch(vim->mode){
          default:
               break;
          case CE_VIM_MODE_NORMAL:
               vim_mode_string = "NORMAL";
               vim_mode_fg = COLOR_BLUE;
               break;
          case CE_VIM_MODE_INSERT:
               vim_mode_string = "INSERT";
               vim_mode_fg = COLOR_GREEN;
               break;
          case CE_VIM_MODE_VISUAL:
               vim_mode_string = "VISUAL";
               vim_mode_fg = COLOR_YELLOW;
               break;
          case CE_VIM_MODE_VISUAL_LINE:
               vim_mode_string = "VISUAL LINE";
               vim_mode_fg = COLOR_BRIGHT_YELLOW;
               break;
          case CE_VIM_MODE_VISUAL_BLOCK:
               vim_mode_string = "VISUAL BLOCK";
               vim_mode_fg = COLOR_BRIGHT_YELLOW;
               break;
          case CE_VIM_MODE_REPLACE:
               vim_mode_string = "REPLACE";
               vim_mode_fg = COLOR_RED;
               break;
          }
     }

     if(vim_mode_string){
          color_pair = ce_color_def_get(color_defs, vim_mode_fg, ui_bg_color);
          attron(COLOR_PAIR(color_pair));
          mvprintw(bottom, view->rect.left + 1, "%s", vim_mode_string);

          color_pair = ce_color_def_get(color_defs, ui_fg_color, ui_bg_color);
          attron(COLOR_PAIR(color_pair));
          printw(" %s", view->buffer->name);
     }else{
          color_pair = ce_color_def_get(color_defs, ui_fg_color, ui_bg_color);
          attron(COLOR_PAIR(color_pair));
          mvprintw(bottom, view->rect.left + 1, "%s", view->buffer->name);
     }

     const char* status_str = ce_buffer_status_get_str(view->buffer->status);
     if(status_str) printw("%s", status_str);

     if(vim_mode_string && ce_macros_is_recording(macros)){
          printw(" RECORDING %c", macros->recording);
     }

#ifdef ENABLE_DEBUG_KEY_PRESS_INFO
     if(vim_mode_string) printw(" %s %d ", keyname(g_last_key), g_last_key);
#endif

     char cursor_pos_string[32];
     int64_t cursor_pos_string_len = snprintf(cursor_pos_string, 32, "%ld, %ld", view->cursor.x + 1, view->cursor.y + 1);
     mvprintw(bottom, view->rect.right - (cursor_pos_string_len + 1), "%s", cursor_pos_string);

     if(multiple_cursors && multiple_cursors->count){
          if(multiple_cursors->active){
               color_pair = ce_color_def_get(color_defs, COLOR_GREEN, ui_bg_color);
          }else{
               color_pair = ce_color_def_get(color_defs, COLOR_RED, ui_bg_color);
          }

          attron(COLOR_PAIR(color_pair));

          int64_t multiple_cursor_string_len = snprintf(cursor_pos_string, 32, "(%ld)", multiple_cursors->count);
          mvprintw(bottom, view->rect.right - (cursor_pos_string_len + 1) - (multiple_cursor_string_len + 1), "%s", cursor_pos_string);
     }
}

static void _draw_layout(CeLayout_t* layout, CeVim_t* vim, CeVimVisualData_t* visual, CeMacros_t* macros,
                         CeBuffer_t* input_buffer, CeColorDefs_t* color_defs, int64_t tab_width, CeLineNumber_t line_number,
                         CeVisualLineDisplayType_t visual_line_display_type, CeMultipleCursors_t* multiple_cursors,
                         CeLayout_t* current, CeSyntaxDef_t* syntax_defs, int64_t terminal_width, bool highlight_search,
                         int ui_fg_color, int ui_bg_color, CeRune_t show_line_extends_passed_view_as){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
     {
          CeDrawColorList_t draw_color_list = {};
          CeAppBufferData_t* buffer_data = layout->view.buffer->app_data;

          if(buffer_data->syntax_function){
               CeRangeList_t range_list = {};
               // add to the highlight range list only if this is the current view
               if(layout == current){
                    switch(vim->mode){
                    default:
                         break;
                    case CE_VIM_MODE_VISUAL:
                    {
                         CeRange_t range = {visual->point, layout->view.cursor};
                         ce_range_sort(&range);
                         ce_range_list_insert(&range_list, range.start, range.end);

                         if(multiple_cursors && multiple_cursors->active){
                              for(int64_t i = 0; i < multiple_cursors->count; i++){
                                   range = (CeRange_t){multiple_cursors->visuals[i].point, multiple_cursors->cursors[i]};
                                   ce_range_sort(&range);
                                   ce_range_list_insert_sorted(&range_list, range.start, range.end);
                              }
                         }
                    } break;
                    case CE_VIM_MODE_VISUAL_LINE:
                    {
                         CeRange_t range = {visual->point, layout->view.cursor};
                         ce_range_sort(&range);
                         range.start.x = 0;
                         range.end.x = ce_utf8_last_index(layout->view.buffer->lines[range.end.y]) + 1;
                         ce_range_list_insert(&range_list, range.start, range.end);

                         if(multiple_cursors && multiple_cursors->active){
                              for(int64_t i = 0; i < multiple_cursors->count; i++){
                                   range = (CeRange_t){multiple_cursors->visuals[i].point, multiple_cursors->cursors[i]};
                                   ce_range_sort(&range);
                                   range.start.x = 0;
                                   range.end.x = ce_utf8_last_index(layout->view.buffer->lines[range.end.y]) + 1;
                                   ce_range_list_insert_sorted(&range_list, range.start, range.end);
                              }
                         }
                    } break;
                    case CE_VIM_MODE_VISUAL_BLOCK:
                    {
                         CeRange_t range = {visual->point, layout->view.cursor};
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
                              ce_range_list_insert(&range_list, start, end);
                         }

                         if(multiple_cursors && multiple_cursors->active){
                              for(int64_t c = 0; c < multiple_cursors->count; c++){
                                   range = (CeRange_t){multiple_cursors->visuals[c].point, multiple_cursors->cursors[c]};

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
                                        ce_range_list_insert_sorted(&range_list, start, end);
                                   }
                              }
                         }
                    } break;
                    }
               }

               // TODO: doesn't work for multiline searches
               if(highlight_search){
                    const char* pattern = NULL;

                    if((strcmp(input_buffer->name, "Search") == 0 ||
                        strcmp(input_buffer->name, "Reverse Search") == 0 ||
                        strcmp(input_buffer->name, "Regex Search") == 0 ||
                        strcmp(input_buffer->name, "Regex Reverse Search") == 0) &&
                       input_buffer->line_count && strlen(input_buffer->lines[0])){
                         pattern = input_buffer->lines[0];
                    }else{
                         const CeVimYank_t* yank = vim->yanks + ce_vim_register_index('/');
                         if(yank->text) pattern = yank->text;
                    }

                    if(pattern){
                         int64_t pattern_len = ce_utf8_strlen(pattern);
                         int64_t min = layout->view.scroll.y;
                         int64_t max = min + (layout->view.rect.bottom - layout->view.rect.top);
                         int64_t clamp_max = (layout->view.buffer->line_count - 1);
                         CE_CLAMP(min, 0, clamp_max);
                         CE_CLAMP(max, 0, clamp_max);

                         if(vim->search_mode == CE_VIM_SEARCH_MODE_FORWARD ||
                            vim->search_mode == CE_VIM_SEARCH_MODE_BACKWARD){
                              for(int64_t i = min; i <= max; i++){
                                   char* match = NULL;
                                   char* itr = layout->view.buffer->lines[i];
                                   while((match = strstr(itr, pattern))){
                                        CePoint_t start = {ce_utf8_strlen_between(layout->view.buffer->lines[i], match) - 1, i};
                                        CePoint_t end = {start.x + (pattern_len - 1), i};
                                        ce_range_list_insert(&range_list, start, end);
                                        itr = match + pattern_len;
                                   }
                              }
                         }else if(vim->search_mode == CE_VIM_SEARCH_MODE_REGEX_FORWARD ||
                                  vim->search_mode == CE_VIM_SEARCH_MODE_REGEX_BACKWARD){
                              regex_t regex = {};
                              int rc = regcomp(&regex, pattern, REG_EXTENDED);
                              if(rc == 0){
                                   const size_t match_count = 1;
                                   regmatch_t matches[match_count];

                                   for(int64_t i = min; i <= max; i++){
                                        char* itr = layout->view.buffer->lines[i];
                                        int64_t prev_end_x = 0;
                                        while(itr){
                                             rc = regexec(&regex, itr, match_count, matches, 0);
                                             if(rc == 0){
                                                  int64_t match_len = matches[0].rm_eo - matches[0].rm_so;
                                                  if(match_len > 0){
                                                       CePoint_t start = {prev_end_x + matches[0].rm_so, i};
                                                       CePoint_t end = {start.x + (match_len - 1), i};
                                                       ce_range_list_insert(&range_list, start, end);
                                                       itr = ce_utf8_iterate_to(itr, matches[0].rm_so + match_len);
                                                       prev_end_x = end.x + 1;
                                                  }else{
                                                       break;
                                                  }
                                             }else{
                                                  break;
                                             }
                                        }
                                   }
                              }
                         }
                    }
               }

               buffer_data->syntax_function(&layout->view, &range_list, &draw_color_list, syntax_defs,
                                            layout->view.buffer->syntax_data);
               ce_range_list_free(&range_list);
          }

          _draw_view(&layout->view, tab_width, line_number, visual_line_display_type, multiple_cursors, &draw_color_list, color_defs, syntax_defs,
                    terminal_width, show_line_extends_passed_view_as);
          ce_draw_color_list_free(&draw_color_list);
          _draw_view_status(&layout->view, layout == current ? vim : NULL, macros, multiple_cursors, color_defs, 0,
                           ui_fg_color, ui_bg_color);
          int64_t rect_height = layout->view.rect.bottom - layout->view.rect.top;
          int color_pair = ce_color_def_get(color_defs, ui_fg_color, ui_bg_color);
          attron(COLOR_PAIR(color_pair));
          if(layout->view.rect.right < (terminal_width - 1)){
               for(int i = 0; i < rect_height; i++){
                    mvaddch(layout->view.rect.top + i, layout->view.rect.right, ' ');
               }
          }
     } break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               _draw_layout(layout->list.layouts[i], vim, visual, macros, input_buffer, color_defs, tab_width,
                           line_number, visual_line_display_type, multiple_cursors, current, syntax_defs, terminal_width, highlight_search,
                           ui_fg_color, ui_bg_color, show_line_extends_passed_view_as);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          _draw_layout(layout->tab.root, vim, visual, macros, input_buffer, color_defs, tab_width, line_number,
                      visual_line_display_type, multiple_cursors, current, syntax_defs, terminal_width, highlight_search, ui_fg_color, ui_bg_color,
                      show_line_extends_passed_view_as);
          break;
     }
}

void ce_draw_term(CeApp_t* app){
     CeColorDefs_t color_defs = {};

     CeLayout_t* tab_list_layout = app->tab_list_layout;
     CeLayout_t* tab_layout = tab_list_layout->tab_list.current;

     CeView_t* view = &tab_layout->tab.current->view;

     // draw a tab bar if there is more than 1 tab
     if(tab_list_layout->tab_list.tab_count > 1){
          move(0, 0);
          int color_pair = ce_color_def_get(&color_defs, app->config_options.ui_fg_color, app->config_options.ui_bg_color);
          attron(COLOR_PAIR(color_pair));
          for(int64_t i = tab_list_layout->tab_list.rect.left; i <= tab_list_layout->tab_list.rect.right; i++){
               addch(' ');
          }

          move(0, 0);

          for(int64_t i = 0; i < tab_list_layout->tab_list.tab_count; i++){
               if(tab_list_layout->tab_list.tabs[i] == tab_list_layout->tab_list.current){
                    color_pair = ce_color_def_get(&color_defs, COLOR_DEFAULT, COLOR_DEFAULT);
                    attron(COLOR_PAIR(color_pair));
               }else{
                    color_pair = ce_color_def_get(&color_defs, app->config_options.ui_fg_color, app->config_options.ui_bg_color);
                    attron(COLOR_PAIR(color_pair));
               }

               if(tab_list_layout->tab_list.tabs[i]->tab.current->type == CE_LAYOUT_TYPE_VIEW){
                    const char* buffer_name = tab_list_layout->tab_list.tabs[i]->tab.current->view.buffer->name;

                    printw(" %s ", buffer_name);
               }else{
                    printw(" selection ");
               }
          }
     }

     standend();
     _draw_layout(tab_layout, &app->vim, &app->visual, &app->macros, app->input_view.buffer, &color_defs,
                 app->config_options.tab_width, app->config_options.line_number, app->config_options.visual_line_display_type,
                 &app->multiple_cursors, tab_layout->tab.current, app->syntax_defs, tab_list_layout->tab_list.rect.right,
                 app->highlight_search, app->config_options.ui_fg_color, app->config_options.ui_bg_color,
                 app->config_options.show_line_extends_passed_view_as);

     if(app->input_complete_func){
          CeDrawColorList_t draw_color_list = {};
          _draw_view(&app->input_view, app->config_options.tab_width, app->config_options.line_number,
                    app->config_options.visual_line_display_type, NULL, &draw_color_list, &color_defs, app->syntax_defs,
                    app->terminal_rect.right, app->config_options.show_line_extends_passed_view_as);
          ce_draw_color_list_free(&draw_color_list);
          int64_t new_status_bar_offset = (app->input_view.rect.bottom - app->input_view.rect.top) + 1;
          _draw_view_status(&app->input_view, &app->vim, &app->macros, &app->multiple_cursors, &color_defs, 0,
                           app->config_options.ui_fg_color, app->config_options.ui_bg_color);
          _draw_view_status(&tab_layout->tab.current->view, NULL, &app->macros, &app->multiple_cursors, &color_defs,
                           -new_status_bar_offset, app->config_options.ui_fg_color, app->config_options.ui_bg_color);
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
          CeDrawColorList_t draw_color_list = {};
          CeRangeList_t range_list = {};
          CeAppBufferData_t* buffer_data = app->complete_view.buffer->app_data;
          buffer_data->syntax_function(&app->complete_view, &range_list, &draw_color_list, app->syntax_defs,
                                       app->complete_view.buffer->syntax_data);
          ce_range_list_free(&range_list);
          _draw_view(&app->complete_view, app->config_options.tab_width, app->config_options.line_number,
                    app->config_options.visual_line_display_type, NULL, &draw_color_list, &color_defs, app->syntax_defs,
                    app->terminal_rect.right, app->config_options.show_line_extends_passed_view_as);
          ce_draw_color_list_free(&draw_color_list);
          if(app->input_complete_func){
               int64_t new_status_bar_offset = (app->complete_view.rect.bottom - app->complete_view.rect.top) + 1 + app->input_view.buffer->line_count;
               _draw_view_status(&tab_layout->tab.current->view, NULL, &app->macros, &app->multiple_cursors,
                                &color_defs, -new_status_bar_offset, app->config_options.ui_fg_color,
                                app->config_options.ui_bg_color);
          }
     }

     if(app->message_mode){
          CeDrawColorList_t draw_color_list = {};
          CeRangeList_t range_list = {};
          CeAppBufferData_t* buffer_data = app->message_view.buffer->app_data;
          buffer_data->syntax_function(&app->message_view, &range_list, &draw_color_list, app->syntax_defs,
                                       app->message_view.buffer->syntax_data);
          ce_range_list_free(&range_list);

          _draw_view(&app->message_view, app->config_options.tab_width, app->config_options.line_number,
                    app->config_options.visual_line_display_type, NULL, &draw_color_list, &color_defs, app->syntax_defs,
                    app->terminal_rect.right, app->config_options.show_line_extends_passed_view_as);
          ce_draw_color_list_free(&draw_color_list);

          // set the specified background
          int message_len = ce_utf8_strlen(app->message_view.buffer->lines[0]);
          int color_pair = ce_color_def_get(&color_defs, app->config_options.message_fg_color, app->config_options.message_bg_color);
          attron(COLOR_PAIR(color_pair));
          int64_t view_width = ce_view_width(&app->message_view);
          move(app->message_view.rect.top, app->message_view.rect.left + message_len);
          for(int i = message_len; i < view_width; i++){
               addch(' ');
          }

     }

     // show border when non view is selected
     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW){
          int64_t rect_height = 0;
          int64_t rect_width = 0;
          CeRect_t* rect = NULL;
          switch(tab_layout->tab.current->type){
          default:
               break;
          case CE_LAYOUT_TYPE_LIST:
               rect = &tab_layout->tab.current->list.rect;
               rect_width = rect->right - rect->left;
               rect_height = rect->bottom - rect->top;
               break;
          case CE_LAYOUT_TYPE_TAB:
               rect = &tab_layout->tab.current->tab.rect;
               rect_width = rect->right - rect->left;
               rect_height = rect->bottom - rect->top;
               break;
          }

          int color_pair = ce_color_def_get(&color_defs, COLOR_BRIGHT_WHITE, COLOR_BRIGHT_WHITE);
          attron(COLOR_PAIR(color_pair));
          for(int i = 0; i < rect_height; i++){
               mvaddch(rect->top + i, rect->right, ' ');
               mvaddch(rect->top + i, rect->left, ' ');
          }

          for(int i = 0; i < rect_width; i++){
               mvaddch(rect->top, rect->left + i, ' ');
               mvaddch(rect->bottom, rect->left + i, ' ');
          }

          mvaddch(rect->bottom, rect->right, ' ');

          move(0, 0);
     }else if(app->input_complete_func){
          CePoint_t screen_cursor = view_cursor_on_screen(&app->input_view, app->config_options.tab_width,
                                                          app->config_options.line_number);
          move(screen_cursor.y, screen_cursor.x);
     }else{
          CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width,
                                                          app->config_options.line_number);
          move(screen_cursor.y, screen_cursor.x);
     }

     refresh();
}
