#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <ncurses.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

#include "ce_app.h"
#include "ce_commands.h"

FILE* g_ce_log = NULL;
CeBuffer_t* g_ce_log_buffer = NULL;

// limit to 60 fps
#define DRAW_USEC_LIMIT 16666

void handle_sigint(int signal){
     // pass
}

const char* buffer_status_get_str(CeBufferStatus_t status){
     if(status == CE_BUFFER_STATUS_READONLY){
           return "[RO]";
     }

     if(status == CE_BUFFER_STATUS_MODIFIED ||
        status == CE_BUFFER_STATUS_NEW_FILE){
          return "*";
     }

     return "";
}

static void build_buffer_list(CeBuffer_t* buffer, CeBufferNode_t* head){
     char buffer_info[BUFSIZ];
     ce_buffer_empty(buffer);

     // calc maxes of things we care about for formatting
     int64_t max_buffer_lines = 0;
     int64_t max_name_len = 0;
     int64_t buffer_count = 0;
     const CeBufferNode_t* itr = head;
     while(itr){
          if(max_buffer_lines < itr->buffer->line_count) max_buffer_lines = itr->buffer->line_count;
          int64_t name_len = strlen(itr->buffer->name);
          if(max_name_len < name_len) max_name_len = name_len;
          buffer_count++;
          itr = itr->next;
     }

     int64_t max_buffer_lines_digits = ce_count_digits(max_buffer_lines);
     if(max_buffer_lines_digits < 5) max_buffer_lines_digits = 5; // account for "lines" string row header
     if(max_name_len < 11) max_name_len = 11; // account for "buffer name" string row header

     // build format string, OMG THIS IS SO UNREADABLE HOLY MOLY BATMAN
     char format_string[BUFSIZ];

     // build buffer info
     snprintf(format_string, BUFSIZ, "%%5s %%-%"PRId64"s %%%"PRId64 PRId64, max_name_len, max_buffer_lines_digits);

     itr = head;
     while(itr){
          const char* buffer_flag_str = buffer_status_get_str(itr->buffer->status);
          snprintf(buffer_info, BUFSIZ, format_string, buffer_flag_str, itr->buffer->name,
                   itr->buffer->line_count);
          buffer_append_on_new_line(buffer, buffer_info);
          itr = itr->next;
     }

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

static void build_yank_list(CeBuffer_t* buffer, CeVimYank_t* yanks){
     char line[256];
     ce_buffer_empty(buffer);
     for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
          CeVimYank_t* yank = yanks + i;
          if(yank->text == NULL) continue;
          char reg = i + '!';
          const char* yank_type = "string";
          switch(yank->type){
          default:
               break;
          case CE_VIM_YANK_TYPE_LINE:
               yank_type = "line";
               break;
          case CE_VIM_YANK_TYPE_BLOCK:
               yank_type = "block";
               break;
          }

          if(yank->type ==CE_VIM_YANK_TYPE_BLOCK){
               snprintf(line, 256, "// register '%c': type: %s\n", reg, yank_type);
               buffer_append_on_new_line(buffer, line);
               for(int64_t l = 0; l < yank->block_line_count; l++){
                    if(yank->block[l]){
                         strncpy(line, yank->block[l], 256);
                         buffer_append_on_new_line(buffer, line);
                    }else{
                         int64_t last_line = buffer->line_count;
                         int64_t line_len = 0;
                         if(last_line) last_line--;
                         if(buffer->lines[last_line]) line_len = ce_utf8_strlen(buffer->lines[last_line]);
                         ce_buffer_insert_string(buffer, "\n\n", (CePoint_t){line_len, last_line});
                    }
               }
          }else{
               snprintf(line, 256, "// register '%c': type: %s\n%s\n", reg, yank_type, yank->text);
               buffer_append_on_new_line(buffer, line);
          }
     }

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

static void build_macro_list(CeBuffer_t* buffer, CeMacros_t* macros){
     ce_buffer_empty(buffer);
     char line[256];
     for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
          CeRuneNode_t* rune_head = macros->rune_head[i];
          if(!rune_head) continue;
          CeRune_t* rune_string = ce_rune_node_string(rune_head);
          char* string = ce_rune_string_to_char_string(rune_string);
          char reg = i + '!';
          snprintf(line, 256, "// register '%c'\n%s", reg, string);
          free(rune_string);
          free(string);
          buffer_append_on_new_line(buffer, line);
     }

     // NOTE: must keep this in sync with ce_rune_string_to_char_string()
     buffer_append_on_new_line(buffer, "");
     buffer_append_on_new_line(buffer, "// escape conversions");
     buffer_append_on_new_line(buffer, "// \\b -> KEY_BACKSPACE");
     buffer_append_on_new_line(buffer, "// \\e -> KEY_ESCAPE");
     buffer_append_on_new_line(buffer, "// \\r -> KEY_ENTER");
     buffer_append_on_new_line(buffer, "// \\t -> KEY_TAB");
     buffer_append_on_new_line(buffer, "// \\u -> KEY_UP");
     buffer_append_on_new_line(buffer, "// \\d -> KEY_DOWN");
     buffer_append_on_new_line(buffer, "// \\l -> KEY_LEFT");
     buffer_append_on_new_line(buffer, "// \\i -> KEY_RIGHT");
     buffer_append_on_new_line(buffer, "// \\\\ -> \\"); // HAHAHAHAHA
     buffer_append_on_new_line(buffer, "// \\^k -> CTRL_K");

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

static void build_mark_list(CeBuffer_t* buffer, CeVimBufferData_t* buffer_data){
     ce_buffer_empty(buffer);
     char line[256];
     buffer_append_on_new_line(buffer, "reg point:\n");
     for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
          CePoint_t* point = buffer_data->marks + i;
          if(point->x == 0 && point->y == 0) continue;
          char reg = i + '!';
          snprintf(line, 256, "'%c' %ld, %ld\n", reg, point->x, point->y);
          buffer_append_on_new_line(buffer, line);
     }

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

static void build_jump_list(CeBuffer_t* buffer, CeJumpList_t* jump_list){
     ce_buffer_empty(buffer);
     char line[256];
     int max_row_digits = -1;
     int max_col_digits = -1;
     for(int64_t i = 0; i < jump_list->count; i++){
          int digits = ce_count_digits(jump_list->destinations[i].point.x);
          if(digits > max_col_digits) max_col_digits = digits;
          digits = ce_count_digits(jump_list->destinations[i].point.y);
          if(digits > max_row_digits) max_row_digits = digits;
     }

     char format_string[128];
     snprintf(format_string, 128, "  %%%dld, %%%dld  %%s", max_col_digits, max_row_digits);
     for(int64_t i = 0; i < jump_list->count; i++){
          if(i == jump_list->current){
               format_string[0] = '*';
          }else{
               format_string[0] = ' ';
          }
          CeDestination_t* dest = jump_list->destinations + i;
          snprintf(line, 256, format_string, dest->point.x, dest->point.y, dest->filepath);
          buffer_append_on_new_line(buffer, line);
     }

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

void draw_view(CeView_t* view, int64_t tab_width, CeLineNumber_t line_number, CeDrawColorList_t* draw_color_list,
               CeColorDefs_t* color_defs, CeSyntaxDef_t* syntax_defs){
     int64_t view_height = view->rect.bottom - view->rect.top;
     int64_t view_width = view->rect.right - view->rect.left;
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
               if(real_y == view->cursor.y){
                    int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_CURRENT_LINE, COLOR_DEFAULT);
                    int change_color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, bg);
                    attron(COLOR_PAIR(change_color_pair));
               }

               if(line_index < view->buffer->line_count){
                    const char* line = view->buffer->lines[y + row_min];

                    while(rune > 0){
                         rune = ce_utf8_decode(line, &rune_len);

                         // check if we need to move to the next color
                         while(draw_color_node && !ce_point_after(draw_color_node->point, (CePoint_t){index, y + view->scroll.y})){
                              int bg = draw_color_node->bg;
                              if(bg == COLOR_DEFAULT && real_y == view->cursor.y){
                                   bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_CURRENT_LINE, bg);
                              }

                              int change_color_pair = ce_color_def_get(color_defs, draw_color_node->fg, bg);
                              attron(COLOR_PAIR(change_color_pair));
                              draw_color_node = draw_color_node->next;
                         }

                         if(x >= col_min && x <= col_max && rune > 0){
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
               standend();
               if(real_y == view->cursor.y){
                    int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_CURRENT_LINE, COLOR_DEFAULT);
                    int change_color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, bg);
                    attron(COLOR_PAIR(change_color_pair));
               }
               for(; x <= col_max; x++) addch(' ');
          }
     }else{
          standend();
     }
}

void draw_view_status(CeView_t* view, CeVim_t* vim, CeMacros_t* macros, CeColorDefs_t* color_defs, int64_t height_offset,
                      int ui_fg_color, int ui_bg_color){
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
     int vim_mode_fg = COLOR_DEFAULT;
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

          color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, ui_bg_color);
          attron(COLOR_PAIR(color_pair));
          printw(" %s", view->buffer->name);
     }else{
          color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, ui_bg_color);
          attron(COLOR_PAIR(color_pair));
          mvprintw(bottom, view->rect.left + 1, "%s", view->buffer->name);
     }

     const char* status_str = buffer_status_get_str(view->buffer->status);
     if(status_str) printw(status_str);

     if(vim_mode_string && ce_macros_is_recording(macros)){
          printw(" RECORDING %c", macros->recording);
     }

     char cursor_pos_string[32];
     int64_t cursor_pos_string_len = snprintf(cursor_pos_string, 32, "%ld, %ld", view->cursor.x + 1, view->cursor.y + 1);
     mvprintw(bottom, view->rect.right - (cursor_pos_string_len + 1), "%s", cursor_pos_string);
}

void draw_layout(CeLayout_t* layout, CeVim_t* vim, CeMacros_t* macros, CeTerminalList_t* terminal_list, CeBuffer_t* input_buffer,
                 CeColorDefs_t* color_defs, int64_t tab_width, CeLineNumber_t line_number, CeLayout_t* current,
                 CeSyntaxDef_t* syntax_defs, int64_t terminal_width, bool highlight_search, int ui_fg_color, int ui_bg_color){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
     {
          CeDrawColorList_t draw_color_list = {};
          CeAppBufferData_t* buffer_data = layout->view.buffer->app_data;

          // update which terminal buffer we are viewing
          CeTerminal_t* terminal = ce_buffer_in_terminal_list(layout->view.buffer, terminal_list);
          if(terminal) layout->view.buffer = terminal->buffer;

          if(buffer_data->syntax_function){
               CeRangeList_t range_list = {};
               // add to the highlight range list only if this is the current view
               if(layout == current){
                    switch(vim->mode){
                    default:
                         break;
                    case CE_VIM_MODE_VISUAL:
                    {
                         CeRange_t range = {vim->visual, layout->view.cursor};
                         ce_range_sort(&range);
                         ce_range_list_insert(&range_list, range.start, range.end);
                    } break;
                    case CE_VIM_MODE_VISUAL_LINE:
                    {
                         CeRange_t range = {vim->visual, layout->view.cursor};
                         ce_range_sort(&range);
                         range.start.x = 0;
                         range.end.x = ce_utf8_last_index(layout->view.buffer->lines[range.end.y]);
                         ce_range_list_insert(&range_list, range.start, range.end);
                    } break;
                    case CE_VIM_MODE_VISUAL_BLOCK:
                    {
                         CeRange_t range = {vim->visual, layout->view.cursor};
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
                    } break;
                    }
               }

               // TODO: doesn't work for multiline searches
               if(highlight_search){
                    const char* pattern = NULL;

                    if((strcmp(input_buffer->name, "SEARCH") == 0 ||
                        strcmp(input_buffer->name, "REVERSE SEARCH") == 0 ||
                        strcmp(input_buffer->name, "REGEX SEARCH") == 0 ||
                        strcmp(input_buffer->name, "REGEX REVERSE SEARCH") == 0) &&
                       input_buffer->line_count && strlen(input_buffer->lines[0])){
                         pattern = input_buffer->lines[0];
                    }else{
                         const CeVimYank_t* yank = vim->yanks + ce_vim_yank_register_index('/');
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

          draw_view(&layout->view, tab_width, line_number, &draw_color_list, color_defs, syntax_defs);
          ce_draw_color_list_free(&draw_color_list);
          draw_view_status(&layout->view, layout == current ? vim : NULL, macros, color_defs, 0, ui_fg_color, ui_bg_color);
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
               draw_layout(layout->list.layouts[i], vim, macros, terminal_list, input_buffer, color_defs, tab_width,
                           line_number, current, syntax_defs, terminal_width, highlight_search, ui_fg_color,
                           ui_bg_color);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          draw_layout(layout->tab.root, vim, macros, terminal_list, input_buffer, color_defs, tab_width, line_number,
                      current, syntax_defs, terminal_width, highlight_search, ui_fg_color, ui_bg_color);
          break;
     }
}

uint64_t time_between(struct timeval previous, struct timeval current){
     return (current.tv_sec - previous.tv_sec) * 1000000LL +
            (current.tv_usec - previous.tv_usec);
}

void draw(CeApp_t* app){
     CeColorDefs_t color_defs = {};

     CeLayout_t* tab_list_layout = app->tab_list_layout;
     CeLayout_t* tab_layout = tab_list_layout->tab_list.current;

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
                    color_pair = ce_color_def_get(&color_defs, COLOR_BRIGHT_WHITE, app->config_options.ui_bg_color);
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

     CeView_t* view = &tab_layout->tab.current->view;

     // update cursor if it is on a terminal
     CeTerminal_t* terminal = ce_buffer_in_terminal_list(view->buffer, &app->terminal_list);
     if(terminal && app->vim.mode == CE_VIM_MODE_INSERT){
          view->cursor.x = terminal->cursor.x;
          view->cursor.y = terminal->cursor.y + terminal->start_line;
     }

     standend();
     draw_layout(tab_layout, &app->vim, &app->macros, &app->terminal_list, app->input_view.buffer, &color_defs, app->config_options.tab_width,
                 app->config_options.line_number, tab_layout->tab.current, app->syntax_defs, tab_list_layout->tab_list.rect.right,
                 app->highlight_search, app->config_options.ui_fg_color, app->config_options.ui_bg_color);

     if(app->input_mode){
          CeDrawColorList_t draw_color_list = {};
          draw_view(&app->input_view, app->config_options.tab_width, app->config_options.line_number, &draw_color_list,
                    &color_defs, app->syntax_defs);
          int64_t new_status_bar_offset = (app->input_view.rect.bottom - app->input_view.rect.top) + 1;
          draw_view_status(&app->input_view, &app->vim, &app->macros, &color_defs, 0, app->config_options.ui_fg_color,
                           app->config_options.ui_bg_color);
          draw_view_status(&tab_layout->tab.current->view, NULL, &app->macros, &color_defs, -new_status_bar_offset,
                           app->config_options.ui_fg_color, app->config_options.ui_bg_color);
     }

     CeComplete_t* complete = ce_app_is_completing(app);
     if(complete && tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW && app->complete_list_buffer->line_count &&
        strlen(app->complete_list_buffer->lines[0])){
          CeLayout_t* view_layout = tab_layout->tab.current;
          app->complete_view.rect.left = view_layout->view.rect.left;
          app->complete_view.rect.right = view_layout->view.rect.right;
          if(app->input_mode){
               app->complete_view.rect.bottom = app->input_view.rect.top;
          }else{
               app->complete_view.rect.bottom = view_layout->view.rect.bottom - 1;
          }
          app->complete_view.rect.top = app->complete_view.rect.bottom - app->complete_list_buffer->line_count;
          if(app->complete_view.rect.top <= view_layout->view.rect.top){
               app->complete_view.rect.top = view_layout->view.rect.top + 1; // account for current view's status bar
          }
          app->complete_view.buffer = app->complete_list_buffer;
          app->complete_view.cursor.y = app->complete_list_buffer->cursor_save.y;
          app->complete_view.cursor.x = 0;
          ce_view_follow_cursor(&app->complete_view, 1, 1, 1); // NOTE: I don't think anyone wants their settings applied here
          CeDrawColorList_t draw_color_list = {};
          CeRangeList_t range_list = {};
          CeAppBufferData_t* buffer_data = app->complete_view.buffer->app_data;
          buffer_data->syntax_function(&app->complete_view, &range_list, &draw_color_list, app->syntax_defs,
                                       app->complete_view.buffer->syntax_data);
          ce_range_list_free(&range_list);
          draw_view(&app->complete_view, app->config_options.tab_width, app->config_options.line_number, &draw_color_list,
                    &color_defs, app->syntax_defs);
          if(app->input_mode){
               int64_t new_status_bar_offset = (app->complete_view.rect.bottom - app->complete_view.rect.top) + 1 + app->input_view.buffer->line_count;
               draw_view_status(&tab_layout->tab.current->view, NULL, &app->macros, &color_defs, -new_status_bar_offset,
                                app->config_options.ui_fg_color, app->config_options.ui_bg_color);
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
     }else if(app->input_mode){
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

static int int_strneq(int* a, int* b, size_t len){
     for(size_t i = 0; i < len; ++i){
          if(!*a) return false;
          if(!*b) return false;
          if(*a != *b) return false;
          a++;
          b++;
     }

     return true;
}

void scroll_to_and_center_if_offscreen(CeView_t* view, CePoint_t point, CeConfigOptions_t* config_options){
     view->cursor = point;
     CePoint_t before_follow = view->scroll;
     ce_view_follow_cursor(view, config_options->horizontal_scroll_off,
                           config_options->vertical_scroll_off, config_options->tab_width);
     if(!ce_points_equal(before_follow, view->scroll)){
          ce_view_center(view);
     }
}

bool apply_completion(CeApp_t* app, CeView_t* view){
     CeComplete_t* complete = ce_app_is_completing(app);
     if(app->vim.mode == CE_VIM_MODE_INSERT && complete){
          if(complete->current >= 0){
               if(strcmp(complete->elements[complete->current].string, app->input_view.buffer->lines[app->input_view.cursor.y]) == 0){
                    return false;
               }
               char* insertion = strdup(complete->elements[complete->current].string);
               int64_t insertion_len = strlen(insertion);
               CePoint_t delete_point = app->input_view.cursor;
               if(complete->current_match){
                    int64_t delete_len = strlen(complete->current_match);
                    delete_point = ce_buffer_advance_point(app->input_view.buffer, app->input_view.cursor, -delete_len);
                    if(delete_len > 0){
                         ce_buffer_remove_string_change(app->input_view.buffer, delete_point, delete_len,
                                                        &app->input_view.cursor, app->input_view.cursor, true);
                    }
               }

               if(insertion_len > 0){
                    CePoint_t cursor_end = {delete_point.x + insertion_len, delete_point.y};
                    ce_buffer_insert_string_change(app->input_view.buffer, insertion, delete_point, &app->input_view.cursor,
                                                   cursor_end, true);
               }

               // if completion was load_file, continue auto completing since we could have completed a directory
               // and want to see what is in it
               if(complete == &app->load_file_complete){
                    char* base_directory = buffer_base_directory(view->buffer, &app->terminal_list);
                    complete_files(&app->load_file_complete, app->input_view.buffer->lines[0], base_directory);
                    free(base_directory);
                    build_complete_list(app->complete_list_buffer, &app->load_file_complete);
               }
          }

          return true;
     }

     return false;
}

bool handle_input_history_key(int key, CeHistory_t* history, CeBuffer_t* input_buffer, CePoint_t* cursor){
     if(key == KEY_UP){
          char* prev = ce_history_previous(history);
          if(prev){
               ce_buffer_remove_string(input_buffer, (CePoint_t){0, 0}, ce_utf8_strlen(input_buffer->lines[0]));
               ce_buffer_insert_string(input_buffer, prev, (CePoint_t){0, 0});
          }
          cursor->x = ce_utf8_strlen(input_buffer->lines[0]);
          return true;
     }

     if(key == KEY_DOWN){
          char* next = ce_history_next(history);
          ce_buffer_remove_string(input_buffer, (CePoint_t){0, 0}, ce_utf8_strlen(input_buffer->lines[0]));
          if(next){
               ce_buffer_insert_string(input_buffer, next, (CePoint_t){0, 0});
          }
          cursor->x = ce_utf8_strlen(input_buffer->lines[0]);
          return true;
     }

     return false;
}

void app_handle_key(CeApp_t* app, CeView_t* view, int key){
     if(key == KEY_RESIZE){
          ce_app_update_terminal_view(app);
          return;
     }

     if(app->last_vim_handle_result == CE_VIM_PARSE_IN_PROGRESS){
          const CeRune_t* end = NULL;
          int64_t parsed_multiplier = istrtol(app->vim.current_command, &end);
          if(end) app->macro_multiplier = parsed_multiplier;
     }

     if(key == '.' && app->last_macro_register){
          app->macro_multiplier = app->last_macro_multiplier;
          app->replay_macro = true;
          key = app->last_macro_register;
     }

     if(app->key_count == 0 &&
        (app->last_vim_handle_result != CE_VIM_PARSE_IN_PROGRESS || app->macro_multiplier > 1) &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONTINUE &&
        app->vim.mode != CE_VIM_MODE_INSERT &&
        app->vim.mode != CE_VIM_MODE_REPLACE){
          if(key == 'q' && !app->replay_macro){ // TODO: make configurable
               if(ce_macros_is_recording(&app->macros)){
                    ce_macros_end_recording(&app->macros);
                    app->record_macro = false;
               }else{
                    app->record_macro = true;
               }

               return;
          }

          if(key == '@' && !app->record_macro && !app->replay_macro){
               app->replay_macro = true;
               app->vim.current_command[0] = 0;
               return;
          }

          if(app->record_macro && !ce_macros_is_recording(&app->macros)){
               ce_macros_begin_recording(&app->macros, key);
               app->macro_multiplier = 1;
               return;
          }

          if(app->replay_macro){
               app->replay_macro = false;
               CeRune_t* rune_string = ce_macros_get_register_string(&app->macros, key);
               if(rune_string){
                    for(int64_t i = 0; i < app->macro_multiplier; i++){
                         CeRune_t* itr = rune_string;
                         while(*itr){
                              app_handle_key(app, view, *itr);

                              // update the view if it has changed
                              CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
                              if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
                                   view = &tab_layout->tab.current->view;
                              }
                              itr++;
                         }
                    }
                    app->last_macro_register = key;
                    app->last_macro_multiplier = app->macro_multiplier;
                    app->macro_multiplier = 1;
               }

               free(rune_string);
               return;
          }
     }

     if(ce_macros_is_recording(&app->macros)){
          ce_macros_record_key(&app->macros, key);
     }

     if(view && !app->input_mode){
          CeTerminal_t* terminal = ce_buffer_in_terminal_list(view->buffer, &app->terminal_list);
          if(terminal){
               int64_t width = (view->rect.right - view->rect.left);
               int64_t height = (view->rect.bottom - view->rect.top);
               if(terminal->columns != width || terminal->rows != height){
                    ce_terminal_resize(terminal, width, height);
               }

               if(app->vim.mode == CE_VIM_MODE_INSERT){
                    if(key == KEY_ESCAPE){
                         app->vim.mode = CE_VIM_MODE_NORMAL;
                    }else if(key == 1){ // ctrl + a
                         // TODO: make this configurable
                         // send escape
                         ce_terminal_send_key(terminal, KEY_ESCAPE);
                    }else{
                         if(key == KEY_ENTER){
                              update_terminal_last_goto_using_cursor(terminal);
                         }
                         ce_terminal_send_key(terminal, key);
                    }
                    return;
               }else if(app->vim.mode == CE_VIM_MODE_NORMAL){
                    if(key == 'p'){
                         CeVimYank_t* yank = app->vim.yanks + ce_vim_yank_register_index('"');
                         if((yank->type == CE_VIM_YANK_TYPE_STRING ||
                             yank->type == CE_VIM_YANK_TYPE_LINE) &&
                             yank->text){
                              char* itr = yank->text;

                              while(*itr){
                                   ce_terminal_send_key(terminal, *itr);
                                   itr++;
                              }

                              app->vim.mode = CE_VIM_MODE_INSERT;
                         }
                         return;
                    }
               }
          }
     }

     // as long as vim isn't in the middle of handling keys, in insert mode vim returns VKH_HANDLED_KEY TODO: is that what we want?
     if(app->last_vim_handle_result != CE_VIM_PARSE_IN_PROGRESS &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONTINUE &&
        app->vim.mode != CE_VIM_MODE_INSERT &&
        app->vim.mode != CE_VIM_MODE_REPLACE){
          // append to keys
          if(app->key_count < APP_MAX_KEY_COUNT){
               app->keys[app->key_count] = key;
               app->key_count++;

               bool no_matches = true;
               for(int64_t i = 0; i < app->key_binds.count; ++i){
                    if(int_strneq(app->key_binds.binds[i].keys, app->keys, app->key_count)){
                         no_matches = false;
                         // if we have matches, but don't completely match, then wait for more keypresses,
                         // otherwise, execute the action
                         if(app->key_binds.binds[i].key_count == app->key_count){
                              CeCommand_t* command = &app->key_binds.binds[i].command;
                              CeCommandFunc_t* command_func = NULL;
                              CeCommandEntry_t* entry = NULL;
                              for(int64_t c = 0; c < app->command_entry_count; ++c){
                                   entry = app->command_entries + c;
                                   if(strcmp(entry->name, command->name) == 0){
                                        command_func = entry->func;
                                        break;
                                   }
                              }

                              if(command_func){
                                   CeCommandStatus_t cs = command_func(command, app);

                                   switch(cs){
                                   default:
                                        app->key_count = 0;
                                        return;
                                   case CE_COMMAND_NO_ACTION:
                                        break;
                                   case CE_COMMAND_FAILURE:
                                        ce_log("'%s' failed", entry->name);
                                        break;
                                   case CE_COMMAND_PRINT_HELP:
                                        ce_log("command help:\n'%s' %s\n", entry->name, entry->description);
                                        break;
                                   }
                              }else{
                                   ce_log("unknown command: '%s'", command->name);
                              }

                              app->key_count = 0;
                              break;
                         }else{
                              return;
                         }
                    }
               }

               if(no_matches){
                    app->vim.current_command[0] = 0;
                    for(int64_t i = 0; i < app->key_count - 1; ++i){
                         ce_vim_append_key(&app->vim, app->keys[i]);
                    }

                    app->key_count = 0;
               }
          }
     }

     if(view){
          if(key == KEY_ENTER){
               if(view->buffer == app->buffer_list_buffer){
                    CeBufferNode_t* itr = app->buffer_node_head;
                    int64_t index = 0;
                    while(itr){
                         if(index == view->cursor.y){
                              ce_view_switch_buffer(view, itr->buffer, &app->vim, &app->config_options, true);
                              break;
                         }
                         itr = itr->next;
                         index++;
                    }
               }else if(!app->input_mode && view->buffer == app->yank_list_buffer){
                    // TODO: move to command
                    app->edit_register = -1;
                    int64_t line = view->cursor.y;
                    CeVimYank_t* selected_yank = NULL;
                    for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
                         CeVimYank_t* yank = app->vim.yanks + i;
                         if(yank->text != NULL){
                              int64_t line_count = 2;
                              line_count += ce_util_count_string_lines(yank->text);
                              line -= line_count;
                              if(line <= 0){
                                   app->edit_register = i;
                                   selected_yank = yank;
                                   break;
                              }
                         }
                    }

                    if(app->edit_register >= 0){
                         app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "EDIT YANK");
                         ce_buffer_insert_string(app->input_view.buffer, selected_yank->text, (CePoint_t){0, 0});
                         app->input_view.cursor.y = app->input_view.buffer->line_count;
                         if(app->input_view.cursor.y) app->input_view.cursor.y--;
                         app->input_view.cursor.x = ce_utf8_strlen(app->input_view.buffer->lines[app->input_view.cursor.y]);
                    }
               }else if(!app->input_mode && view->buffer == app->macro_list_buffer){
                    // TODO: move to command
                    app->edit_register = -1;
                    int64_t line = view->cursor.y;
                    char* macro_string = NULL;
                    for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
                         CeRuneNode_t* rune_node = app->macros.rune_head[i];
                         if(rune_node){
                              line -= 2;
                              if(line <= 2){
                                   app->edit_register = i;
                                   CeRune_t* rune_string = ce_rune_node_string(rune_node);
                                   macro_string = ce_rune_string_to_char_string(rune_string);
                                   free(rune_string);
                                   break;
                              }
                         }
                    }

                    if(app->edit_register >= 0){
                         app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "EDIT MACRO");
                         ce_buffer_insert_string(app->input_view.buffer, macro_string, (CePoint_t){0, 0});
                         app->input_view.cursor.y = app->input_view.buffer->line_count;
                         if(app->input_view.cursor.y) app->input_view.cursor.y--;
                         app->input_view.cursor.x = ce_utf8_strlen(app->input_view.buffer->lines[app->input_view.cursor.y]);
                         free(macro_string);
                    }
               }else if(app->input_mode){
                    if(app->input_view.buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                         apply_completion(app, view);
                         if(strcmp(app->input_view.buffer->name, "LOAD FILE") == 0){
                              char* base_directory = buffer_base_directory(view->buffer, &app->terminal_list);
                              char filepath[PATH_MAX];
                              for(int64_t i = 0; i < app->input_view.buffer->line_count; i++){
                                   if(base_directory && app->input_view.buffer->lines[i][0] != '/'){
                                        snprintf(filepath, PATH_MAX, "%s/%s", base_directory, app->input_view.buffer->lines[i]);
                                   }else{
                                        strncpy(filepath, app->input_view.buffer->lines[i], PATH_MAX);
                                   }
                                   load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                                       true, filepath);
                              }

                              free(base_directory);
                         }else if(strcmp(app->input_view.buffer->name, "SEARCH") == 0 ||
                                  strcmp(app->input_view.buffer->name, "REVERSE SEARCH") == 0 ||
                                  strcmp(app->input_view.buffer->name, "REGEX SEARCH") == 0 ||
                                  strcmp(app->input_view.buffer->name, "REGEX REVERSE SEARCH") == 0){
                              ce_history_insert(&app->search_history, app->input_view.buffer->lines[0]);

                              // update yanks
                              CeVimYank_t* yank = app->vim.yanks + ce_vim_yank_register_index('/');
                              free(yank->text);
                              yank->text = strdup(app->input_view.buffer->lines[0]);
                              yank->type = CE_VIM_YANK_TYPE_STRING;

                              // clear input buffer
                              app->input_view.buffer->lines[0][0] = 0;

                              // insert jump
                              CeAppViewData_t* view_data = view->user_data;
                              CeJumpList_t* jump_list = &view_data->jump_list;
                              CeDestination_t destination = {};
                              destination.point = view->cursor;
                              strncpy(destination.filepath, view->buffer->name, PATH_MAX);
                              ce_jump_list_insert(jump_list, destination);
                         }else if(strcmp(app->input_view.buffer->name, "COMMAND") == 0){
                              char* end_of_number = app->input_view.buffer->lines[0];
                              int64_t line_number = strtol(app->input_view.buffer->lines[0], &end_of_number, 10);
                              if(end_of_number > app->input_view.buffer->lines[0]){
                                   // if the command entered was a number, go to that line
                                   if(line_number >= 0 && line_number < view->buffer->line_count){
                                        view->cursor.y = line_number - 1;
                                        view->cursor.x = ce_vim_soft_begin_line(view->buffer, view->cursor.y);
                                        ce_view_follow_cursor(view, app->config_options.horizontal_scroll_off,
                                                              app->config_options.vertical_scroll_off,
                                                              app->config_options.tab_width);
                                        ce_view_center(view);
                                   }
                              }else{
                                   // convert and run the command
                                   CeCommand_t command = {};
                                   if(!ce_command_parse(&command, app->input_view.buffer->lines[0])){
                                        ce_log("failed to parse command: '%s'\n", app->input_view.buffer->lines[0]);
                                   }else{
                                        CeCommandFunc_t* command_func = NULL;
                                        for(int64_t i = 0; i < app->command_entry_count; i++){
                                             CeCommandEntry_t* entry = app->command_entries + i;
                                             if(strcmp(entry->name, command.name) == 0){
                                                  command_func = entry->func;
                                                  break;
                                             }
                                        }

                                        if(command_func){
                                             // TODO: compress this, we do it a lot, and I'm sure there will be more we need to do in the future
                                             app->input_mode = false;
                                             app->vim.mode = CE_VIM_MODE_NORMAL;

                                             command_func(&command, app);
                                             ce_history_insert(&app->command_history, app->input_view.buffer->lines[0]);

                                             return;
                                        }else{
                                             ce_log("unknown command: '%s'\n", command.name);
                                        }
                                   }
                              }
                         }else if(strcmp(app->input_view.buffer->name, "EDIT YANK") == 0){
                              CeVimYank_t* yank = app->vim.yanks + app->edit_register;
                              CeVimYankType_t yank_type = yank->type;
                              ce_vim_yank_free(yank);
                              if(yank_type == CE_VIM_YANK_TYPE_BLOCK){
                                   yank->block_line_count = app->input_view.buffer->line_count;
                                   yank->block = malloc(yank->block_line_count * sizeof(*yank->block));
                                   for(int64_t i = 0; i < app->input_view.buffer->line_count; i++){
                                        if(strlen(app->input_view.buffer->lines[i])){
                                             yank->block[i] = strdup(app->input_view.buffer->lines[i]);
                                        }else{
                                             yank->block[i] = NULL;
                                        }
                                   }
                                   yank->type = yank_type;
                              }else{
                                   yank->text = ce_buffer_dupe(app->input_view.buffer);
                                   yank->type = yank_type;
                              }
                         }else if(strcmp(app->input_view.buffer->name, "EDIT MACRO") == 0){
                              CeRune_t* rune_string = ce_char_string_to_rune_string(app->input_view.buffer->lines[0]);
                              if(rune_string){
                                   ce_rune_node_free(app->macros.rune_head + app->edit_register);
                                   CeRune_t* itr = rune_string;
                                   while(*itr){
                                        ce_rune_node_insert(app->macros.rune_head + app->edit_register, *itr);
                                        itr++;
                                   }

                                   free(rune_string);
                              }
                         }else if(strcmp(app->input_view.buffer->name, "SWITCH BUFFER") == 0){
                              CeAppViewData_t* view_data = view->user_data;
                              CeJumpList_t* jump_list = &view_data->jump_list;
                              CeBufferNode_t* itr = app->buffer_node_head;
                              while(itr){
                                   if(strcmp(itr->buffer->name, app->input_view.buffer->lines[0]) == 0){
                                        ce_view_switch_buffer(view, itr->buffer, &app->vim, &app->config_options, jump_list);
                                        break;
                                   }
                                   itr = itr->next;
                              }
                         }else if(strcmp(app->input_view.buffer->name, UNSAVED_BUFFERS_DIALOGUE) == 0){
                              if(strcmp(app->input_view.buffer->lines[0], "y") == 0 ||
                                 strcmp(app->input_view.buffer->lines[0], "Y") == 0){
                                   app->quit = true;
                              }
                         }else if(strcmp(app->input_view.buffer->name, "REPLACE ALL") == 0){
                              int64_t index = ce_vim_yank_register_index('/');
                              CeVimYank_t* yank = app->vim.yanks + index;
                              if(yank->text){
                                   replace_all(view, &app->vim, yank->text, app->input_view.buffer->lines[0]);
                              }
                         }
                    }

                    // TODO: compress this, we do it a lot, and I'm sure there will be more we need to do in the future
                    app->input_mode = false;
                    app->vim.mode = CE_VIM_MODE_NORMAL;
                    return;
               }else{
                    key = CE_NEWLINE;
               }
          }else if(key == CE_TAB){ // TODO: configure auto complete key?
               if(apply_completion(app, view)) return;
          }else if(key == 14){ // ctrl + n
               CeComplete_t* complete = ce_app_is_completing(app);
               if(app->vim.mode == CE_VIM_MODE_INSERT && complete){
                    ce_complete_next_match(complete);
                    build_complete_list(app->complete_list_buffer, complete);
                    return;
               }
          }else if(key == 16){ // ctrl + p
               CeComplete_t* complete = ce_app_is_completing(app);
               if(app->vim.mode == CE_VIM_MODE_INSERT && complete){
                    ce_complete_previous_match(complete);
                    build_complete_list(app->complete_list_buffer, complete);
                    return;
               }
          }else if(key == KEY_ESCAPE && app->input_mode && app->vim.mode == CE_VIM_MODE_NORMAL){ // Escape
               ce_history_reset_current(&app->command_history);
               ce_history_reset_current(&app->search_history);
               app->input_mode = false;
               app->vim.mode = CE_VIM_MODE_NORMAL;

               CeComplete_t* complete = ce_app_is_completing(app);
               if(complete) ce_complete_reset(complete);
               return;
          }else if(key == 'd' && view->buffer == app->buffer_list_buffer){ // Escape
               CeBufferNode_t* itr = app->buffer_node_head;
               int64_t buffer_index = 0;
               while(itr){
                    if(buffer_index == view->cursor.y) break;
                    buffer_index++;
                    itr = itr->next;
               }

               if(buffer_index == view->cursor.y &&
                  (itr->buffer != app->buffer_list_buffer &&
                   itr->buffer != app->yank_list_buffer &&
                   itr->buffer != app->complete_list_buffer &&
                   itr->buffer != app->macro_list_buffer &&
                   itr->buffer != app->mark_list_buffer &&
                   itr->buffer != app->jump_list_buffer &&
                   itr->buffer != g_ce_log_buffer)){
                    // find all the views showing this buffer and switch to a different view
                    for(int64_t t = 0; t < app->tab_list_layout->tab_list.tab_count; t++){
                         CeLayoutBufferInViewsResult_t result = ce_layout_buffer_in_views(app->tab_list_layout->tab_list.tabs[t], itr->buffer);
                         for(int64_t i = 0; i < result.layout_count; i++){
                              result.layouts[i]->view.buffer = app->buffer_list_buffer;
                         }
                    }

                    ce_buffer_node_delete(&app->buffer_node_head, itr->buffer);
               }
          }

          if(app->input_mode){
               if(strcmp(app->input_view.buffer->name, "COMMAND") == 0 && app->input_view.buffer->line_count){
                    handle_input_history_key(key, &app->command_history, app->input_view.buffer, &app->input_view.cursor);
               }else if((strcmp(app->input_view.buffer->name, "SEARCH") == 0 ||
                         strcmp(app->input_view.buffer->name, "REVERSE SEARCH") == 0 ||
                         strcmp(app->input_view.buffer->name, "REGEX SEARCH") == 0 ||
                         strcmp(app->input_view.buffer->name, "REGEX REVERSE SEARCH") == 0) && app->input_view.buffer->line_count){
                    handle_input_history_key(key, &app->search_history, app->input_view.buffer, &app->input_view.cursor);
               }

               CeAppBufferData_t* buffer_data = app->input_view.buffer->app_data;

               app->last_vim_handle_result = ce_vim_handle_key(&app->vim, &app->input_view, key, &buffer_data->vim, &app->config_options);

               if(app->vim.mode == CE_VIM_MODE_INSERT && app->input_view.buffer->line_count){
                    if(strcmp(app->input_view.buffer->name, "COMMAND") == 0){
                         ce_complete_match(&app->command_complete, app->input_view.buffer->lines[0]);
                         build_complete_list(app->complete_list_buffer, &app->command_complete);
                    }else if(strcmp(app->input_view.buffer->name, "LOAD FILE") == 0){
                         char* base_directory = buffer_base_directory(view->buffer, &app->terminal_list);
                         complete_files(&app->load_file_complete, app->input_view.buffer->lines[0], base_directory);
                         free(base_directory);
                         build_complete_list(app->complete_list_buffer, &app->load_file_complete);
                    }else if(strcmp(app->input_view.buffer->name, "SWITCH BUFFER") == 0){
                         ce_complete_match(&app->switch_buffer_complete, app->input_view.buffer->lines[0]);
                         build_complete_list(app->complete_list_buffer, &app->switch_buffer_complete);
                    }
               }
          }else{
               CeAppBufferData_t* buffer_data = view->buffer->app_data;
               app->last_vim_handle_result = ce_vim_handle_key(&app->vim, view, key, &buffer_data->vim, &app->config_options);

               // A "jump" is one of the following commands: "'", "`", "G", "/", "?", "n",
               // "N", "%", "(", ")", "[[", "]]", "{", "}", ":s", ":tag", "L", "M", "H" and
               if(app->vim.current_action.verb.function == ce_vim_verb_motion){
                    if(app->vim.current_action.motion.function == ce_vim_motion_mark ||
                       app->vim.current_action.motion.function == ce_vim_motion_end_of_file ||
                       app->vim.current_action.motion.function == ce_vim_motion_search_next ||
                       app->vim.current_action.motion.function == ce_vim_motion_search_prev ||
                       app->vim.current_action.motion.function == ce_vim_motion_match_pair){
                         CeAppViewData_t* view_data = view->user_data;
                         CeJumpList_t* jump_list = &view_data->jump_list;
                         CeDestination_t destination = {};
                         destination.point = view->cursor;
                         strncpy(destination.filepath, view->buffer->name, PATH_MAX);
                         ce_jump_list_insert(jump_list, destination);
                    }else if(app->vim.current_action.motion.function == ce_vim_motion_search_word_forward ||
                             app->vim.current_action.motion.function == ce_vim_motion_search_word_backward ||
                             app->vim.current_action.motion.function == ce_vim_motion_search_next ||
                             app->vim.current_action.motion.function == ce_vim_motion_search_prev){
                         app->highlight_search = true;
                    }
               }

               if(app->last_vim_handle_result == CE_VIM_PARSE_COMPLETE &&
                  app->vim.current_action.repeatable){
                    app->last_macro_register = 0;
               }
          }
     }else{
          if(key == KEY_ESCAPE){
               CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
               CeLayout_t* current_layout = tab_layout->tab.current;
               CeRect_t layout_rect = {};

               switch(current_layout->type){
               default:
                    assert(!"unexpected current layout type");
                    return;
               case CE_LAYOUT_TYPE_LIST:
                    layout_rect = current_layout->list.rect;
                    break;
               case CE_LAYOUT_TYPE_TAB:
                    layout_rect = current_layout->tab.rect;
                    break;
               case CE_LAYOUT_TYPE_TAB_LIST:
                    layout_rect = current_layout->tab_list.rect;
                    break;
               }

               tab_layout->tab.current = ce_layout_find_at(tab_layout, (CePoint_t){layout_rect.left, layout_rect.top});
               return;
          }
     }

     // incremental search
     if(app->input_mode){
          if(strcmp(app->input_view.buffer->name, "SEARCH") == 0){
               if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                    CePoint_t match_point = ce_buffer_search_forward(view->buffer, view->cursor, app->input_view.buffer->lines[0]);
                    if(match_point.x >= 0){
                         scroll_to_and_center_if_offscreen(view, match_point, &app->config_options);
                    }else{
                         view->cursor = app->search_start;
                    }
               }else{
                    view->cursor = app->search_start;
               }
          }else if(strcmp(app->input_view.buffer->name, "REVERSE SEARCH") == 0){
               if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                    CePoint_t match_point = ce_buffer_search_backward(view->buffer, view->cursor, app->input_view.buffer->lines[0]);
                    if(match_point.x >= 0){
                         scroll_to_and_center_if_offscreen(view, match_point, &app->config_options);
                    }else{
                         view->cursor = app->search_start;
                    }
               }else{
                    view->cursor = app->search_start;
               }
          }else if(strcmp(app->input_view.buffer->name, "REGEX SEARCH") == 0){
               if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                    regex_t regex = {};
                    int rc = regcomp(&regex, app->input_view.buffer->lines[0], REG_EXTENDED);
                    if(rc != 0){
                         char error_buffer[BUFSIZ];
                         regerror(rc, &regex, error_buffer, BUFSIZ);
                         ce_log("regcomp() failed: '%s'", error_buffer);
                    }else{
                         CeRegexSearchResult_t result = ce_buffer_regex_search_forward(view->buffer, view->cursor, &regex);
                         if(result.point.x >= 0){
                              scroll_to_and_center_if_offscreen(view, result.point, &app->config_options);
                         }else{
                              view->cursor = app->search_start;
                         }
                    }
               }else{
                    view->cursor = app->search_start;
               }
          }else if(strcmp(app->input_view.buffer->name, "REGEX REVERSE SEARCH") == 0){
               if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                    regex_t regex = {};
                    int rc = regcomp(&regex, app->input_view.buffer->lines[0], REG_EXTENDED);
                    if(rc != 0){
                         char error_buffer[BUFSIZ];
                         regerror(rc, &regex, error_buffer, BUFSIZ);
                         ce_log("regcomp() failed: '%s'", error_buffer);
                    }else{
                         CeRegexSearchResult_t result = ce_buffer_regex_search_backward(view->buffer, view->cursor, &regex);
                         if(result.point.x >= 0){
                              scroll_to_and_center_if_offscreen(view, result.point, &app->config_options);
                         }else{
                              view->cursor = app->search_start;
                         }
                    }
               }else{
                    view->cursor = app->search_start;
               }
          }
     }
}

void print_help(char* program){
     printf("usage  : %s [options] [file]\n", program);
     printf("options:\n");
     printf("  -c <config file> path to shared object configuration\n");
}

int main(int argc, char** argv){
     const char* config_filepath = NULL;
     int last_arg_index = 0;

     // setup signal handler
     signal(SIGINT, handle_sigint);

     // parse args
     {
          char c;
          while((c = getopt(argc, argv, "c:h")) != -1){
               switch(c){
               case 'c':
                    config_filepath = optarg;
                    break;
               case 'h':
               default:
                    print_help(argv[0]);
                    return 1;
               }
          }

          last_arg_index = optind;
     }

     setlocale(LC_ALL, "");

     // TODO: allocate this on the heap when/if it gets too big?
     CeApp_t app = {};

     // init log buffer
     {
          g_ce_log_buffer = new_buffer();
          ce_buffer_alloc(g_ce_log_buffer, 1, "[log]");
          ce_buffer_node_insert(&app.buffer_node_head, g_ce_log_buffer);
          g_ce_log_buffer->status = CE_BUFFER_STATUS_READONLY;
          g_ce_log_buffer->no_line_numbers = true;
     }

     char log_filepath[PATH_MAX];
     snprintf(log_filepath, PATH_MAX, "%s/ce.log", getenv("HOME"));
     if(!ce_log_init(log_filepath)){
          return 1;
     }

     // init buffers
     {
          app.buffer_list_buffer = new_buffer();
          app.yank_list_buffer = new_buffer();
          app.complete_list_buffer = new_buffer();
          app.macro_list_buffer = new_buffer();
          app.mark_list_buffer = new_buffer();
          app.jump_list_buffer = new_buffer();
          CeBuffer_t* scratch_buffer = new_buffer();

          ce_buffer_alloc(app.buffer_list_buffer, 1, "[buffers]");
          ce_buffer_node_insert(&app.buffer_node_head, app.buffer_list_buffer);
          ce_buffer_alloc(app.yank_list_buffer, 1, "[yanks]");
          ce_buffer_node_insert(&app.buffer_node_head, app.yank_list_buffer);
          ce_buffer_alloc(app.complete_list_buffer, 1, "[completions]");
          ce_buffer_node_insert(&app.buffer_node_head, app.complete_list_buffer);
          ce_buffer_alloc(app.macro_list_buffer, 1, "[macros]");
          ce_buffer_node_insert(&app.buffer_node_head, app.macro_list_buffer);
          ce_buffer_alloc(app.mark_list_buffer, 1, "[marks]");
          ce_buffer_node_insert(&app.buffer_node_head, app.mark_list_buffer);
          ce_buffer_alloc(app.jump_list_buffer, 1, "[jumps]");
          ce_buffer_node_insert(&app.buffer_node_head, app.jump_list_buffer);
          ce_buffer_alloc(scratch_buffer, 1, "scratch");
          ce_buffer_node_insert(&app.buffer_node_head, scratch_buffer);

          app.buffer_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.yank_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.complete_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.macro_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.mark_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.jump_list_buffer->status = CE_BUFFER_STATUS_NONE;
          scratch_buffer->status = CE_BUFFER_STATUS_NONE;

          app.buffer_list_buffer->no_line_numbers = true;
          app.yank_list_buffer->no_line_numbers = true;
          app.complete_list_buffer->no_line_numbers = true;
          app.macro_list_buffer->no_line_numbers = true;
          app.mark_list_buffer->no_line_numbers = true;
          app.jump_list_buffer->no_line_numbers = true;

          CeAppBufferData_t* buffer_data = app.complete_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_completions;

          buffer_data = app.buffer_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.yank_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.macro_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.mark_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.jump_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = scratch_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;

          if(argc > 1){
               for(int64_t i = last_arg_index; i < argc; i++){
                    CeBuffer_t* buffer = new_buffer();
                    if(ce_buffer_load_file(buffer, argv[i])){
                         ce_buffer_node_insert(&app.buffer_node_head, buffer);
                         determine_buffer_syntax(buffer);
                    }else{
                         free(buffer);
                    }
               }
          }else{
               CeBuffer_t* buffer = new_buffer();
               ce_buffer_alloc(buffer, 1, "unnamed");
          }
     }

     // init ncurses
     {
          initscr();
          nodelay(stdscr, TRUE);
          keypad(stdscr, TRUE);
          cbreak();
          noecho();
          raw();

          if(has_colors() == FALSE){
               printf("Your terminal doesn't support colors. what year do you live in?\n");
               return 1;
          }

          start_color();
          use_default_colors();

          define_key("\x11", KEY_CLOSE);     // ctrl + q    (17) (0x11) ASCII "DC1" Device Control 1
          define_key("\x12", KEY_REDO);
          define_key(NULL, KEY_ENTER);       // Blow away enter
          define_key("\x0D", KEY_ENTER);     // Enter       (13) (0x0D) ASCII "CR"  NL Carriage Return
     }

     ce_app_init_default_commands(&app);
     ce_vim_init(&app.vim);

     // init layout
     {
          CeLayout_t* tab_layout = ce_layout_tab_init(app.buffer_node_head->buffer);
          app.tab_list_layout = ce_layout_tab_list_init(tab_layout);
          ce_app_update_terminal_view(&app);
     }

     // setup input buffer
     {
          CeBuffer_t* buffer = new_buffer();
          ce_buffer_alloc(buffer, 1, "input");
          app.input_view.buffer = buffer;
          app.input_view.buffer->no_line_numbers = true;
          ce_buffer_node_insert(&app.buffer_node_head, buffer);
     }

     // init user config
     if(config_filepath){
          if(!user_config_init(&app.user_config, config_filepath)) return 1;
          app.user_config.init_func(&app);
     }else{
          // default config

          // config options
          CeConfigOptions_t* config_options = &app.config_options;
          config_options->tab_width = 5;
          config_options->horizontal_scroll_off = 10;
          config_options->vertical_scroll_off = 5;
          config_options->insert_spaces_on_tab = true;
          config_options->terminal_scroll_back = 1024;
          config_options->line_number = CE_LINE_NUMBER_NONE;

          // keybinds
          CeKeyBindDef_t normal_mode_bind_defs[] = {
               {{'\\', 'q'}, "quit"},
               {{23, 'h'}, "select_adjacent_layout left"}, // ctrl w
               {{23, 'l'}, "select_adjacent_layout right"}, // ctrl w
               {{23, 'k'}, "select_adjacent_layout up"}, // ctrl w
               {{23, 'j'}, "select_adjacent_layout down"}, // ctrl w
               {{22}, "save_buffer"}, // ctrl s
               {{'\\', 'b'}, "show_buffers"},
               {{6}, "load_file"}, // ctrl f
               {{'/'}, "search forward"},
               {{'?'}, "search backward"},
               {{':'}, "command"},
               {{'g', 't'}, "select_adjacent_tab right"},
               {{'g', 'T'}, "select_adjacent_tab left"},
               {{'\\', '/'}, "regex_search forward"},
               {{'\\', '?'}, "regex_search backward"},
               {{'g', 'r'}, "redraw"},
               {{'\\', 'f'}, "reload_file"},
               {{2}, "switch_buffer"}, // ctrl b
               {{9}, "jump_list previous"}, // ctrl + o
               {{15}, "jump_list next"}, // ctrl + i
               {{'K'}, "man_page_on_word_under_cursor"},
          };

          ce_convert_bind_defs(&app.key_binds, normal_mode_bind_defs, sizeof(normal_mode_bind_defs) / sizeof(normal_mode_bind_defs[0]));

          // syntax
          {
               CeSyntaxDef_t* syntax_defs = malloc(CE_SYNTAX_COLOR_COUNT * sizeof(*syntax_defs));
               syntax_defs[CE_SYNTAX_COLOR_NORMAL].fg = COLOR_DEFAULT;
               syntax_defs[CE_SYNTAX_COLOR_NORMAL].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_TYPE].fg = COLOR_BRIGHT_BLUE;
               syntax_defs[CE_SYNTAX_COLOR_TYPE].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_KEYWORD].fg = COLOR_BLUE;
               syntax_defs[CE_SYNTAX_COLOR_KEYWORD].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_CONTROL].fg = COLOR_YELLOW;
               syntax_defs[CE_SYNTAX_COLOR_CONTROL].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_CAPS_VAR].fg = COLOR_MAGENTA;
               syntax_defs[CE_SYNTAX_COLOR_CAPS_VAR].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_COMMENT].fg = COLOR_GREEN;
               syntax_defs[CE_SYNTAX_COLOR_COMMENT].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_STRING].fg = COLOR_RED;
               syntax_defs[CE_SYNTAX_COLOR_STRING].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_CHAR_LITERAL].fg = COLOR_RED;
               syntax_defs[CE_SYNTAX_COLOR_CHAR_LITERAL].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_NUMBER_LITERAL].fg = COLOR_MAGENTA;
               syntax_defs[CE_SYNTAX_COLOR_NUMBER_LITERAL].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_PREPROCESSOR].fg = COLOR_BRIGHT_MAGENTA;
               syntax_defs[CE_SYNTAX_COLOR_PREPROCESSOR].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_TRAILING_WHITESPACE].fg = COLOR_RED;
               syntax_defs[CE_SYNTAX_COLOR_TRAILING_WHITESPACE].bg = COLOR_RED;
               syntax_defs[CE_SYNTAX_COLOR_VISUAL].fg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_VISUAL].bg = COLOR_WHITE;
               syntax_defs[CE_SYNTAX_COLOR_MATCH].fg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_MATCH].bg = COLOR_WHITE;
               syntax_defs[CE_SYNTAX_COLOR_CURRENT_LINE].fg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_CURRENT_LINE].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_DIFF_ADD].fg = COLOR_GREEN;
               syntax_defs[CE_SYNTAX_COLOR_DIFF_ADD].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_DIFF_REMOVE].fg = COLOR_RED;
               syntax_defs[CE_SYNTAX_COLOR_DIFF_REMOVE].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_DIFF_HEADER].fg = COLOR_MAGENTA;
               syntax_defs[CE_SYNTAX_COLOR_DIFF_HEADER].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_DIFF_COMMENT].fg = COLOR_BLUE;
               syntax_defs[CE_SYNTAX_COLOR_DIFF_COMMENT].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_COMPLETE_SELECTED].fg = COLOR_WHITE;;
               syntax_defs[CE_SYNTAX_COLOR_COMPLETE_SELECTED].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_COMPLETE_MATCH].fg = COLOR_BRIGHT_CYAN;
               syntax_defs[CE_SYNTAX_COLOR_COMPLETE_MATCH].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_LINE_NUMBER].fg = COLOR_DEFAULT;
               syntax_defs[CE_SYNTAX_COLOR_LINE_NUMBER].bg = COLOR_DEFAULT;

               app.config_options.ui_fg_color = COLOR_DEFAULT;
               app.config_options.ui_bg_color = COLOR_WHITE;

               app.syntax_defs = syntax_defs;
          }
     }

     ce_app_init_command_completion(&app);

     draw(&app);

     // init draw thread
     struct timeval previous_draw_time = {};
     struct timeval current_draw_time = {};
     uint64_t time_since_last_draw = 0;

     // main loop
     while(!app.quit){
          gettimeofday(&current_draw_time, NULL);
          time_since_last_draw = time_between(previous_draw_time, current_draw_time);

          // figure out our current view rect
          CeView_t* view = NULL;
          CeLayout_t* tab_layout = app.tab_list_layout->tab_list.current;

          switch(tab_layout->tab.current->type){
          default:
               break;
          case CE_LAYOUT_TYPE_VIEW:
               view = &tab_layout->tab.current->view;
               break;
          }

          int key = getch();

          if(time_since_last_draw >= DRAW_USEC_LIMIT){
               if(app.ready_to_draw){
                    draw(&app);
                    app.ready_to_draw = false;
                    previous_draw_time = current_draw_time;
               }else{
                    CeTerminalNode_t* itr = app.terminal_list.head;
                    bool do_draw = false;

                    while(itr){
                         if(itr->terminal.ready_to_draw){
                              do_draw = true;
                              itr->terminal.ready_to_draw = false;
                              break;
                         }
                         itr = itr->next;
                    }

                    // if we did draw, turn of any outstanding draw flags
                    if(do_draw){
                         draw(&app);
                         app.ready_to_draw = false;
                    }
               }
          }

          // TODO: compress with below
          if(view){
               CeTerminal_t* terminal = ce_buffer_in_terminal_list(view->buffer, &app.terminal_list);
               if(terminal){
                    if(app.vim.mode == CE_VIM_MODE_INSERT){
                         view->scroll.x = 0;
                         view->scroll.y = terminal->start_line;
                    }else{
                         ce_view_follow_cursor(view, 0, 0, app.config_options.tab_width);
                    }
               }
          }

          if(key == ERR){
               sleep(0);
               continue;
          }

          // handle input from the user
          app_handle_key(&app, view, key);

          // update refs to view and tab_layout
          tab_layout = app.tab_list_layout->tab_list.current;

          switch(tab_layout->tab.current->type){
          default:
               break;
          case CE_LAYOUT_TYPE_VIEW:
               view = &tab_layout->tab.current->view;
               break;
          }

          if(view){
               CeTerminal_t* terminal = ce_buffer_in_terminal_list(view->buffer, &app.terminal_list);
               if(terminal){
                    if(app.vim.mode == CE_VIM_MODE_INSERT){
                         view->scroll.x = 0;
                         view->scroll.y = terminal->start_line;
                    }else{
                         ce_view_follow_cursor(view, 0, 0, app.config_options.tab_width);
                    }
               }else{
                    ce_view_follow_cursor(view, app.config_options.horizontal_scroll_off, app.config_options.vertical_scroll_off,
                                          app.config_options.tab_width);
               }

               // setup input view overlay if we are
               if(app.input_mode) input_view_overlay(&app.input_view, view);
          }

          // update any list buffers if they are in view
          if(ce_layout_buffer_in_view(tab_layout, app.buffer_list_buffer)){
               build_buffer_list(app.buffer_list_buffer, app.buffer_node_head);
          }

          if(ce_layout_buffer_in_view(tab_layout, app.yank_list_buffer)){
               build_yank_list(app.yank_list_buffer, app.vim.yanks);
          }

          if(ce_layout_buffer_in_view(tab_layout, app.macro_list_buffer)){
               build_macro_list(app.macro_list_buffer, &app.macros);
          }

          if(view && ce_layout_buffer_in_view(tab_layout, app.mark_list_buffer)){
               CeAppBufferData_t* buffer_data = view->buffer->app_data;
               build_mark_list(app.mark_list_buffer, &buffer_data->vim);
          }

          if(view && ce_layout_buffer_in_view(tab_layout, app.jump_list_buffer)){
               CeAppViewData_t* view_data = view->user_data;
               build_jump_list(app.jump_list_buffer, &view_data->jump_list);
          }

          // tell the draw thread we are ready to draw
          app.ready_to_draw = true;
     }

     // cleanup
     if(config_filepath){
          app.user_config.free_func(&app);
          user_config_free(&app.user_config);
     }

     ce_macros_free(&app.macros);
     ce_complete_free(&app.command_complete);
     ce_complete_free(&app.load_file_complete);
     ce_complete_free(&app.switch_buffer_complete);

     CeKeyBinds_t* binds = &app.key_binds;
     for(int64_t i = 0; i < binds->count; ++i){
          ce_command_free(&binds->binds[i].command);
          if(!binds->binds[i].key_count) continue;
          free(binds->binds[i].keys);
     }
     free(binds->binds);

     free(app.command_entries);

     // unlink terminal node from buffer no list
     {
          CeBufferNode_t* itr = app.buffer_node_head;
          CeBufferNode_t* prev = NULL;
          while(itr){
               CeTerminal_t* terminal = ce_buffer_in_terminal_list(itr->buffer, &app.terminal_list);
               if(terminal){
                    if(prev){
                         prev->next = itr->next;
                    }else{
                         app.buffer_node_head = itr->next;
                    }
                    free(itr);
                    break;
               }

               itr = itr->next;
          }
     }

     ce_buffer_node_free(&app.buffer_node_head);

     CeTerminalNode_t* itr = app.terminal_list.head;
     while(itr){
          CeTerminalNode_t* tmp = itr;
          itr = itr->next;
          ce_terminal_free(&tmp->terminal);
          free(tmp);
     }

     ce_layout_free(&app.tab_list_layout);
     ce_vim_free(&app.vim);
     endwin();
     return 0;
}
