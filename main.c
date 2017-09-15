#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <ncurses.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <signal.h>

#include "ce_app.h"

FILE* g_ce_log = NULL;

#define UNSAVED_BUFFERS_DIALOGUE "UNSAVED BUFFERS, QUIT? [Y/N]"

// limit to 60 fps
#define DRAW_USEC_LIMIT 16666

void handle_sigint(int signal){
     // pass
}

bool user_config_init(CeUserConfig_t* user_config, const char* filepath){
     user_config->handle = dlopen(filepath, RTLD_LAZY);
     if(!user_config->handle){
          ce_log("dlopen() failed: '%s'\n", dlerror());
          return false;
     }

     user_config->filepath = strdup(filepath);
     user_config->init_func = dlsym(user_config->handle, "ce_init");
     if(!user_config->init_func){
          ce_log("missing 'ce_init()' in %s\n", user_config->filepath);
          return false;
     }

     user_config->free_func = dlsym(user_config->handle, "ce_free");
     if(!user_config->free_func){
          ce_log("missing 'ce_init()' in %s\n", user_config->filepath);
          return false;
     }

     return true;
}

void user_config_free(CeUserConfig_t* user_config){
     free(user_config->filepath);
     // NOTE: comment out dlclose() so valgrind can get a helpful stack frame
     dlclose(user_config->handle);
     memset(user_config, 0, sizeof(*user_config));
}

char* directory_from_filename(const char* filename){
     const char* last_slash = strrchr(filename, '/');
     char* directory = NULL;
     if(last_slash) directory = strndup(filename, last_slash - filename);
     return directory;
}

bool buffer_append_on_new_line(CeBuffer_t* buffer, const char* string){
     int64_t last_line = buffer->line_count;
     if(last_line) last_line--;
     int64_t line_len = ce_utf8_strlen(buffer->lines[last_line]);
     if(line_len){
          if(!ce_buffer_insert_string(buffer, "\n", (CePoint_t){line_len, last_line})) return false;
     }
     int64_t next_line = last_line;
     if(line_len) next_line++;
     return ce_buffer_insert_string(buffer, string, (CePoint_t){0, next_line});
}

static void build_buffer_list(CeBuffer_t* buffer, CeBufferNode_t* head){
     int64_t index = 1;
     char line[256];
     ce_buffer_empty(buffer);
     while(head){
          snprintf(line, 256, "%ld %s %ld", index, head->buffer->name, head->buffer->line_count);
          buffer_append_on_new_line(buffer, line);
          head = head->next;
          index++;
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
          snprintf(line, 256, "// register '%c': line: %s\n%s\n", reg, yank->line ? "true" : "false", yank->text);
          buffer_append_on_new_line(buffer, line);
     }

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

static void build_complete_list(CeBuffer_t* buffer, CeComplete_t* complete){
     ce_buffer_empty(buffer);
     buffer->syntax_data = complete;
     char line[256];
     int64_t cursor = 0;
     for(int64_t i = 0; i < complete->count; i++){
          if(complete->elements[i].match){
               if(i == complete->current) cursor = buffer->line_count;
               snprintf(line, 256, "%s", complete->elements[i].string);
               buffer_append_on_new_line(buffer, line);
          }
     }

     // TODO: figure out why we have to account for this case
     if(buffer->line_count == 1 && cursor == 1) cursor = 0;

     buffer->cursor_save = (CePoint_t){0, cursor};
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

CeBuffer_t* new_buffer(){
     CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
     if(!buffer) return buffer;
     buffer->app_data = calloc(1, sizeof(CeAppBufferData_t));
     return buffer;
}

static bool string_ends_with(const char* str, const char* pattern){
     int64_t str_len = strlen(str);
     int64_t pattern_len = strlen(pattern);
     if(str_len < pattern_len) return false;

     return strncmp(str + (str_len - pattern_len), pattern, pattern_len) == 0;
}

void determine_buffer_syntax(CeBuffer_t* buffer){
     CeAppBufferData_t* buffer_data = buffer->app_data;

     if(string_ends_with(buffer->name, ".c") ||
        string_ends_with(buffer->name, ".h")){
          buffer_data->syntax_function = ce_syntax_highlight_c;
     }else if(string_ends_with(buffer->name, ".cpp") ||
        string_ends_with(buffer->name, ".hpp")){
          buffer_data->syntax_function = ce_syntax_highlight_cpp;
     }else if(string_ends_with(buffer->name, ".py")){
          buffer_data->syntax_function = ce_syntax_highlight_python;
     }else if(string_ends_with(buffer->name, ".java")){
          buffer_data->syntax_function = ce_syntax_highlight_java;
     }else if(string_ends_with(buffer->name, ".sh")){
          buffer_data->syntax_function = ce_syntax_highlight_bash;
     }else if(string_ends_with(buffer->name, ".cfg")){
          buffer_data->syntax_function = ce_syntax_highlight_config;
     }else if(string_ends_with(buffer->name, ".diff") ||
              string_ends_with(buffer->name, ".patch") ||
              string_ends_with(buffer->name, "COMMIT_EDITMSG")){
          buffer_data->syntax_function = ce_syntax_highlight_diff;
     }else{
          buffer_data->syntax_function = ce_syntax_highlight_plain;
     }
}

static CeBuffer_t* load_file_into_view(CeBufferNode_t** buffer_node_head, CeView_t* view,
                                       CeConfigOptions_t* config_options, CeVim_t* vim, const char* filepath){
     // adjust the filepath if it doesn't match our pwd
     char real_path[PATH_MAX + 1];
     char load_path[PATH_MAX + 1];
     char* res = realpath(filepath, real_path);
     if(!res) return NULL;
     char cwd[PATH_MAX + 1];
     if(getcwd(cwd, sizeof(cwd)) != NULL){
          size_t cwd_len = strlen(cwd);
          // append a '/' so it looks like a path
          if(cwd_len < PATH_MAX){
               cwd[cwd_len] = '/';
               cwd_len++;
               cwd[cwd_len] = 0;
          }
          // if the file is in our current directory, only show part of the path
          if(strncmp(cwd, real_path, cwd_len) == 0){
               strncpy(load_path, real_path + cwd_len, PATH_MAX);
          }else{
               strncpy(load_path, real_path, PATH_MAX);
          }
     }else{
          strncpy(load_path, real_path, PATH_MAX);
     }

     // have we already loaded this file?
     CeBufferNode_t* itr = *buffer_node_head;
     while(itr){
          if(strcmp(itr->buffer->name, load_path) == 0){
               ce_view_switch_buffer(view, itr->buffer, vim, config_options);
               return itr->buffer;
          }
          itr = itr->next;
     }


     // load file
     CeBuffer_t* buffer = new_buffer();
     if(ce_buffer_load_file(buffer, load_path)){
          ce_buffer_node_insert(buffer_node_head, buffer);
          ce_view_switch_buffer(view, buffer, vim, config_options);
          determine_buffer_syntax(buffer);
     }else{
          free(buffer);
          return NULL;
     }

     return buffer;
}

void complete_files(CeComplete_t* complete, const char* line, const char* base_directory){
     char full_path[PATH_MAX];
     if(base_directory && *line != '/'){
          snprintf(full_path, PATH_MAX, "%s/%s", base_directory, line);
     }else{
          strncpy(full_path, line, PATH_MAX);
     }

     // figure out the directory to complete
     const char* last_slash = strrchr(full_path, '/');
     char* directory = NULL;

     if(last_slash){
          directory = strndup(full_path, (last_slash - full_path) + 1);
     }else{
          directory = strdup(".");
     }

     // build list of files to complete
     struct dirent *node;
     DIR* os_dir = opendir(directory);
     if(!os_dir){
          free(directory);
          return;
     }

     int64_t file_count = 0;
     char** files = malloc(sizeof(*files));

     char tmp[PATH_MAX];
     struct stat info;
     while((node = readdir(os_dir)) != NULL){
          snprintf(tmp, PATH_MAX, "%s/%s", directory, node->d_name);
          stat(tmp, &info);
          file_count++;
          files = realloc(files, file_count * sizeof(*files));
          if(S_ISDIR(info.st_mode)){
               asprintf(&files[file_count - 1], "%s/", node->d_name);
          }else{
               files[file_count - 1] = strdup(node->d_name);
          }
     }

     closedir(os_dir);

     // check for exact match
     bool exact_match = true;
     if(complete->count != file_count){
          exact_match = false;
     }else{
          for(int64_t i = 0; i < file_count; i++){
               if(strcmp(files[i], complete->elements[i].string) != 0){
                    exact_match = false;
                    break;
               }
          }
     }

     if(!exact_match){
          ce_complete_init(complete, (const char**)(files), file_count);
     }

     for(int64_t i = 0; i < file_count; i++){
          free(files[i]);
     }
     free(files);

     if(last_slash){
          ce_complete_match(complete, last_slash + 1);
     }else{
          ce_complete_match(complete, line);
     }

     free(directory);
}

static char* view_base_directory(CeView_t* view, CeApp_t* app){
     if(view->buffer == app->terminal.buffer){
          return ce_terminal_get_current_directory(&app->terminal);
     }

     return directory_from_filename(view->buffer->name);
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

void draw_view_status(CeView_t* view, CeVim_t* vim, CeMacros_t* macros, CeColorDefs_t* color_defs, int64_t height_offset){
     // create bottom bar bg
     int64_t bottom = view->rect.bottom + height_offset;
     int color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
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
               vim_mode_string = "N";
               vim_mode_fg = COLOR_BLUE;
               break;
          case CE_VIM_MODE_INSERT:
               vim_mode_string = "I";
               vim_mode_fg = COLOR_GREEN;
               break;
          case CE_VIM_MODE_VISUAL:
               vim_mode_string = "V";
               vim_mode_fg = COLOR_YELLOW;
               break;
          case CE_VIM_MODE_VISUAL_LINE:
               vim_mode_string = "VL";
               vim_mode_fg = COLOR_BRIGHT_YELLOW;
               break;
          case CE_VIM_MODE_VISUAL_BLOCK:
               vim_mode_string = "VB";
               vim_mode_fg = COLOR_BRIGHT_YELLOW;
               break;
          case CE_VIM_MODE_REPLACE:
               vim_mode_string = "R";
               vim_mode_fg = COLOR_RED;
               break;
          }
     }

     if(vim_mode_string){
          color_pair = ce_color_def_get(color_defs, vim_mode_fg, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          mvprintw(bottom, view->rect.left + 1, "%s", vim_mode_string);

          color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          printw(" %s", view->buffer->name);
     }else{
          color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          mvprintw(bottom, view->rect.left + 1, "%s", view->buffer->name);
     }

     if(view->buffer->status == CE_BUFFER_STATUS_MODIFIED ||
        view->buffer->status == CE_BUFFER_STATUS_NEW_FILE){
          addch('*');
     }else if(view->buffer->status == CE_BUFFER_STATUS_READONLY){
          printw("[RO]");
     }

     if(vim_mode_string && ce_macros_is_recording(macros)){
          printw(" RECORDING %c", macros->recording);
     }

     char cursor_pos_string[32];
     int64_t cursor_pos_string_len = snprintf(cursor_pos_string, 32, "%ld, %ld", view->cursor.x + 1, view->cursor.y + 1);
     mvprintw(bottom, view->rect.right - (cursor_pos_string_len + 1), "%s", cursor_pos_string);
}

CeDestination_t scan_line_for_destination(const char* line){
     CeDestination_t destination = {};
     destination.point = (CePoint_t){-1, -1};

     // grep/gcc format
     char* file_end = strchr(line, ':');
     if(!file_end) return destination;
     char* row_end = strchr(file_end + 1, ':');
     char* col_end = NULL;
     if(row_end) col_end = strchr(row_end + 1, ':');
     // col_end and row_end is not always present

     int64_t filepath_len = file_end - line;
     if(filepath_len >= PATH_MAX) return destination;
     strncpy(destination.filepath, line, filepath_len);
     destination.filepath[filepath_len] = 0;
     char* end = NULL;

     if(row_end){
          destination.point.y = strtol(file_end + 1, &end, 10);

          if(destination.point.y > 0) destination.point.y--; // account for format which is 1 indexed

          if(col_end){
               destination.point.x = strtol(row_end + 1, &end, 10);
               if(destination.point.x > 0) destination.point.x--; // account for format which is 1 indexed
          }else{
               destination.point.x = 0;
          }
     }else{
          destination.point = (CePoint_t){0, 0};
     }

     return destination;
}

void draw_layout(CeLayout_t* layout, CeVim_t* vim, CeMacros_t* macros, CeTerminal_t* terminal,
                 CeColorDefs_t* color_defs, int64_t tab_width, CeLineNumber_t line_number, CeLayout_t* current,
                 CeSyntaxDef_t* syntax_defs, int64_t terminal_width){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
     {
          CeDrawColorList_t draw_color_list = {};
          CeAppBufferData_t* buffer_data = layout->view.buffer->app_data;

          // update which terminal buffer we are viewing
          if(layout->view.buffer == terminal->lines_buffer || layout->view.buffer == terminal->alternate_lines_buffer){
               layout->view.buffer = terminal->buffer;
          }

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

               buffer_data->syntax_function(&layout->view, &range_list, &draw_color_list, syntax_defs,
                                            layout->view.buffer->syntax_data);
               ce_range_list_free(&range_list);
          }

          draw_view(&layout->view, tab_width, line_number, &draw_color_list, color_defs, syntax_defs);
          ce_draw_color_list_free(&draw_color_list);
          draw_view_status(&layout->view, layout == current ? vim : NULL, macros, color_defs, 0);
          int64_t rect_height = layout->view.rect.bottom - layout->view.rect.top;
          int color_pair = ce_color_def_get(color_defs, COLOR_BRIGHT_BLACK, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          if(layout->view.rect.right < (terminal_width - 1)){
               for(int i = 0; i < rect_height; i++){
                    mvaddch(layout->view.rect.top + i, layout->view.rect.right, ' ');
               }
          }
     } break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               draw_layout(layout->list.layouts[i], vim, macros, terminal, color_defs, tab_width, line_number, current,
                           syntax_defs, terminal_width);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          draw_layout(layout->tab.root, vim, macros, terminal, color_defs, tab_width, line_number, current, syntax_defs,
                      terminal_width);
          break;
     }
}

static CePoint_t view_cursor_on_screen(CeView_t* view, int64_t tab_width, CeLineNumber_t line_number){
     // move the visual cursor to the right location
     int64_t visible_cursor_x = 0;
     if(ce_buffer_point_is_valid(view->buffer, view->cursor)){
          visible_cursor_x = ce_util_string_index_to_visible_index(view->buffer->lines[view->cursor.y],
                                                                   view->cursor.x, tab_width);
     }

     int64_t line_number_width = 0;
     if(!view->buffer->no_line_numbers){
          line_number_width = ce_line_number_column_width(line_number, view->buffer->line_count, view->rect.top, view->rect.bottom);
     }

     return (CePoint_t){visible_cursor_x - view->scroll.x + view->rect.left + line_number_width,
                        view->cursor.y - view->scroll.y + view->rect.top};
}

void* draw_thread(void* thread_data){
     CeApp_t* app = (CeApp_t*)(thread_data);
     struct timeval previous_draw_time;
     struct timeval current_draw_time;
     uint64_t time_since_last_draw = 0;
     CeColorDefs_t color_defs = {};

     gettimeofday(&previous_draw_time, NULL);

     while(!app->quit){
          while(true){
               gettimeofday(&current_draw_time, NULL);
               time_since_last_draw = (current_draw_time.tv_sec - previous_draw_time.tv_sec) * 1000000LL +
                                      (current_draw_time.tv_usec - previous_draw_time.tv_usec);
               if(time_since_last_draw >= DRAW_USEC_LIMIT){
                    if(app->ready_to_draw){
                         app->ready_to_draw = false;
                         break;
                    }else if(app->terminal.ready_to_draw){
                         app->terminal.ready_to_draw = false;
                         break;
                    }
               }
               sleep(0);
          }

          previous_draw_time = current_draw_time;

          CeLayout_t* tab_list_layout = app->tab_list_layout;
          CeLayout_t* tab_layout = tab_list_layout->tab_list.current;

          // draw a tab bar if there is more than 1 tab
          if(tab_list_layout->tab_list.tab_count > 1){
               move(0, 0);
               int color_pair = ce_color_def_get(&color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
               attron(COLOR_PAIR(color_pair));
               for(int64_t i = tab_list_layout->tab_list.rect.left; i <= tab_list_layout->tab_list.rect.right; i++){
                    addch(' ');
               }

               move(0, 0);

               for(int64_t i = 0; i < tab_list_layout->tab_list.tab_count; i++){
                    if(tab_list_layout->tab_list.tabs[i] == tab_list_layout->tab_list.current){
                         color_pair = ce_color_def_get(&color_defs, COLOR_BRIGHT_WHITE, COLOR_DEFAULT);
                         attron(COLOR_PAIR(color_pair));
                    }else{
                         color_pair = ce_color_def_get(&color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
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
          if((view->buffer == app->terminal.lines_buffer ||
              view->buffer == app->terminal.alternate_lines_buffer) &&
             app->vim.mode == CE_VIM_MODE_INSERT){
               view->cursor.x = app->terminal.cursor.x;
               view->cursor.y = app->terminal.cursor.y;
               ce_view_follow_cursor(view, 1, 1, app->config_options.tab_width);
          }

          standend();
          draw_layout(tab_layout, &app->vim, &app->macros, &app->terminal, &color_defs, app->config_options.tab_width,
                      app->config_options.line_number, tab_layout->tab.current, app->syntax_defs, tab_list_layout->tab_list.rect.right);

          if(app->input_mode){
               CeDrawColorList_t draw_color_list = {};
               draw_view(&app->input_view, app->config_options.tab_width, app->config_options.line_number, &draw_color_list,
                         &color_defs, app->syntax_defs);
               int64_t new_status_bar_offset = (app->input_view.rect.bottom - app->input_view.rect.top) + 1;
               draw_view_status(&app->input_view, &app->vim, &app->macros, &color_defs, 0);
               draw_view_status(&tab_layout->tab.current->view, NULL, &app->macros, &color_defs, -new_status_bar_offset);
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
                    int64_t new_status_bar_offset = (app->complete_view.rect.bottom - app->complete_view.rect.top) + 2;
                    draw_view_status(&tab_layout->tab.current->view, NULL, &app->macros, &color_defs, -new_status_bar_offset);
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

     return NULL;
}

void input_view_overlay(CeView_t* input_view, CeView_t* view){
     input_view->rect.left = view->rect.left;
     input_view->rect.right = view->rect.right;
     input_view->rect.bottom = view->rect.bottom;
     int64_t max_height = (view->rect.bottom - view->rect.top) - 1;
     int64_t height = input_view->buffer->line_count;
     if(height <= 0) height = 1;
     if(height > max_height) height = max_height;
     input_view->rect.top = view->rect.bottom - height;
}

bool enable_input_mode(CeView_t* input_view, CeView_t* view, CeVim_t* vim, const char* dialogue){
     // update input view to overlay the current view
     input_view_overlay(input_view, view);

     // update name based on dialog
     free(input_view->buffer->app_data);
     bool success = ce_buffer_alloc(input_view->buffer, 1, dialogue);
     input_view->buffer->app_data = calloc(1, sizeof(CeAppBufferData_t));
     input_view->cursor = (CePoint_t){0, 0};
     vim->mode = CE_VIM_MODE_INSERT;
     ce_rune_node_free(&vim->insert_rune_head);

     return success;
}

void buffer_replace_all(CeBuffer_t* buffer, CePoint_t cursor, const char* match, const char* replacement, CePoint_t start, CePoint_t end,
                        bool regex_search){
     bool chain_undo = false;
     int64_t match_len = 0;
     if(!regex_search) match_len = strlen(match);
     regex_t regex = {};
     int rc = regcomp(&regex, match, REG_EXTENDED);
     if(rc != 0){
          char error_buffer[BUFSIZ];
          regerror(rc, &regex, error_buffer, BUFSIZ);
          ce_log("regcomp() failed: '%s'", error_buffer);
          return;
     }
     while(true){
          CePoint_t match_point;

          // find the match
          if(regex_search){
               CeRegexSearchResult_t result = ce_buffer_regex_search_forward(buffer, start, &regex);
               match_point = result.point;
               match_len = result.length;
          }else{
               match_point = ce_buffer_search_forward(buffer, start, match);
          }

          if(match_point.x < 0) break;
          if(ce_point_after(match_point, end)) break;

          ce_buffer_remove_string_change(buffer, match_point, match_len, &cursor, cursor, chain_undo);
          chain_undo = true;

          ce_buffer_insert_string_change(buffer, strdup(replacement), match_point, &cursor, cursor, chain_undo);
     }
}

void replace_all(CeView_t* view, CeVim_t* vim, const char* match, const char* replace){
     CePoint_t start;
     CePoint_t end;
     if(vim->mode == CE_VIM_MODE_VISUAL){
          if(ce_point_after(view->cursor, vim->visual)){
               start = vim->visual;
               end = view->cursor;
          }else{
               start = view->cursor;
               end = vim->visual;
          }
     }else if(vim->mode == CE_VIM_MODE_VISUAL_LINE){
          if(ce_point_after(view->cursor, vim->visual)){
               start = (CePoint_t){0, vim->visual.y};
               end = (CePoint_t){ce_utf8_last_index(view->buffer->lines[view->cursor.y]), view->cursor.y};
          }else{
               start = (CePoint_t){0, view->cursor.y};
               end = (CePoint_t){ce_utf8_last_index(view->buffer->lines[vim->visual.y]), vim->visual.y};
          }
     }else{
          start = view->cursor;
          end = ce_buffer_end_point(view->buffer);
     }

     if(ce_point_after(end, start)){
          buffer_replace_all(view->buffer, view->cursor, match, replace, start, end, false);
     }
}

bool get_layout_and_view(CeApp_t* app, CeView_t** view, CeLayout_t** tab_layout){
     *tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return false;

     if((*tab_layout)->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          *view = &(*tab_layout)->tab.current->view;
          return true;
     }

     return true;
}

CeCommandStatus_t command_quit(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     bool unsaved_buffers = false;
     CeBufferNode_t* itr = app->buffer_node_head;
     while(itr){
          if(itr->buffer->status == CE_BUFFER_STATUS_MODIFIED && itr->buffer != app->input_view.buffer){
               unsaved_buffers = true;
               break;
          }
          itr = itr->next;
     }

     if(unsaved_buffers){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, UNSAVED_BUFFERS_DIALOGUE);
     }else{
          app->quit = true;
     }

     return CE_COMMAND_SUCCESS;
}

// TODO: is this useful or did I pre-maturely create this
static bool get_view_info_from_tab(CeLayout_t* tab_layout, CeView_t** view, CeRect_t* view_rect){
     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          *view = &tab_layout->tab.current->view;
     }else if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_LIST){
          *view_rect = tab_layout->list.rect;
     }else if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_TAB){
          *view_rect = tab_layout->tab.rect;
     }else{
          return false;
     }

     return true;
}

CeCommandStatus_t command_select_adjacent_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CePoint_t target;
     CeView_t* view = NULL;
     CeRect_t view_rect = {};
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(!get_view_info_from_tab(tab_layout, &view, &view_rect)){
          assert(!"unknown layout type");
          return CE_COMMAND_FAILURE;
     }

     if(strcmp(command->args[0].string, "up") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width,
                                                               app->config_options.line_number);
               target = (CePoint_t){screen_cursor.x, view->rect.top - 1};
          }else{
               target = (CePoint_t){view_rect.left, view_rect.top - 1};
          }
     }else if(strcmp(command->args[0].string, "down") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width,
                                                               app->config_options.line_number);
               target = (CePoint_t){screen_cursor.x, view->rect.bottom + 1};
          }else{
               target = (CePoint_t){view_rect.left, view_rect.bottom + 1};
          }
     }else if(strcmp(command->args[0].string, "left") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width,
                                                               app->config_options.line_number);
               target = (CePoint_t){view->rect.left - 1, screen_cursor.y};
          }else{
               target = (CePoint_t){view_rect.left - 1, view_rect.top};
          }
     }else if(strcmp(command->args[0].string, "right") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width,
                                                               app->config_options.line_number);
               target = (CePoint_t){view->rect.right + 1, screen_cursor.y};
          }else{
               target = (CePoint_t){view_rect.right + 1, view_rect.top};
          }
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0].string);
          return CE_COMMAND_PRINT_HELP;
     }

     // wrap around
     if(target.x >= app->terminal_width) target.x %= app->terminal_width;
     if(target.x < 0) target.x = app->terminal_width + target.x;
     if(target.y >= app->terminal_height) target.y %= app->terminal_height;
     if(target.y < 0) target.y = app->terminal_height + target.y;

     CeLayout_t* layout = ce_layout_find_at(tab_layout, target);
     if(layout){
          tab_layout->tab.current = layout;
          app->vim.mode = CE_VIM_MODE_NORMAL;
          app->input_mode = false;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_save_buffer(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;
     ce_buffer_save(view->buffer);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_show_buffers(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;
     ce_view_switch_buffer(view, app->buffer_list_buffer, &app->vim, &app->config_options);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_show_yanks(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;
     ce_view_switch_buffer(view, app->yank_list_buffer, &app->vim, &app->config_options);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_split_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     bool vertical = false;

     if(strcmp(command->args[0].string, "vertical") == 0){
          vertical = true;
     }else if(strcmp(command->args[0].string, "horizontal") == 0){
          // pass
     }else{
          ce_log("unrecognized argument '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     ce_layout_split(tab_layout, vertical);
     ce_layout_distribute_rect(tab_layout, app->terminal_rect);
     ce_layout_view_follow_cursor(tab_layout, app->config_options.horizontal_scroll_off,
                                  app->config_options.vertical_scroll_off, app->config_options.tab_width,
                                  app->terminal.buffer);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_select_parent_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     CeLayout_t* layout = ce_layout_find_parent(tab_layout, tab_layout->tab.current);
     if(layout) tab_layout->tab.current = layout;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_delete_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeRect_t view_rect = {};
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     // check if this is the only view, and ignore the delete request
     if(app->tab_list_layout->tab_list.tab_count == 1 &&
        tab_layout->tab.root->type == CE_LAYOUT_TYPE_LIST &&
        tab_layout->tab.root->list.layout_count == 1 &&
        tab_layout->tab.current == tab_layout->tab.root->list.layouts[0]){
          return CE_COMMAND_NO_ACTION;
     }

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(!get_view_info_from_tab(tab_layout, &view, &view_rect)){
          assert(!"unknown layout type");
          return CE_COMMAND_FAILURE;
     }

     CePoint_t cursor = {0, 0};
     if(view) cursor = view_cursor_on_screen(view, app->config_options.tab_width, app->config_options.line_number);
     ce_layout_delete(tab_layout, tab_layout->tab.current);
     ce_layout_distribute_rect(tab_layout, app->terminal_rect);
     ce_layout_view_follow_cursor(tab_layout, app->config_options.horizontal_scroll_off,
                                  app->config_options.vertical_scroll_off, app->config_options.tab_width,
                                  app->terminal.buffer);
     CeLayout_t* layout = ce_layout_find_at(tab_layout, cursor);
     if(layout) tab_layout->tab.current = layout;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_load_file(CeCommand_t* command, void* user_data){
     if(command->arg_count < 0 || command->arg_count > 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     if(command->arg_count == 1){
          if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
          load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, command->args[0].string);
     }else{ // it's 0
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "LOAD FILE");

          char* base_directory = view_base_directory(view, app);
          complete_files(&app->load_file_complete, app->input_view.buffer->lines[0], base_directory);
          free(base_directory);
          build_complete_list(app->complete_list_buffer, &app->load_file_complete);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_new_tab(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     CeLayout_t* new_tab_layout = ce_layout_tab_list_add(app->tab_list_layout);
     if(!new_tab_layout) return CE_COMMAND_NO_ACTION;
     app->tab_list_layout->tab_list.current = new_tab_layout;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_select_adjacent_tab(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     if(strcmp(command->args[0].string, "left") == 0){
          for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
               if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
                    if(i > 0){
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i - 1];
                         return CE_COMMAND_SUCCESS;
                    }else{
                         // wrap around
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[app->tab_list_layout->tab_list.tab_count - 1];
                         return CE_COMMAND_SUCCESS;
                    }
                    break;
               }
          }
     }else if(strcmp(command->args[0].string, "right") == 0){
          for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
               if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
                    if(i < (app->tab_list_layout->tab_list.tab_count - 1)){
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i + 1];
                         return CE_COMMAND_SUCCESS;
                    }else{
                         // wrap around
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[0];
                         return CE_COMMAND_SUCCESS;
                    }
                    break;
               }
          }
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_NO_ACTION;
}

CeCommandStatus_t command_search(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     if(strcmp(command->args[0].string, "forward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_FORWARD;
          app->search_start = view->cursor;
     }else if(strcmp(command->args[0].string, "backward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REVERSE SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_BACKWARD;
          app->search_start = view->cursor;
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_regex_search(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     if(strcmp(command->args[0].string, "forward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REGEX SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_REGEX_FORWARD;
          app->search_start = view->cursor;
     }else if(strcmp(command->args[0].string, "backward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REGEX REVERSE SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_REGEX_BACKWARD;
          app->search_start = view->cursor;
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

// lol
CeCommandStatus_t command_command(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "COMMAND");
     ce_complete_reset(&app->command_complete);
     build_complete_list(app->complete_list_buffer, &app->command_complete);
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_switch_to_terminal(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     ce_switch_to_terminal(app, view, tab_layout);
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_switch_buffer(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "SWITCH BUFFER");

     int64_t buffer_count = 0;
     CeBufferNode_t* itr = app->buffer_node_head;
     while(itr){
          buffer_count++;
          itr = itr->next;
     }

     char** filenames = malloc(buffer_count * sizeof(*filenames));

     int64_t index = 0;
     itr = app->buffer_node_head;
     while(itr){
          filenames[index] = strdup(itr->buffer->name);
          index++;
          itr = itr->next;
     }

     ce_complete_init(&app->switch_buffer_complete, (const char**)filenames, buffer_count);
     build_complete_list(app->complete_list_buffer, &app->switch_buffer_complete);

     for(int64_t i = 0; i < buffer_count; i++){
          free(filenames[i]);
     }
     free(filenames);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_redraw(CeCommand_t* command, void* user_data){
     clear();
     return CE_COMMAND_SUCCESS;
}

CeBuffer_t* load_destination_into_view(CeBufferNode_t** buffer_node_head, CeView_t* view, CeConfigOptions_t* config_options,
                                CeVim_t* vim, CeDestination_t* destination){
     CeBuffer_t* load_buffer = load_file_into_view(buffer_node_head, view, config_options, vim, destination->filepath);
     if(!load_buffer) return load_buffer;

     if(destination->point.y < load_buffer->line_count){
          view->cursor.y = destination->point.y;
          int64_t line_len = ce_utf8_strlen(load_buffer->lines[view->cursor.y]);
          if(destination->point.x < line_len) view->cursor.x = destination->point.x;
     }

     return load_buffer;
}

CeCommandStatus_t command_goto_destination_in_line(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     if(view->buffer->line_count == 0) return CE_COMMAND_NO_ACTION;

     CeDestination_t destination = scan_line_for_destination(view->buffer->lines[view->cursor.y]);
     if(destination.point.x < 0) return CE_COMMAND_NO_ACTION;

     CeAppBufferData_t* buffer_data = view->buffer->app_data;
     buffer_data->last_goto_destination = view->cursor.y;

     CeBuffer_t* buffer = load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                                     &destination);
     if(!buffer) return CE_COMMAND_NO_ACTION;


     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_goto_next_destination(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     CeBuffer_t* buffer = app->last_goto_buffer;
     if(!buffer) buffer = app->terminal.buffer;
     if(buffer->line_count == 0) return CE_COMMAND_SUCCESS;

     CeAppBufferData_t* buffer_data = buffer->app_data;

     int64_t save_destination = buffer_data->last_goto_destination;
     for(int64_t i = buffer_data->last_goto_destination + 1; i != buffer_data->last_goto_destination; i++){
          if(i >= buffer->line_count){
               i = 0;
               if(i == buffer_data->last_goto_destination) break;
          }

          CeDestination_t destination = scan_line_for_destination(buffer->lines[i]);
          if(destination.point.x < 0) continue;
          if(access(destination.filepath, F_OK) == -1) continue;

          CeBuffer_t* loaded_buffer = load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                                                 &destination);
          if(loaded_buffer){
               CeLayout_t* layout = ce_layout_buffer_in_view(tab_layout, buffer);
               if(layout) layout->view.scroll.y = i;
               buffer_data->last_goto_destination = i;
          }
          break;
     }

     // we didn't find anything, and since the user asked for a destination, find this one
     if(buffer_data->last_goto_destination == save_destination && save_destination < buffer->line_count){
          CeDestination_t destination = scan_line_for_destination(buffer->lines[save_destination]);
          if(destination.point.x >= 0){
               CeLayout_t* layout = ce_layout_buffer_in_view(tab_layout, buffer);
               if(layout) layout->view.scroll.y = save_destination;
               load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, &destination);
          }
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_goto_prev_destination(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     CeBuffer_t* buffer = app->last_goto_buffer;
     if(!buffer) buffer = app->terminal.buffer;
     if(buffer->line_count == 0) return CE_COMMAND_SUCCESS;

     CeAppBufferData_t* buffer_data = buffer->app_data;

     int64_t save_destination = buffer_data->last_goto_destination;
     for(int64_t i = buffer_data->last_goto_destination - 1; i != buffer_data->last_goto_destination; i--){
          if(i < 0){
               i = buffer->line_count - 1;
               if(i == buffer_data->last_goto_destination) break;
          }

          CeDestination_t destination = scan_line_for_destination(buffer->lines[i]);
          if(destination.point.x < 0) continue;
          if(access(destination.filepath, F_OK) == -1) continue;

          CeBuffer_t* loaded_buffer = load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                                          &destination);
          if(loaded_buffer){
               CeLayout_t* layout = ce_layout_buffer_in_view(tab_layout, buffer);
               if(layout) layout->view.scroll.y = i;
               buffer_data->last_goto_destination = i;
          }
          break;
     }

     // we didn't find anything, and since the user asked for a destination, find this one
     if(buffer_data->last_goto_destination == save_destination && save_destination < buffer->line_count){
          CeDestination_t destination = scan_line_for_destination(buffer->lines[save_destination]);
          if(destination.point.x >= 0){
               load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, &destination);
          }
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_replace_all(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     if(command->arg_count == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REPLACE ALL");
     }else if(command->arg_count == 1 && command->args[0].type == CE_COMMAND_ARG_STRING){
          int64_t index = ce_vim_yank_register_index('/');
          CeVimYank_t* yank = app->vim.yanks + index;
          if(yank->text){
               replace_all(view, &app->vim, yank->text, command->args[0].string);
          }
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_reload_file(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     if(access(view->buffer->name, F_OK) == -1){
          ce_log("'%s' is not file backed, unable to reload\n", view->buffer->name);
          return CE_COMMAND_NO_ACTION;
     }

     char* filename = strdup(view->buffer->name);
     CeAppBufferData_t* buffer_data = view->buffer->app_data;
     ce_buffer_free(view->buffer);
     view->buffer->app_data = buffer_data; // NOTE: not great that I need to save user data and reset it
     ce_buffer_load_file(view->buffer, filename);
     free(filename);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_reload_config(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     char* config_path = strdup(app->user_config.filepath);
     user_config_free(&app->user_config);
     user_config_init(&app->user_config, config_path);
     free(config_path);
     app->user_config.init_func(app);
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_buffer_type(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     CeAppBufferData_t* buffer_data = view->buffer->app_data;

     if(strcmp(command->args[0].string, "c") == 0){
          buffer_data->syntax_function = ce_syntax_highlight_c;
     }else if(strcmp(command->args[0].string, "python") == 0){
          buffer_data->syntax_function = ce_syntax_highlight_python;
     }else if(strcmp(command->args[0].string, "java") == 0){
          buffer_data->syntax_function = ce_syntax_highlight_java;
     }else if(strcmp(command->args[0].string, "bash") == 0){
          buffer_data->syntax_function = ce_syntax_highlight_bash;
     }else if(strcmp(command->args[0].string, "config") == 0){
          buffer_data->syntax_function = ce_syntax_highlight_config;
     }else if(strcmp(command->args[0].string, "diff") == 0){
          buffer_data->syntax_function = ce_syntax_highlight_diff;
     }else if(strcmp(command->args[0].string, "plain") == 0){
          buffer_data->syntax_function = ce_syntax_highlight_plain;
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_new_buffer(CeCommand_t* command, void* user_data){
     if(command->arg_count > 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     const char* buffer_name = "unnamed";
     if(command->arg_count == 1 && command->args[0].type == CE_COMMAND_ARG_STRING) buffer_name = command->args[0].string;

     CeBuffer_t* buffer = new_buffer();
     ce_buffer_alloc(buffer, 1, buffer_name);
     view->buffer = buffer;
     view->cursor = (CePoint_t){0, 0};
     ce_buffer_node_insert(&app->buffer_node_head, buffer);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_rename_buffer(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     free(view->buffer->name);
     view->buffer->name = strdup(command->args[0].string);
     if(view->buffer->status == CE_BUFFER_STATUS_NONE) view->buffer->status = CE_BUFFER_STATUS_MODIFIED;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_jump_list(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     CeDestination_t* destination = NULL;
     int64_t view_width = view->rect.right - view->rect.left;
     int64_t view_height = view->rect.bottom - view->rect.top;
     CeRect_t view_rect = {view->scroll.x, view->scroll.x + view_width, view->scroll.y, view->scroll.y + view_height};

     if(strcmp(command->args[0].string, "next")){
          // ignore destinations on screen
          while((destination = ce_jump_list_next(&app->jump_list))){
               if(strcmp(destination->filepath, view->buffer->name) != 0 || !ce_point_in_rect(destination->point, view_rect)){
                    break;
               }
          }
     }else if(strcmp(command->args[0].string, "previous")){
          // ignore destinations on screen
          while((destination = ce_jump_list_previous(&app->jump_list))){
               if(strcmp(destination->filepath, view->buffer->name) != 0 || !ce_point_in_rect(destination->point, view_rect)){
                    break;
               }
          }
     }

     if(destination){
          if(load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, destination->filepath)){
               view->cursor = destination->point;
          }
     }
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_line_number(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     if(strcmp(command->args[0].string, "none") == 0){
          app->config_options.line_number = CE_LINE_NUMBER_NONE;
     }else if(strcmp(command->args[0].string, "absolute") == 0){
          app->config_options.line_number = CE_LINE_NUMBER_ABSOLUTE;
     }else if(strcmp(command->args[0].string, "relative") == 0){
          app->config_options.line_number = CE_LINE_NUMBER_RELATIVE;
     }else if(strcmp(command->args[0].string, "both") == 0){
          app->config_options.line_number = CE_LINE_NUMBER_ABSOLUTE_AND_RELATIVE;
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_terminal_command(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = (CeApp_t*)(user_data);
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     ce_run_command_in_terminal(&app->terminal, command->args[0].string);
     CeLayout_t* terminal_layout = ce_layout_buffer_in_view(tab_layout, app->terminal.buffer);
     if(terminal_layout){
          terminal_layout->view.cursor.x = 0;
          terminal_layout->view.cursor.y = app->terminal.cursor.y;
          terminal_layout->view.scroll.y = app->terminal.cursor.y;
          terminal_layout->view.scroll.x = 0;
     }
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_terminal_command_in_view(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = (CeApp_t*)(user_data);
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     ce_run_command_in_terminal(&app->terminal, command->args[0].string);
     view = ce_switch_to_terminal(app, view, tab_layout);
     view->cursor.x = 0;
     view->cursor.y = app->terminal.cursor.y;
     view->scroll.y = app->terminal.cursor.y;
     view->scroll.x = 0;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_man_page_on_word_under_cursor(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = (CeApp_t*)(user_data);
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     CeRange_t range = ce_vim_find_little_word_boundaries(view->buffer, view->cursor); // returns -1
     char* word = ce_buffer_dupe_string(view->buffer, range.start, (range.end.x - range.start.x) + 1);
     if(!word) return CE_COMMAND_NO_ACTION;
     char cmd[128];
     snprintf(cmd, 128, "man %s", word);
     free(word);
     ce_run_command_in_terminal(&app->terminal, cmd);
     ce_switch_to_terminal(app, view, tab_layout);

     return CE_COMMAND_SUCCESS;
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
                    char* base_directory = view_base_directory(view, app);
                    complete_files(&app->load_file_complete, app->input_view.buffer->lines[0], base_directory);
                    free(base_directory);
                    build_complete_list(app->complete_list_buffer, &app->load_file_complete);
               }
          }

          return true;
     }

     return false;
}

void app_handle_key(CeApp_t* app, CeView_t* view, int key){
     if(app->key_count == 0 &&
        app->last_vim_handle_result != CE_VIM_PARSE_IN_PROGRESS &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONTINUE &&
        app->vim.mode != CE_VIM_MODE_INSERT){
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
               return;
          }

          if(app->record_macro && !ce_macros_is_recording(&app->macros)){
               ce_macros_begin_recording(&app->macros, key);
               return;
          }

          if(app->replay_macro){
               app->replay_macro = false;
               CeRune_t* rune_string = ce_macros_get_register_string(&app->macros, key);
               if(rune_string){
                    CeRune_t* itr = rune_string;
                    while(*itr){
                         app_handle_key(app, view, *itr);
                         itr++;
                    }
               }

               free(rune_string);
               return;
          }
     }

     if(ce_macros_is_recording(&app->macros)){
          ce_macros_record_key(&app->macros, key);
     }

     if(view && (view->buffer == app->terminal.lines_buffer || view->buffer == app->terminal.alternate_lines_buffer) &&
        app->vim.mode == CE_VIM_MODE_INSERT && !app->input_mode){
          if(key == KEY_ESCAPE){
               app->vim.mode = CE_VIM_MODE_NORMAL;
          }else if(key == 1){ // ctrl + a
               // TODO: make this configurable
               // send escape
               ce_terminal_send_key(&app->terminal, KEY_ESCAPE);
          }else{
               if(key == KEY_ENTER){
                    CeAppBufferData_t* buffer_data = app->terminal.buffer->app_data;
                    buffer_data->last_goto_destination = app->terminal.cursor.y;
               }
               ce_terminal_send_key(&app->terminal, key);
          }
          return;
     }

     // as long as vim isn't in the middle of handling keys, in insert mode vim returns VKH_HANDLED_KEY TODO: is that what we want?
     if(app->last_vim_handle_result != CE_VIM_PARSE_IN_PROGRESS &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONTINUE &&
        app->vim.mode != CE_VIM_MODE_INSERT){
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
                              ce_view_switch_buffer(view, itr->buffer, &app->vim, &app->config_options);
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
                              CeDestination_t destination = {};
                              destination.point = view->cursor;
                              strncpy(destination.filepath, view->buffer->name, PATH_MAX);
                              ce_jump_list_insert(&app->jump_list, destination);

                              char* base_directory = view_base_directory(view, app);
                              char filepath[PATH_MAX];
                              for(int64_t i = 0; i < app->input_view.buffer->line_count; i++){
                                   if(base_directory && app->input_view.buffer->lines[i][0] != '/'){
                                        snprintf(filepath, PATH_MAX, "%s/%s", base_directory, app->input_view.buffer->lines[i]);
                                   }else{
                                        strncpy(filepath, app->input_view.buffer->lines[i], PATH_MAX);
                                   }
                                   load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, filepath);
                              }

                              free(base_directory);
                         }else if(strcmp(app->input_view.buffer->name, "SEARCH") == 0 ||
                                  strcmp(app->input_view.buffer->name, "REVERSE SEARCH") == 0 ||
                                  strcmp(app->input_view.buffer->name, "REGEX SEARCH") == 0 ||
                                  strcmp(app->input_view.buffer->name, "REGEX REVERSE SEARCH") == 0){
                              // update yanks
                              int64_t index = ce_vim_yank_register_index('/');
                              CeVimYank_t* yank = app->vim.yanks + index;
                              free(yank->text);
                              yank->text = strdup(app->input_view.buffer->lines[0]);
                              yank->line = false;

                              // insert jump
                              CeDestination_t destination = {};
                              destination.point = view->cursor;
                              strncpy(destination.filepath, view->buffer->name, PATH_MAX);
                              ce_jump_list_insert(&app->jump_list, destination);
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
                              free(yank->text);
                              yank->text = ce_buffer_dupe(app->input_view.buffer);
                              yank->line = false;
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
                              CeBufferNode_t* itr = app->buffer_node_head;
                              while(itr){
                                   if(strcmp(itr->buffer->name, app->input_view.buffer->lines[0]) == 0){
                                        ce_view_switch_buffer(view, itr->buffer, &app->vim, &app->config_options);
                                        if(itr->buffer == app->terminal.buffer){
                                             int64_t width = view->rect.right - view->rect.left;
                                             int64_t height = view->rect.bottom - view->rect.top;
                                             ce_terminal_resize(&app->terminal, width, height);
                                        }
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

               if(buffer_index == view->cursor.y && itr->buffer != app->buffer_list_buffer){
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
                    if(key == KEY_UP){
                         char* prev = ce_history_previous(&app->command_history);
                         if(prev){
                              ce_buffer_remove_string(app->input_view.buffer, (CePoint_t){0, 0}, ce_utf8_strlen(app->input_view.buffer->lines[0]));
                              ce_buffer_insert_string(app->input_view.buffer, prev, (CePoint_t){0, 0});
                         }
                         return;
                    }

                    if(key == KEY_DOWN){
                         char* next = ce_history_next(&app->command_history);
                         if(next){
                              ce_buffer_remove_string(app->input_view.buffer, (CePoint_t){0, 0}, ce_utf8_strlen(app->input_view.buffer->lines[0]));
                              ce_buffer_insert_string(app->input_view.buffer, next, (CePoint_t){0, 0});
                         }
                         return;
                    }
               }

               CeAppBufferData_t* buffer_data = app->input_view.buffer->app_data;

               app->last_vim_handle_result = ce_vim_handle_key(&app->vim, &app->input_view, key, &buffer_data->vim, &app->config_options);

               if(app->vim.mode == CE_VIM_MODE_INSERT && app->input_view.buffer->line_count){
                    if(strcmp(app->input_view.buffer->name, "COMMAND") == 0){
                         ce_complete_match(&app->command_complete, app->input_view.buffer->lines[0]);
                         build_complete_list(app->complete_list_buffer, &app->command_complete);
                    }else if(strcmp(app->input_view.buffer->name, "LOAD FILE") == 0){
                         char* base_directory = view_base_directory(view, app);
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
                         CeDestination_t destination = {};
                         destination.point = view->cursor;
                         strncpy(destination.filepath, view->buffer->name, PATH_MAX);
                         ce_jump_list_insert(&app->jump_list, destination);
                    }
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
     printf("usage  : %s [options]\n", program);
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

     // validate args
     {
          if(!config_filepath){
               printf("error: please specify a config file\n\n");
               print_help(argv[0]);
               return 1;
          }
     }

     setlocale(LC_ALL, "");

     char log_filepath[PATH_MAX];
     snprintf(log_filepath, PATH_MAX, "%s/ce.log", getenv("HOME"));
     if(!ce_log_init(log_filepath)){
          return 1;
     }

     // init ncurses
     {
          initscr();
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

     // TODO: allocate this on the heap when/if it gets too big?
     CeApp_t app = {};

     // init commands
     CeCommandEntry_t command_entries[] = {
          {command_quit, "quit", "quit ce"},
          {command_select_adjacent_layout, "select_adjacent_layout", "select 'left', 'right', 'up' or 'down adjacent layouts"},
          {command_save_buffer, "save_buffer", "save the currently selected view's buffer"},
          {command_show_buffers, "show_buffers", "show the list of buffers"},
          {command_show_yanks, "show_yanks", "show the state of your vim yanks"},
          {command_split_layout, "split_layout", "split the current layout 'horizontal' or 'vertical' into 2 layouts"},
          {command_select_parent_layout, "select_parent_layout", "select the parent of the current layout"},
          {command_delete_layout, "delete_layout", "delete the current layout (unless it's the only one left)"},
          {command_load_file, "load_file", "load a file (optionally specified)"},
          {command_new_tab, "new_tab", "create a new tab"},
          {command_select_adjacent_tab, "select_adjacent_tab", "selects either the 'left' or 'right' tab"},
          {command_search, "search", "interactive search 'forward' or 'backward'"},
          {command_regex_search, "regex_search", "interactive regex search 'forward' or 'backward'"},
          {command_command, "command", "interactively send a commmand"},
          {command_redraw, "redraw", "redraw the entire editor"},
          {command_switch_to_terminal, "switch_to_terminal", "if the terminal is in view, goto it, otherwise, open the terminal in the current view"},
          {command_switch_buffer, "switch_buffer", "open dialogue to switch buffer by name"},
          {command_goto_destination_in_line, "goto_destination_in_line", "scan current line for destination formats"},
          {command_goto_next_destination, "goto_next_destination", "find the next line in the buffer that contains a destination to goto"},
          {command_goto_prev_destination, "goto_prev_destination", "find the previous line in the buffer that contains a destination to goto"},
          {command_replace_all, "replace_all", "replace all occurances below cursor (or within a visual range)"},
          {command_reload_file, "reload_file", "reload the file in the current view, overwriting any changes outstanding"},
          {command_reload_config, "reload_config", "reload the config shared object"},
          {command_buffer_type, "buffer_type", "set the current buffer's type: c, python, java, bash, config, diff, plain"},
          {command_new_buffer, "new_buffer", "create a new buffer"},
          {command_rename_buffer, "rename_buffer", "rename the current buffer"},
          {command_jump_list, "jump_list", "jump to 'next' or 'previous' jump location based on argument passed in"},
          {command_line_number, "line_number", "change line number mode: 'none', 'absolute', 'relative', or 'both'"},
          {command_terminal_command, "terminal_command", "run a command in the terminal"},
          {command_terminal_command_in_view, "terminal_command_in_view", "run a command in the terminal, and switch to it in view"},
          {command_man_page_on_word_under_cursor, "man_page_on_word_under_cursor", "run man on the word under the cursor"},
     };

     int64_t command_entry_count = sizeof(command_entries) / sizeof(command_entries[0]);
     app.command_entries = malloc(command_entry_count * sizeof(*app.command_entries));
     app.command_entry_count = command_entry_count;
     for(int64_t i = 0; i < command_entry_count; i++){
          app.command_entries[i] = command_entries[i];
     }

     app.buffer_list_buffer = new_buffer();
     app.yank_list_buffer = new_buffer();
     app.complete_list_buffer = new_buffer();
     app.macro_list_buffer = new_buffer();
     app.mark_list_buffer = new_buffer();

     // init buffers
     {
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

          app.buffer_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.yank_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.complete_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.macro_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.mark_list_buffer->status = CE_BUFFER_STATUS_NONE;

          app.buffer_list_buffer->no_line_numbers = true;
          app.yank_list_buffer->no_line_numbers = true;
          app.complete_list_buffer->no_line_numbers = true;
          app.macro_list_buffer->no_line_numbers = true;
          app.mark_list_buffer->no_line_numbers = true;

          CeAppBufferData_t* buffer_data = app.complete_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_completions;

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

     // init vim
     {
          ce_vim_init(&app.vim);
     }

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
     {
          if(!user_config_init(&app.user_config, config_filepath)) return 1;
          app.user_config.init_func(&app);
     }

     // init terminal
     {
          getmaxyx(stdscr, app.terminal_height, app.terminal_width);
          if(app.config_options.terminal_scroll_back < app.terminal_height){
               app.config_options.terminal_scroll_back = app.terminal_height - 1;
          }
          ce_terminal_init(&app.terminal, app.terminal_width, app.terminal_height - 1, app.config_options.terminal_scroll_back);
          ce_buffer_node_insert(&app.buffer_node_head, app.terminal.buffer);

          app.terminal.lines_buffer->app_data = calloc(1, sizeof(CeAppBufferData_t));
          app.terminal.lines_buffer->no_line_numbers = true;
          CeAppBufferData_t* buffer_data = app.terminal.lines_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_terminal;
          app.terminal.lines_buffer->syntax_data = &app.terminal;

          app.terminal.alternate_lines_buffer->app_data = calloc(1, sizeof(CeAppBufferData_t));
          app.terminal.alternate_lines_buffer->no_line_numbers = true;
          buffer_data = app.terminal.alternate_lines_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_terminal;
          app.terminal.alternate_lines_buffer->syntax_data = &app.terminal;
     }

     // init command complete
     {
          const char** commands = malloc(app.command_entry_count * sizeof(*commands));
          for(int64_t i = 0; i < app.command_entry_count; i++){
               commands[i] = strdup(app.command_entries[i].name);
          }
          ce_complete_init(&app.command_complete, commands, app.command_entry_count);
          for(int64_t i = 0; i < app.command_entry_count; i++){
               free((char*)(commands[i]));
          }
          free(commands);
     }

     // init draw thread
     pthread_t thread_draw;
     {
          pthread_create(&thread_draw, NULL, draw_thread, &app);
          app.ready_to_draw = true;
     }

     // main loop
     while(!app.quit){
          // TODO: we can optimize by only resizing when we see a resized event
          ce_app_update_terminal_view(&app);

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

          // handle input from the user
          int key = getch();
          app_handle_key(&app, view, key);

          if(view){
               if(view->buffer == app.terminal.lines_buffer || view->buffer == app.terminal.alternate_lines_buffer){
                    ce_view_follow_cursor(view, 1, 1, app.config_options.tab_width);
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

          // tell the draw thread we are ready to draw
          app.ready_to_draw = true;
     }

     // cleanup
     pthread_cancel(thread_draw);
     pthread_join(thread_draw, NULL);
     app.user_config.free_func(&app);
     user_config_free(&app.user_config);

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
               if(itr->buffer == app.terminal.lines_buffer ||
                  itr->buffer == app.terminal.alternate_lines_buffer){
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
     ce_terminal_free(&app.terminal);
     ce_layout_free(&app.tab_list_layout);
     ce_vim_free(&app.vim);
     endwin();
     return 0;
}
