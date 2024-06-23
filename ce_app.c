#include "ce_app.h"
#include "ce_commands.h"
#include "ce_subprocess.h"
#include "ce_syntax.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <ncurses.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/wait.h>

int g_shell_command_ready_fds[2];
bool g_shell_command_should_die = false;

bool ce_buffer_node_insert(CeBufferNode_t** head, CeBuffer_t* buffer){
     CeBufferNode_t* node = malloc(sizeof(*node));
     if(!node) return false;
     node->buffer = buffer;
     node->next = *head;
     *head = node;
     return true;
}

static void free_buffer_node(CeBufferNode_t* node){
     CeAppBufferData_t* buffer_data = node->buffer->app_data;
     if(buffer_data) free(buffer_data->base_directory);
     free(node->buffer->app_data);
     ce_buffer_free(node->buffer);
     free(node->buffer);
     free(node);
}

CeBufferNode_t* ce_buffer_node_unlink(CeBufferNode_t** head, CeBuffer_t* buffer){
     CeBufferNode_t* prev = NULL;
     CeBufferNode_t* itr = *head;
     while(itr){
          if(itr->buffer == buffer) break;
          prev = itr;
          itr = itr->next;
     }

     if(!itr) return NULL;

     if(prev){
          prev->next = itr->next;
     }else{
          *head = (*head)->next;
     }

     return itr;
}

bool ce_buffer_node_delete(CeBufferNode_t** head, CeBuffer_t* buffer){
     CeBufferNode_t* node = ce_buffer_node_unlink(head, buffer);
     free_buffer_node(node);
     return true;
}

void ce_buffer_node_free(CeBufferNode_t** head){
     CeBufferNode_t* itr = *head;
     while(itr){
          CeBufferNode_t* tmp = itr;
          itr = itr->next;
          free_buffer_node(tmp);
     }
     *head = NULL;
}

CeStringNode_t* ce_string_node_insert(CeStringNode_t** head, const char* string){
     CeStringNode_t* tail = *head;
     CeStringNode_t* node;
     if(tail){
          while(tail->next) tail = tail->next;

          // NOTE: we probably don't want this if we want the linked list to be general
          // skip the insertion if the string matches the previous string
          if(strcmp(string, tail->string) == 0) return NULL;

          node = calloc(1, sizeof(*node));
          if(!node) return node;
          node->string = strdup(string);

          tail->next = node;
          node->prev = tail;
     }else{
          node = calloc(1, sizeof(*node));
          if(!node) return node;
          node->string = strdup(string);

          *head = node;
     }

     return node;
}

void ce_string_node_free(CeStringNode_t** head){
     CeStringNode_t* itr = *head;
     while(itr){
          CeStringNode_t* tmp = itr;
          itr = itr->next;
          free(tmp->string);
          free(tmp);
     }

     *head = NULL;
}

bool ce_history_insert(CeHistory_t* history, const char* string){
     CeStringNode_t* new_node = ce_string_node_insert(&history->head, string);
     if(!new_node) return false;
     ce_history_reset_current(history);
     return true;
}

char* ce_history_previous(CeHistory_t* history){
     if(history->current){
          if(history->current->prev){
               history->current = history->current->prev;
          }

          return history->current->string;
     }else{
          history->current = history->head;
          if(history->current){
               while(history->current->next) history->current = history->current->next;
               return history->current->string;
          }
     }

     return NULL;
}

char* ce_history_next(CeHistory_t* history){
     if(history->current){
          history->current = history->current->next;
          if(history->current) return history->current->string;
     }

     return NULL;
}

void ce_history_reset_current(CeHistory_t* history){
     history->current = NULL;
}

void ce_history_free(CeHistory_t* history){
     ce_string_node_free(&history->head);
}

void ce_convert_bind_defs(CeKeyBinds_t* binds, CeKeyBindDef_t* bind_defs, int64_t bind_def_count){
     if(binds->count){
          for(int64_t i = 0; i < binds->count; ++i){
               free(binds->binds[i].keys);
          }
          free(binds->binds);
     }

     binds->count = bind_def_count;
     binds->binds = malloc(binds->count * sizeof(*binds->binds));

     for(int64_t i = 0; i < binds->count; ++i){
          ce_command_parse(&binds->binds[i].command, bind_defs[i].command);
          binds->binds[i].key_count = 0;

          for(int k = 0; k < 4; ++k){
               if(bind_defs[i].keys[k] == 0) break;
               binds->binds[i].key_count++;
          }

          if(!binds->binds[i].key_count) continue;

          binds->binds[i].keys = malloc(binds->binds[i].key_count * sizeof(binds->binds[i].keys[0]));

          for(int k = 0; k < binds->binds[i].key_count; ++k){
               binds->binds[i].keys[k] = bind_defs[i].keys[k];
          }
     }
}

CeComplete_t* ce_app_is_completing(CeApp_t* app){
     if(app->input_complete_func && app->input_complete.count) return &app->input_complete;
     return NULL;
}

void ce_set_vim_key_bind(CeVimKeyBind_t* key_binds, int64_t* key_bind_count, CeRune_t key, CeVimParseFunc_t* parse_func){
     for(int64_t i = 0; i < *key_bind_count; ++i){
          CeVimKeyBind_t* key_bind = key_binds + i;
          if(key_bind->key == key){
               key_bind->function = parse_func;
               return;
          }
     }

     // we didn't find the key to override it, so we add the binding
     ce_vim_add_key_bind(key_binds, key_bind_count, key, parse_func);
}

void ce_extend_commands(CeCommandEntry_t** command_entries, int64_t* command_entry_count, CeCommandEntry_t* new_command_entries,
                        int64_t new_command_entry_count){
     int64_t final_command_entry_count = *command_entry_count + new_command_entry_count;
     *command_entries = realloc(*command_entries, final_command_entry_count * sizeof(**command_entries));
     for(int64_t i = 0; i < new_command_entry_count; i++){
          (*command_entries)[i + *command_entry_count] = new_command_entries[i];
     }
     *command_entry_count = final_command_entry_count;
}

void ce_syntax_highlight_completions(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                     CeSyntaxDef_t* syntax_defs, void* user_data){
     if(!user_data) return;
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     CeComplete_t* complete = user_data;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     int64_t match_len = 0;
     if(complete->current_match) match_len = strlen(complete->current_match);

     // figure out which line to highlight
     int64_t selected = 0;
     for(int64_t i = 0; i < complete->count; i++){
          if(complete->elements[i].match){
               if(i == complete->current) break;
               selected++;
          }
     }

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          char* end_of_match = strchr(line, ':');
          CePoint_t match_point = {0, y};

          if(selected == y){
               int fg = ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_SELECTED, ce_draw_color_list_last_fg_color(draw_color_list));
               int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_SELECTED, ce_draw_color_list_last_bg_color(draw_color_list));
               ce_draw_color_list_insert(draw_color_list, fg, bg, match_point);
          }else{
               ce_draw_color_list_insert(draw_color_list, COLOR_DEFAULT, COLOR_DEFAULT, match_point);
          }

          if(complete->current_match && strlen(complete->current_match)){
               char* prev_match = line;
               char* match = NULL;
               while((match = strstr(prev_match, complete->current_match))){
                    if(end_of_match && match >= end_of_match) break;
                    match_point.x = ce_utf8_strlen_between(line, match) - 1;
                    prev_match = match + match_len;
                    int fg = ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_MATCH, ce_draw_color_list_last_fg_color(draw_color_list));
                    int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_MATCH, ce_draw_color_list_last_bg_color(draw_color_list));
                    ce_draw_color_list_insert(draw_color_list, fg, bg, match_point);

                    match_point.x += match_len;

                    if(selected == y){
                         fg = ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_SELECTED, COLOR_DEFAULT);
                         bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_SELECTED, ce_draw_color_list_last_bg_color(draw_color_list));
                         ce_draw_color_list_insert(draw_color_list, fg, bg, match_point);
                    }else{
                         ce_draw_color_list_insert(draw_color_list, COLOR_DEFAULT, COLOR_DEFAULT, match_point);
                    }
               }
          }
     }
}

void ce_syntax_highlight_message(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                 CeSyntaxDef_t* syntax_defs, void* user_data){
     CeConfigOptions_t* config_options = user_data;
     ce_draw_color_list_insert(draw_color_list, config_options->message_fg_color, config_options->message_bg_color, (CePoint_t){0, 0});
}

void ce_jump_list_insert(CeJumpList_t* jump_list, CeDestination_t destination){
     if(jump_list->count < JUMP_LIST_DESTINATION_COUNT){
          if(jump_list->count > 0){
               jump_list->itr++;
               jump_list->current = jump_list->itr;
          }
          jump_list->destinations[jump_list->itr] = destination;
          jump_list->count++;
          return;
     }

     // shift all destinations down
     for(int i = 1; i <= jump_list->itr; i++){
          jump_list->destinations[i - 1] = jump_list->destinations[i];
     }

     jump_list->destinations[jump_list->itr] = destination;
}

CeDestination_t* ce_jump_list_previous(CeJumpList_t* jump_list){
     if(jump_list->count == 0) return NULL;
     if(jump_list->itr >= (jump_list->count - 1)) return 0;
     jump_list->itr++;
     jump_list->current = jump_list->itr;
     return jump_list->destinations + jump_list->itr;
}

CeDestination_t* ce_jump_list_next(CeJumpList_t* jump_list){
     if(jump_list->count == 0) return NULL;
     if(jump_list->itr < 0) return NULL;
     jump_list->current = jump_list->itr;
     jump_list->itr--;
     return jump_list->destinations + jump_list->current;
}

CeDestination_t* ce_jump_list_current(CeJumpList_t* jump_list){
     if(jump_list->count == 0) return NULL;
     return jump_list->destinations + jump_list->current;
}

void ce_view_switch_buffer(CeView_t* view, CeBuffer_t* buffer, CeVim_t* vim,
                           CeConfigOptions_t* config_options, bool insert_into_jump_list){
     CeAppViewData_t* view_data = view->user_data;
     CeJumpList_t* jump_list = &view_data->jump_list;

     if(view_data->prev_buffer != view->buffer) view_data->prev_buffer = view->buffer;

     // if the old buffer is not in the jump list, then add it
     if(insert_into_jump_list){
          bool add_current = false;
          CeDestination_t* current_destination = ce_jump_list_current(jump_list);
          if(current_destination){
               if(!ce_destination_in_view(current_destination, view)){
                    add_current = true;
               }
          }else{
               add_current = true;
          }

          CeDestination_t destination = {};

          if(add_current){
               destination.point = view->cursor;
               strncpy(destination.filepath, view->buffer->name, PATH_MAX);
               ce_jump_list_insert(jump_list, destination);
          }
     }

     // save the cursor on the old buffer
     view->buffer->cursor_save = view->cursor;
     view->buffer->scroll_save = view->scroll;

     // update new buffer, using the buffer's cursor
     view->buffer = buffer;
     view->cursor = buffer->cursor_save;
     view->scroll = buffer->scroll_save;

     ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off, config_options->tab_width);

     // add to the jump list
     if(insert_into_jump_list){
          CeDestination_t destination = {};
          destination.point = view->cursor;
          strncpy(destination.filepath, view->buffer->name, PATH_MAX);
          ce_jump_list_insert(jump_list, destination);
     }

     vim->mode = CE_VIM_MODE_NORMAL;
}

void ce_app_clear_filepath_cache(CeApp_t* app){
     for(int64_t i = 0; i < app->cached_filepath_count; i++){
         free(app->cached_filepaths[i]);
     }
     free(app->cached_filepaths);
}

void ce_app_update_terminal_view(CeApp_t* app, int width, int height) {
     app->terminal_width = width;
     app->terminal_height = height;
     app->terminal_rect = (CeRect_t){0, app->terminal_width - 1, 0, app->terminal_height - 1};
     ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);
}

bool ce_app_switch_to_prev_buffer_in_view(CeApp_t* app, CeView_t* view, bool switch_if_deleted){
     CeAppViewData_t* view_data = view->user_data;
     if(!view_data->prev_buffer) return false;

     bool deleted = true;
     CeBufferNode_t* itr = app->buffer_node_head;
     while(itr){
          if(itr->buffer == view_data->prev_buffer){
               deleted = false;
               break;
          }
          itr = itr->next;
     }

     if(deleted){
          if(switch_if_deleted){
               view_data->prev_buffer = app->buffer_node_head->buffer;
          }else{
               view_data->prev_buffer = NULL;
               return false;
          }
     }

     ce_view_switch_buffer(view, view_data->prev_buffer, &app->vim, &app->config_options, true);
     return true;
}

bool enable_input_mode(CeView_t* input_view, CeView_t* view, CeVim_t* vim, const char* dialogue){
     // update input view to overlay the current view
     input_view_overlay(input_view, view);

     // update name based on dialog
     free(input_view->buffer->app_data);
     bool success = ce_buffer_alloc(input_view->buffer, 1, dialogue);
     input_view->buffer->app_data = calloc(1, sizeof(CeAppBufferData_t));
     input_view->buffer->no_line_numbers = true;
     input_view->cursor = (CePoint_t){0, 0};
     vim->mode = CE_VIM_MODE_INSERT;
     ce_rune_node_free(&vim->insert_rune_head);

     return success;
}

void input_view_overlay(CeView_t* input_view, CeView_t* view){
     input_view->rect.left = view->rect.left;
     input_view->rect.right = view->rect.right - 1;
     input_view->rect.bottom = view->rect.bottom;
     int64_t max_height = (view->rect.bottom - view->rect.top) - 1;
     int64_t height = input_view->buffer->line_count;
     if(height <= 0) height = 1;
     if(height > max_height) height = max_height;
     input_view->rect.top = view->rect.bottom - height;
}

CePoint_t view_cursor_on_screen(CeView_t* view, int64_t tab_width, CeLineNumber_t line_number){
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

CeBuffer_t* load_file_into_view(CeBufferNode_t** buffer_node_head, CeView_t* view,
                                CeConfigOptions_t* config_options, CeVim_t* vim,
                                bool insert_into_jump_list, const char* filepath){
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
               ce_view_switch_buffer(view, itr->buffer, vim, config_options, insert_into_jump_list);
               return itr->buffer;
          }
          itr = itr->next;
     }

     // load file
     CeBuffer_t* buffer = new_buffer();
     if(ce_buffer_load_file(buffer, load_path)){
          ce_buffer_node_insert(buffer_node_head, buffer);
          ce_view_switch_buffer(view, buffer, vim, config_options, insert_into_jump_list);
          determine_buffer_syntax(buffer);
     }else{
          free(buffer);
          return NULL;
     }

     return buffer;
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
        string_ends_with(buffer->name, ".h") ||
        string_ends_with(buffer->name, ".js")){
          buffer_data->syntax_function = ce_syntax_highlight_c;
     }else if(string_ends_with(buffer->name, ".cpp") ||
              string_ends_with(buffer->name, ".hpp") ||
              string_ends_with(buffer->name, ".hh") ||
              string_ends_with(buffer->name, ".cc")){
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

char* directory_from_filename(const char* filename){
     const char* last_slash = strrchr(filename, '/');
     char* directory = NULL;
     if(last_slash) directory = strndup(filename, last_slash - filename);
     return directory;
}

char* buffer_base_directory(CeBuffer_t* buffer){
     CeAppBufferData_t* buffer_data = buffer->app_data;
     if(buffer_data->base_directory) return strdup(buffer_data->base_directory);

     return directory_from_filename(buffer->name);
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
          ce_complete_init(complete, (const char**)(files), NULL, file_count);
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

void build_complete_list(CeBuffer_t* buffer, CeComplete_t* complete){
     ce_buffer_empty(buffer);
     buffer->syntax_data = complete;
     char line[256];
     int64_t cursor = 0;
     int max_string_len = 0;
     for(int64_t i = 0; i < complete->count; i++){
          if(complete->elements[i].match){
               int len = strlen(complete->elements[i].string);
               if(len > max_string_len) max_string_len = len;
          }
     }
     for(int64_t i = 0; i < complete->count; i++){
          if(complete->elements[i].match){
               if(i == complete->current) cursor = buffer->line_count;
               if(complete->elements[i].description){
                    snprintf(line, 256, "%-*s : %s", max_string_len, complete->elements[i].string, complete->elements[i].description);
               }else{
                    snprintf(line, 256, "%s", complete->elements[i].string);
               }
               buffer_append_on_new_line(buffer, line);
          }
     }

     // TODO: figure out why we have to account for this case
     if(buffer->line_count == 1 && cursor == 1) cursor = 0;

     buffer->cursor_save = (CePoint_t){0, cursor};
     buffer->status = CE_BUFFER_STATUS_READONLY;
}

bool buffer_append_on_new_line(CeBuffer_t* buffer, const char* string){
     int64_t old_line_count = buffer->line_count;
     if(old_line_count == 1 && strlen(buffer->lines[0]) == 0){
          return ce_buffer_insert_string(buffer, string, (CePoint_t){0, 0});
     }
     int64_t new_line_count = buffer->line_count + 1;
     buffer->lines = realloc(buffer->lines, new_line_count * sizeof(buffer->lines[0]));
     if(buffer->lines == NULL) return false;
     buffer->line_count = new_line_count;
     buffer->lines[old_line_count] = calloc(1, sizeof(buffer->lines[old_line_count]));
     return ce_buffer_insert_string(buffer, string, (CePoint_t){0, old_line_count});
}

CeDestination_t scan_line_for_destination(const char* line){
     CeDestination_t destination = {};
     destination.point = (CePoint_t){-1, -1};

     // TODO: more formats, including git grep

     // valgrind format
     // '==7330==    by 0x638B16A: initializer (ce_config.c:1983)'
     if(line[0] == '=' && line[1] == '='){
          char* open_paren = strchr(line, '(');
          char* close_paren = strchr(line, ')');
          if(open_paren && close_paren){
               char* file_end = strchr(open_paren, ':');
               if(file_end){
                    char* end = NULL;
                    char* number_start = file_end + 1;
                    destination.point.y = strtol(number_start, &end, 10);
                    if(end != number_start){
                         if(destination.point.y > 0) destination.point.y--;
                         destination.point.x = 0;

                         int64_t filepath_len = file_end - (open_paren + 1);
                         if(filepath_len >= PATH_MAX) return destination;
                         strncpy(destination.filepath, (open_paren + 1), filepath_len);
                         destination.filepath[filepath_len] = 0;
                         return destination;
                    }
               }
          }
     }

     // grep/gcc format
     // ce_app.c:1515:23
     char* file_end = strchr(line, ':');
     if(file_end){
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
               char* row_start = file_end + 1;
               destination.point.y = strtol(row_start, &end, 10);
               if(end == row_start) destination.point.y = -1;

               if(destination.point.y > 0){
                    destination.point.y--; // account for format which is 1 indexed
               }

               if(col_end){
                    char* col_start = row_end + 1;
                    destination.point.x = strtol(col_start, &end, 10);
                    if(end == col_start) destination.point.x = 0;
                    if(destination.point.x > 0) destination.point.x--; // account for format which is 1 indexed
               }else{
                    destination.point.x = 0;
               }
          }

          return destination;
     }

     // cscope format
     // ce_app.c buffer_append_on_new_line 694 bool buffer_append_on_new_line(CeBuffer_t* buffer, const char * string){
     file_end = strchr(line, ' ');
     if(file_end){
          char* symbol_end = strchr(file_end + 1, ' ');
          if(symbol_end){
               char* row_start = symbol_end + 1;
               char* end = NULL;
               destination.point.y = strtol(row_start, &end, 10);
               if(end != row_start){
                    if(destination.point.y > 0) destination.point.y--;
                    destination.point.x = 0;

                    int64_t filepath_len = file_end - line;
                    if(filepath_len >= PATH_MAX) return destination;
                    strncpy(destination.filepath, line, filepath_len);
                    destination.filepath[filepath_len] = 0;
                    return destination;
               }
          }
     }

     return destination;
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

int64_t istrtol(const CeRune_t* istr, const CeRune_t** end_of_numbers){
     int64_t value = 0;
     const CeRune_t* itr = istr;

     while(*itr){
          if(isdigit(*itr)){
               value *= 10;
               value += *itr - '0';
          }else{
               if(itr != istr) *end_of_numbers = itr;
               break;
          }

          itr++;
     }

     if(!(*itr) && itr != istr) *end_of_numbers = itr;

     return value;
}

int64_t istrlen(const CeRune_t* istr){
     const CeRune_t* start = istr;
     while(*istr) istr++;
     return istr - start;
}

bool ce_destination_in_view(CeDestination_t* destination, CeView_t* view){
     if(!view) return false;

     int64_t view_width = ce_view_width(view);
     int64_t view_height = ce_view_height(view);
     CeRect_t view_rect = {view->scroll.x, view->scroll.x + view_width, view->scroll.y, view->scroll.y + view_height};

     return strcmp(destination->filepath, view->buffer->name) == 0 &&
            ce_point_in_rect(destination->point, view_rect);
}

void ce_app_init_default_commands(CeApp_t* app){
     CeCommandEntry_t command_entries[] = {
          {command_balance_layout, "balance_layout", "rebalance layout based on the node tree"},
          {command_blank, "blank", "empty command"},
          {command_command, "command", "interactively send a commmand"},
          {command_delete_layout, "delete_layout", "delete the current layout (unless it's the only one left)"},
          {command_goto_destination_in_line, "goto_destination_in_line", "scan current line for destination formats"},
          {command_goto_next_destination, "goto_next_destination", "find the next line in the buffer that contains a destination to goto"},
          {command_goto_prev_destination, "goto_prev_destination", "find the previous line in the buffer that contains a destination to goto"},
          {command_goto_prev_buffer_in_view, "goto_prev_buffer_in_view", "go to the previous buffer that was shown in the current view"},
          {command_jump_list, "jump_list", "jump to 'next' or 'previous' jump location based on argument passed in"},
          {command_line_number, "line_number", "change line number mode: 'none', 'absolute', 'relative', or 'both'"},
          {command_load_file, "load_file", "load a file (optionally specified)"},
          {command_load_project, "load_project", "search backward in the tree for a .git folder, then find all files in the project and autocomplete on them. Optionally specify directories to ignore in the arguments."},
          {command_load_directory_files, "load_directory_files", "find all files recursively in the specified directory and autocomplete on them."},
          {command_load_cached_files, "load_cached_files", "autocomplete based on last cached recursive file search."},
          {command_man_page_on_word_under_cursor, "man_page_on_word_under_cursor", "run man on the word under the cursor"},
          {command_new_buffer, "new_buffer", "create a new buffer"},
          {command_new_tab, "new_tab", "create a new tab"},
          {command_noh, "noh", "turn off search highlighting"},
          {command_quit, "quit", "quit ce"},
          {command_redraw, "redraw", "redraw the entire editor"},
          {command_regex_search, "regex_search", "interactive regex search 'forward' or 'backward'"},
          {command_reload_config, "reload_config", "reload the config shared object"},
          {command_reload_file, "reload_file", "reload the file in the current view, overwriting any changes outstanding"},
          {command_rename_buffer, "rename_buffer", "rename the current buffer"},
          {command_replace_all, "replace_all", "replace all occurances below cursor (or within a visual range) with the previous search if 1 argument is given, if 2 are given replaces the first argument with the second argument"},
          {command_resize_layout, "resize_layout", "resize the current view. specify 'expand' or 'shrink', direction 'left', 'right', 'up', 'down' and an amount"},
          {command_save_all_and_quit, "save_all_and_quit", "save all modified buffers and quit the editor"},
          {command_save_buffer, "save_buffer", "save the currently selected view's buffer"},
          {command_search, "search", "interactive search 'forward' or 'backward'"},
          {command_select_adjacent_layout, "select_adjacent_layout", "select 'left', 'right', 'up' or 'down adjacent layouts"},
          {command_select_adjacent_tab, "select_adjacent_tab", "selects either the 'left' or 'right' tab"},
          {command_select_parent_layout, "select_parent_layout", "select the parent of the current layout"},
          {command_setnopaste, "setnopaste", "done pasting, so turn on auto indentation again"},
          {command_setpaste, "setpaste", "about to paste, so turn off auto indentation"},
          {command_show_buffers, "show_buffers", "show the list of buffers"},
          {command_show_jumps, "show_jumps", "show the state of your jumps"},
          {command_show_macros, "show_macros", "show the state of your macros"},
          {command_show_marks, "show_marks", "show the state of your vim marks"},
          {command_show_yanks, "show_yanks", "show the state of your vim yanks"},
          {command_split_layout, "split_layout", "split the current layout 'horizontal' or 'vertical' into 2 layouts"},
          {command_switch_buffer, "switch_buffer", "open dialogue to switch buffer by name"},
          {command_syntax, "syntax", "set the current buffer's type: 'c', 'cpp', 'python', 'java', 'bash', 'config', 'diff', 'plain'"},
          {command_toggle_log_keys_pressed, "toggle_log_keys_pressed", "debug command to log key presses"},
          {command_shell_command, "shell_command", "run a shell command"},
          {command_shell_command_relative, "shell_command_relative", "run a shell command relative to the current buffer"},
          {command_vim_cn, "cn", "vim's cn command to select the goto the next build error"},
          {command_vim_cp, "cp", "vim's cn command to select the goto the previous build error"},
          {command_vim_e, "e", "vim's e command to load a file specified"},
          {command_vim_find, "find", "vim's find command to search for files recursively"},
          {command_vim_make, "make", "vim's make command"},
          {command_vim_q, "q", "vim's q command to close the current window"},
          {command_vim_sp, "sp", "vim's sp command to split the window vertically. It optionally takes a file to open"},
          {command_vim_tabnew, "tabnew", "vim's tabnew command to create a new tab"},
          {command_vim_tabnext, "tabnext", "vim's tabnext command to select the next tab"},
          {command_vim_tabprevious, "tabprevious", "vim's tabprevious command to select the previous tab"},
          {command_vim_vsp, "vsp", "vim's vsp command to split the window vertically. It optionally takes a file to open"},
          {command_vim_w, "w", "vim's w command to save the current buffer"},
          {command_vim_wq, "wq", "vim's w command to save the current buffer and close the current window"},
          {command_vim_wqa, "wqa", "vim's wqa command save all modified buffers and quit the editor"},
          {command_vim_q_exclam, "q!", "vim's q! command to delete the current view and if it is the only view then to quit the editor"},
          {command_vim_qa_exclam, "qa!", "vim's qa! command to quit the editor regardless of the open views"},
          {command_vim_xa, "xa", "vim's xa command save all modified buffers and quit the editor"},
     };

     int64_t command_entry_count = sizeof(command_entries) / sizeof(command_entries[0]);
     app->command_entries = malloc(command_entry_count * sizeof(*app->command_entries));
     app->command_entry_count = command_entry_count;
     for(int64_t i = 0; i < command_entry_count; i++){
          app->command_entries[i] = command_entries[i];
     }
}

void ce_app_init_command_completion(CeApp_t* app, CeComplete_t* complete){
     const char** commands = malloc(app->command_entry_count * sizeof(*commands));
     const char** descriptions = malloc(app->command_entry_count * sizeof(*descriptions));
     for(int64_t i = 0; i < app->command_entry_count; i++){
          commands[i] = strdup(app->command_entries[i].name);
          descriptions[i] = strdup(app->command_entries[i].description);
     }
     ce_complete_init(complete, commands, descriptions, app->command_entry_count);
     for(int64_t i = 0; i < app->command_entry_count; i++){
          free((char*)(commands[i]));
          free((char*)(descriptions[i]));
     }
     free(commands);
     free(descriptions);
}

void ce_app_message(CeApp_t* app, const char* fmt, ...){
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) return;
     CeView_t* view = &tab_layout->tab.current->view;

     app->message_view.rect.left = view->rect.left;
     app->message_view.rect.right = view->rect.right - 1;
     app->message_view.rect.bottom = view->rect.bottom + 1;
     app->message_view.rect.top = view->rect.bottom;

     gettimeofday(&app->message_time, NULL);
     app->message_mode = true;

     free(app->message_view.buffer->app_data);
     ce_buffer_alloc(app->message_view.buffer, 1, "[message]");
     app->message_view.buffer->app_data = calloc(1, sizeof(CeAppBufferData_t));
     app->message_view.cursor = (CePoint_t){0, 0};

     char message_buffer[BUFSIZ];
     va_list args;
     va_start(args, fmt);
     vsnprintf(message_buffer, BUFSIZ, fmt, args);
     va_end(args);

     ce_buffer_insert_string(app->message_view.buffer, message_buffer, app->message_view.cursor);
     app->message_view.buffer->status = CE_BUFFER_STATUS_READONLY;
     app->message_view.buffer->no_line_numbers = true;

     CeAppBufferData_t* buffer_data = app->message_view.buffer->app_data;
     buffer_data->syntax_function = ce_syntax_highlight_message;
     app->message_view.buffer->syntax_data = &app->config_options;
}

void ce_app_input(CeApp_t* app, const char* dialogue, CeInputCompleteFunc* input_complete_func){
     CeView_t* input_view = &app->input_view;

     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) return;
     CeView_t* view = &tab_layout->tab.current->view;

     input_view_overlay(input_view, view);

     free(input_view->buffer->app_data);

     ce_buffer_alloc(input_view->buffer, 1, dialogue);
     input_view->buffer->app_data = calloc(1, sizeof(CeAppBufferData_t));
     input_view->buffer->no_line_numbers = true;
     input_view->buffer->no_highlight_current_line = true;
     input_view->cursor = (CePoint_t){0, 0};

     app->vim_visual_save.mode = app->vim.mode;
     app->vim_visual_save.visual_point = app->visual.point;

     app->vim.mode = CE_VIM_MODE_INSERT;
     ce_rune_node_free(&app->vim.insert_rune_head);

     app->input_complete_func = input_complete_func;
     ce_complete_free(&app->input_complete);
}

bool ce_app_apply_completion(CeApp_t* app){
     CeComplete_t* complete = ce_app_is_completing(app);
     if(app->vim.mode == CE_VIM_MODE_INSERT && complete){
          if(complete->current >= 0){
               int64_t completion_len = strlen(complete->elements[complete->current].string);
               int64_t input_len = strlen(app->input_view.buffer->lines[app->input_view.cursor.y]);
               int64_t input_offset = 0;
               if(input_len > completion_len) input_offset = input_len - completion_len;
               if(strcmp(complete->elements[complete->current].string, app->input_view.buffer->lines[app->input_view.cursor.y] + input_offset) == 0){
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
          }

          return true;
     }

     return false;
}

bool unsaved_buffers_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
     if(strcmp(app->input_view.buffer->lines[0], "y") == 0 ||
        strcmp(app->input_view.buffer->lines[0], "Y") == 0){
          app->quit = true;
     }

     return true;
}

bool buffer_modified_outside_editor_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
     if(strcmp(app->input_view.buffer->lines[0], "y") == 0 ||
        strcmp(app->input_view.buffer->lines[0], "Y") == 0){
          CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
          CeView_t* view = NULL;

          if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
               view = &tab_layout->tab.current->view;
          }else{
               return false;
          }

          ce_buffer_save(view->buffer);
     }

     return true;
}

bool command_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) return false;
     CeView_t* view = &tab_layout->tab.current->view;

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
          }
     }else{
          // convert and run the command
          CeCommand_t command = {};
          if(!ce_command_parse(&command, app->input_view.buffer->lines[0])){
               ce_log("failed to parse command: '%s'\n", app->input_view.buffer->lines[0]);
          }else{
               CeCommandFunc_t* command_func = NULL;
               CeCommandEntry_t* entry = NULL;
               for(int64_t i = 0; i < app->command_entry_count; i++){
                    entry = app->command_entries + i;
                    if(strcmp(entry->name, command.name) == 0){
                         command_func = entry->func;
                         break;
                    }
               }

               if(command_func){
                    CeCommandStatus_t cs = command_func(&command, app);
                    switch(cs){
                    default:
                         break;
                    case CE_COMMAND_PRINT_HELP:
                         ce_app_message(app, "%s: %s", entry->name, entry->description);
                         break;
                    }
                    ce_history_insert(&app->command_history, app->input_view.buffer->lines[0]);
               }else{
                    ce_app_message(app, "unknown command: '%s'", command.name);
               }

               ce_command_free(&command);
          }
     }

     return true;
}

bool load_file_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
     // TODO: compress with code above
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) return false;
     CeView_t* view = &tab_layout->tab.current->view;

     char* base_directory = buffer_base_directory(view->buffer);
     char filepath[PATH_MAX];
     for(int64_t i = 0; i < app->input_view.buffer->line_count; i++){
          if(base_directory && app->input_view.buffer->lines[i][0] != '/'){
               snprintf(filepath, PATH_MAX, "%s/%s", base_directory, app->input_view.buffer->lines[i]);
          }else{
               strncpy(filepath, app->input_view.buffer->lines[i], PATH_MAX);
          }
          if(!load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                  true, filepath)){
               ce_app_message(app, "failed to load file '%s': '%s'", filepath, strerror(errno));
               errno = 0;
               return false;
          }
     }

     free(base_directory);
     return true;
}

bool load_project_file_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
     // TODO: compress with code above
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) return false;
     CeView_t* view = &tab_layout->tab.current->view;

     for(int64_t i = 0; i < input_buffer->line_count; i++){
          if(!load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                  true, input_buffer->lines[i])){
               ce_app_message(app, "failed to load file '%s': '%s'", input_buffer->lines[i], strerror(errno));
               errno = 0;
               return false;
          }
     }

     return true;
}

bool search_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
     // TODO: compress with code above
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) return false;
     CeView_t* view = &tab_layout->tab.current->view;

     ce_history_insert(&app->search_history, app->input_view.buffer->lines[0]);

     // update yanks
     CeVimYank_t* yank = app->vim.yanks + ce_vim_register_index('/');
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
     return true;
}

bool switch_buffer_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
     // TODO: compress with code above
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) return false;
     CeView_t* view = &tab_layout->tab.current->view;

     CeAppViewData_t* view_data = view->user_data;
     CeJumpList_t* jump_list = &view_data->jump_list;
     CeBufferNode_t* itr = app->buffer_node_head;
     while(itr){
          if(strcmp(itr->buffer->name, app->input_view.buffer->lines[0]) == 0){
               ce_view_switch_buffer(view, itr->buffer, &app->vim, &app->config_options,
                                     jump_list);
               break;
          }
          itr = itr->next;
     }

     return true;
}

bool replace_all_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
     // TODO: compress with code above
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW) return false;
     CeView_t* view = &tab_layout->tab.current->view;

     int64_t index = ce_vim_register_index('/');
     CeVimYank_t* yank = app->vim.yanks + index;
     if(yank->text){
          replace_all(view, &app->vim_visual_save, yank->text, app->input_view.buffer->lines[0]);
     }
     return true;
}

bool edit_macro_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
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
     return true;
}

bool edit_yank_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer){
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
     return true;
}

typedef struct{
     CeBuffer_t* buffer;
     char* command;
     volatile bool* volatile should_scroll;
}ShellCommandData_t;

typedef struct{
     ShellCommandData_t* shell_command_data;
     CeSubprocess_t* subprocess;
}RunShellCommandCleanup_t;

void run_shell_command_cleanup(void* data){
     RunShellCommandCleanup_t* cleanup = (RunShellCommandCleanup_t*)(data);

     *cleanup->shell_command_data->should_scroll = false;

     if(cleanup->shell_command_data){
          free(cleanup->shell_command_data->command);
          free(cleanup->shell_command_data);
     }

     if(cleanup->subprocess){
          // kill the subprocess and wait for it to be cleaned up
          ce_subprocess_kill(cleanup->subprocess, SIGKILL);
          ce_subprocess_close(cleanup->subprocess);
     }else{
     }
}

static void* run_shell_command_and_output_to_buffer(void* data){
     ShellCommandData_t* shell_command_data = (ShellCommandData_t*)(data);

     CeSubprocess_t subprocess;
     if(!ce_subprocess_open(&subprocess, shell_command_data->command)){
          ce_log("failed to run shell command '%s': '%s'", shell_command_data->command, strerror(errno));
          pthread_exit(NULL);
     }

     *shell_command_data->should_scroll = true;

     // we aren't using stdin here, so we should close it in case the
     // subprocess waits for stdin to close before it completes which is common
     // in filter applications
     ce_subprocess_close_stdin(&subprocess);

     RunShellCommandCleanup_t cleanup = {shell_command_data, &subprocess};
     int rc = 0;

     char bytes[BUFSIZ];
     snprintf(bytes, BUFSIZ, "pid %d started: '%s'\n\n", subprocess.pid, shell_command_data->command);
     ce_buffer_insert_string(shell_command_data->buffer, bytes, ce_buffer_end_point(shell_command_data->buffer));

     int stdout_fd = fileno(subprocess.stdout_file);
     int flags = fcntl(stdout_fd, F_GETFL, 0);
     fcntl(stdout_fd, F_SETFL, flags | O_NONBLOCK);

     while(true){
          if(g_shell_command_should_die){
               run_shell_command_cleanup(&cleanup);
               return NULL;
          }

          rc = read(stdout_fd, bytes, BUFSIZ);
          if(rc > 0){
               bytes[rc] = 0;

               // sanitize bytes for non-printable characters
               for(int i = 0; i < rc; i++){
                   if(bytes[i] < 32 && bytes[i] != '\n') bytes[i] = '?';
               }

               ce_buffer_insert_string(shell_command_data->buffer, bytes, ce_buffer_end_point(shell_command_data->buffer));
               do{
                    rc = write(g_shell_command_ready_fds[1], "1", 2);
               }while(rc == -1 && errno == EINTR);

               if(rc < 0){
                    ce_log("%s() write() to terminal ready fd failed: %s", __FUNCTION__, strerror(errno));
                    run_shell_command_cleanup(&cleanup);
                    return NULL;
               }
          }else if(rc < 0){
               if(errno == EAGAIN || errno == EWOULDBLOCK){
                    usleep(1000);
               }else if(errno == EBADF){
                    break;
               }
          }else{
               break;
          }
     }

     if(ferror(subprocess.stdout_file)){
          ce_log("shell command: fgets() from pid %d failed\n", subprocess.pid);
          run_shell_command_cleanup(&cleanup);
          return NULL;
     }

     int status = ce_subprocess_close(&subprocess);

     if(WIFEXITED(status)){
          snprintf(bytes, BUFSIZ, "\npid %d exited with code %d", subprocess.pid, WEXITSTATUS(status));
     }else if(WIFSIGNALED(status)){
          snprintf(bytes, BUFSIZ, "\npid %d killed by signal %d", subprocess.pid, WTERMSIG(status));
     }else if(WIFSTOPPED(status)){
          snprintf(bytes, BUFSIZ, "\npid %d stopped by signal %d", subprocess.pid, WSTOPSIG(status));
     }else{
          snprintf(bytes, BUFSIZ, "\npid %d stopped with unexpected status %d", subprocess.pid, status);
     }

     ce_buffer_insert_string(shell_command_data->buffer, bytes, ce_buffer_end_point(shell_command_data->buffer));
     shell_command_data->buffer->status = CE_BUFFER_STATUS_READONLY;
     do{
          rc = write(g_shell_command_ready_fds[1], "1", 2);
     }while(rc == -1 && errno == EINTR);

     if(rc < 0){
          ce_log("%s() write() to terminal ready fd failed: %s", __FUNCTION__, strerror(errno));
          run_shell_command_cleanup(&cleanup);
          return NULL;
     }

     run_shell_command_cleanup(&cleanup);
     return NULL;
}

bool ce_app_run_shell_command(CeApp_t* app, const char* command, CeLayout_t* tab_layout, CeView_t* view, bool relative){
     if(app->shell_command_thread){
          g_shell_command_should_die = true;
          pthread_join(app->shell_command_thread, NULL);
          g_shell_command_should_die = false;
     }

     ce_buffer_empty(app->shell_command_buffer);

     CeAppBufferData_t* buffer_data = app->shell_command_buffer->app_data;
     buffer_data->last_goto_destination = 0;
     app->last_goto_buffer = app->shell_command_buffer;

     char* base_directory = relative ? buffer_base_directory(view->buffer) : NULL;
     CeLayout_t* view_layout = ce_layout_buffer_in_view(tab_layout, app->shell_command_buffer);
     if(view_layout){
          view_layout->view.cursor = (CePoint_t){0, 0};
          view_layout->view.scroll = (CePoint_t){0, 0};
     }else{
          ce_view_switch_buffer(view, app->shell_command_buffer, &app->vim,
                                &app->config_options, true);
          view->cursor = (CePoint_t){0, 0};
          view->scroll = (CePoint_t){0, 0};
          free(buffer_data->base_directory);
          buffer_data->base_directory = NULL;
          if(base_directory){
               buffer_data->base_directory = strdup(base_directory);
          }
     }

     char updated_command[BUFSIZ];
     if(base_directory){
          snprintf(updated_command, BUFSIZ, "cd %s && %s", base_directory, command);
     }else{
          strncpy(updated_command, command, BUFSIZ);
     }

     ShellCommandData_t* shell_command_data = malloc(sizeof(*shell_command_data));
     shell_command_data->buffer = app->shell_command_buffer;
     shell_command_data->command = strdup(updated_command);
     shell_command_data->should_scroll = &app->shell_command_buffer_should_scroll;

     int rc = pthread_create(&app->shell_command_thread, NULL, run_shell_command_and_output_to_buffer, shell_command_data);
     if(rc != 0){
          ce_log("pthread_create() failed: '%s'\n", strerror(errno));
          return false;
     }

     return true;
}
