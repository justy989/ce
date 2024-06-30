#include <stdlib.h>
#include <string.h>
#include <locale.h>
// WINDOWS: time
// #include <sys/time.h>
#include <sys/stat.h>
// WINDOWS: poll
// #include <sys/poll.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

#if defined(DISPLAY_TERMINAL)
    #include <ncurses.h>
#elif defined(DISPLAY_GUI)
    #include <SDL.h>
    #include <SDL_ttf.h>
#else
#error "No display mode specified"
#endif

#include "ce_app.h"
#include "ce_commands.h"
#include "ce_key_defines.h"

#if defined(DISPLAY_TERMINAL)
  #include "ce_draw_term.h"
#elif defined(DISPLAY_GUI)
  #include "ce_draw_gui.h"
#endif

#if !defined(PLATFORM_WINDOWS)
    #include <unistd.h>
#endif

#ifdef ENABLE_DEBUG_KEY_PRESS_INFO
int g_last_key = 0;
#endif

// limit to 60 fps
#define DRAW_USEC_LIMIT 16666

void handle_sigint(int signal){
     // pass
}

typedef struct{
     char* start;
     char* newline;
}FindTrailingResult_t;

static const char* keyname_str(int key) {
#if defined(DISPLAY_TERMINAL)
    return keyname(key);
#endif
    return "UNKNOWN";
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
          if(max_name_len < name_len) max_buffer_lines = name_len;
          buffer_count++;
          itr = itr->next;
     }

     int64_t max_buffer_lines_digits = ce_count_digits(max_buffer_lines);
     if(max_buffer_lines_digits < 5) max_buffer_lines_digits = 5; // account for "lines" string row header
     if(max_name_len < 11) max_name_len = 11; // account for "buffer name" string row header

     // build format string, OMG THIS IS SO UNREADABLE HOLY MOLY BATMAN
     char format_string[BUFSIZ];
     snprintf(format_string, BUFSIZ, "%%5s %%-%"PRId64"s %%%"PRId64 PRId64, max_name_len, max_buffer_lines_digits);

     // build buffer info
     itr = head;
     while(itr){
          const char* buffer_flag_str = ce_buffer_status_get_str(itr->buffer->status);
          // if the current buffer is the one we are putthing this list together on, set it to readonly for visual sake
          if(itr->buffer == buffer) buffer_flag_str = ce_buffer_status_get_str(CE_BUFFER_STATUS_READONLY);
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
          char reg = i + ' ';
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

          if(yank->type == CE_VIM_YANK_TYPE_BLOCK){
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

static void build_bind_list(CeBuffer_t* buffer, CeKeyBinds_t* key_binds){
     char line[256];
     ce_buffer_empty(buffer);
     for(int64_t i = 0; i < key_binds->count; i++){
          CeKeyBind_t* bind = key_binds->binds + i;
          int32_t printed = 0;

          // print keys
          for(int64_t k = 0; k < bind->key_count; k++){
               int key = bind->keys[k];
               printed += snprintf(line + printed, 256 - printed, "%s", keyname_str(key));
          }

          // print command with arguments
          printed += snprintf(line + printed, 256 - printed, " -> %s", bind->command.name);

          for(int64_t c = 0; c < bind->command.arg_count; c++){
               switch(bind->command.args[c].type){
               default:
                    break;
               case CE_COMMAND_ARG_INTEGER:
                    printed += snprintf(line + printed, 256 - printed, " %" PRId64, bind->command.args[c].integer);
                    break;
               case CE_COMMAND_ARG_DECIMAL:
                    printed += snprintf(line + printed, 256 - printed, " %f", bind->command.args[c].decimal);
                    break;
               case CE_COMMAND_ARG_STRING:
                    printed += snprintf(line + printed, 256 - printed, " %s", bind->command.args[c].string);
                    break;
               }
          }

          buffer_append_on_new_line(buffer, line);
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
          snprintf(line, 256, "'%c' %" PRId64 ", %" PRId64 "\n", reg, point->x, point->y);
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

// WINDOWS: time
// uint64_t time_between(struct timeval previous, struct timeval current){
//      return (current.tv_sec - previous.tv_sec) * 1000000LL +
//             (current.tv_usec - previous.tv_usec);
// }


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

bool handle_input_history_key(int key, CeHistory_t* history, CeBuffer_t* input_buffer, CePoint_t* cursor){
     if(key == KEY_UP_ARROW){
          char* prev = ce_history_previous(history);
          if(prev){
               ce_buffer_remove_string(input_buffer, (CePoint_t){0, 0}, ce_utf8_strlen(input_buffer->lines[0]));
               ce_buffer_insert_string(input_buffer, prev, (CePoint_t){0, 0});
          }
          cursor->x = ce_utf8_strlen(input_buffer->lines[0]);
          return true;
     }

     if(key == KEY_DOWN_ARROW){
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
     if(key == KEY_INVALID) return;

     if(key == KEY_RESIZE_EVENT){
#if defined(DISPLAY_TERMINAL)
          int terminal_width = 0;
          int terminal_height = 0;
          getmaxyx(stdscr, terminal_height, terminal_width);
          ce_app_update_terminal_view(app, terminal_width, terminal_height);
#endif
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
               if(app->record_macro && ce_macros_is_recording(&app->macros)){
                    ce_macros_end_recording(&app->macros);
                    app->record_macro = false;
                    return;
               }else if(!app->record_macro){
                    app->record_macro = true;
                    return;
               }
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

     // as long as vim isn't in the middle of handling keys, in insert mode vim returns VKH_HANDLED_KEY
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

                                   app->key_count = 0;
                                   app->vim.current_command[0] = 0;

                                   switch(cs){
                                   default:
                                        return;
                                   case CE_COMMAND_NO_ACTION:
                                        break;
                                   case CE_COMMAND_FAILURE:
                                        ce_log("'%s' failed\n", entry->name);
                                        return;
                                   case CE_COMMAND_PRINT_HELP:
                                        ce_app_message(app, "%s: %s\n", entry->name, entry->description);
                                        return;
                                   }
                              }else{
                                   ce_app_message(app, "unknown command: '%s'", command->name);
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
          if(key == KEY_CARRIAGE_RETURN) key = CE_NEWLINE;
          if(key == CE_NEWLINE && !app->input_complete_func && view->buffer == app->buffer_list_buffer){
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
          }else if(key == CE_NEWLINE && !app->input_complete_func && view->buffer == app->yank_list_buffer){
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
                    ce_app_input(app, "Edit Yank", edit_yank_input_complete_func);
                    ce_buffer_insert_string(app->input_view.buffer, selected_yank->text, (CePoint_t){0, 0});
                    app->input_view.cursor.y = app->input_view.buffer->line_count;
                    if(app->input_view.cursor.y) app->input_view.cursor.y--;
                    app->input_view.cursor.x = ce_utf8_strlen(app->input_view.buffer->lines[app->input_view.cursor.y]);
               }
          }else if(key == CE_NEWLINE && !app->input_complete_func && view->buffer == app->macro_list_buffer){
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
                    ce_app_input(app, "Edit Macro", edit_macro_input_complete_func);
                    ce_buffer_insert_string(app->input_view.buffer, macro_string, (CePoint_t){0, 0});
                    app->input_view.cursor.y = app->input_view.buffer->line_count;
                    if(app->input_view.cursor.y) app->input_view.cursor.y--;
                    app->input_view.cursor.x = ce_utf8_strlen(app->input_view.buffer->lines[app->input_view.cursor.y]);
                    free(macro_string);
               }
          }else if(key == CE_NEWLINE && app->input_complete_func){
               ce_app_apply_completion(app);
               app->vim.mode = CE_VIM_MODE_NORMAL;
               CeInputCompleteFunc* input_complete_func = app->input_complete_func;
               app->input_complete_func = NULL;
               if(app->input_view.buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                    input_complete_func(app, app->input_view.buffer);
               }
          }else if(key == app->config_options.apply_completion_key && ce_app_is_completing(app)){
               if(ce_app_apply_completion(app)){
                    // TODO: compress with other similar code elsewhere
                    if(app->input_complete_func == load_file_input_complete_func){
                         char* base_directory = buffer_base_directory(view->buffer);
                         complete_files(&app->input_complete, app->input_view.buffer->lines[0], base_directory);
                         free(base_directory);
                         build_complete_list(app->complete_list_buffer, &app->input_complete);
                    }else{
                         ce_complete_match(&app->input_complete, app->input_view.buffer->lines[0]);
                         build_complete_list(app->complete_list_buffer, &app->input_complete);
                    }

                    return;
               }
          }else if(key == app->config_options.cycle_next_completion_key){
               CeComplete_t* complete = ce_app_is_completing(app);
               if(app->vim.mode == CE_VIM_MODE_INSERT && complete){
                    ce_complete_next_match(complete);
                    build_complete_list(app->complete_list_buffer, complete);
                    return;
               }
          }else if(key == app->config_options.cycle_prev_completion_key){
               CeComplete_t* complete = ce_app_is_completing(app);
               if(app->vim.mode == CE_VIM_MODE_INSERT && complete){
                    ce_complete_previous_match(complete);
                    build_complete_list(app->complete_list_buffer, complete);
                    return;
               }
          }else if(key == KEY_ESCAPE && app->input_complete_func && app->vim.mode == CE_VIM_MODE_NORMAL){ // Escape
               ce_history_reset_current(&app->command_history);
               ce_history_reset_current(&app->search_history);
               app->input_complete_func = NULL;

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

               if(buffer_index == view->cursor.y){
                    if(itr->buffer == app->buffer_list_buffer ||
                       itr->buffer == app->yank_list_buffer ||
                       itr->buffer == app->complete_list_buffer ||
                       itr->buffer == app->macro_list_buffer ||
                       itr->buffer == app->mark_list_buffer ||
                       itr->buffer == app->jump_list_buffer ||
                       itr->buffer == app->shell_command_buffer ||
                       itr->buffer == g_ce_log_buffer ||
                       itr->buffer == app->message_view.buffer ||
                       itr->buffer == app->input_view.buffer){
                         ce_app_message(app, "cannot delete buffer '%s'", itr->buffer->name);
                    }else{
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
          }else if(app->input_complete_func){

               // TODO: how are we going to let this be supported through customization
               if(app->input_complete_func == command_input_complete_func && app->input_view.buffer->line_count){
                    handle_input_history_key(key, &app->command_history, app->input_view.buffer, &app->input_view.cursor);
               }else if(app->input_complete_func == search_input_complete_func && app->input_view.buffer->line_count){
                    handle_input_history_key(key, &app->search_history, app->input_view.buffer, &app->input_view.cursor);
               }

               CeAppBufferData_t* buffer_data = app->input_view.buffer->app_data;
               app->last_vim_handle_result = ce_vim_handle_key(&app->vim, &app->input_view, &app->input_view.cursor,
                                                               &app->visual, key, &buffer_data->vim, &app->config_options, true);

               if(app->vim.mode == CE_VIM_MODE_INSERT && app->input_view.buffer->line_count){
                    if(app->input_complete_func == load_file_input_complete_func){
                         char* base_directory = buffer_base_directory(view->buffer);
                         complete_files(&app->input_complete, app->input_view.buffer->lines[0], base_directory);
                         free(base_directory);
                         build_complete_list(app->complete_list_buffer, &app->input_complete);
                    }else{
                         ce_complete_match(&app->input_complete, app->input_view.buffer->lines[0]);
                         build_complete_list(app->complete_list_buffer, &app->input_complete);
                    }
               }
          }else{
               // TODO: how are we going to let this be supported through customization
               CeAppBufferData_t* buffer_data = view->buffer->app_data;

               app->last_vim_handle_result = ce_vim_handle_key(&app->vim, view, &view->cursor, &app->visual,
                                                               key, &buffer_data->vim, &app->config_options, true);

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
                         strncpy(destination.filepath, view->buffer->name, MAX_PATH_LEN);
                         ce_jump_list_insert(jump_list, destination);
                    }

                    if(app->vim.current_action.motion.function == ce_vim_motion_search_word_forward ||
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
     if(view && app->input_complete_func == search_input_complete_func){
          if(strcmp(app->input_view.buffer->name, "Search") == 0){
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
          }else if(strcmp(app->input_view.buffer->name, "Reverse Search") == 0){
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
          // WINDOWS: regex
          // }else if(strcmp(app->input_view.buffer->name, "Regex Search") == 0){
          //      if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
          //           regex_t regex = {};
          //           int rc = regcomp(&regex, app->input_view.buffer->lines[0], REG_EXTENDED);
          //           if(rc != 0){
          //                char error_buffer[BUFSIZ];
          //                regerror(rc, &regex, error_buffer, BUFSIZ);
          //                ce_log("regcomp() failed: '%s'", error_buffer);
          //           }else{
          //                CeRegexSearchResult_t result = ce_buffer_regex_search_forward(view->buffer, view->cursor, &regex);
          //                if(result.point.x >= 0){
          //                     scroll_to_and_center_if_offscreen(view, result.point, &app->config_options);
          //                }else{
          //                     view->cursor = app->search_start;
          //                }
          //           }
          //      }else{
          //           view->cursor = app->search_start;
          //      }
          // }else if(strcmp(app->input_view.buffer->name, "Regex Reverse Search") == 0){
          //      if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
          //           regex_t regex = {};
          //           int rc = regcomp(&regex, app->input_view.buffer->lines[0], REG_EXTENDED);
          //           if(rc != 0){
          //                char error_buffer[BUFSIZ];
          //                regerror(rc, &regex, error_buffer, BUFSIZ);
          //                ce_log("regcomp() failed: '%s'", error_buffer);
          //           }else{
          //                CeRegexSearchResult_t result = ce_buffer_regex_search_backward(view->buffer, view->cursor, &regex);
          //                if(result.point.x >= 0){
          //                     scroll_to_and_center_if_offscreen(view, result.point, &app->config_options);
          //                }else{
          //                     view->cursor = app->search_start;
          //                }
          //           }
          //      }else{
          //           view->cursor = app->search_start;
          //      }
          }
     }
}

void print_help(char* program){
     printf("usage  : %s [options] [file]\n", program);
     printf("options:\n");
     printf("  -c <config file> path to shared object configuration\n");
}

int main(int argc, char* argv[]){
     const char* config_filepath = NULL;
     int last_arg_index = 0;

     // setup signal handler
     signal(SIGINT, handle_sigint);

     // parse args
     for(int i = 1; i < argc; i++){
         char* arg = argv[i];
         if (arg[0] == '-') {
             if (strcmp(arg, "-c") == 0) {
                 if ((i + 1) >= argc) {
                     printf("error: missing config argument. See help.\n");
                     return 1;
                 }
                 config_filepath = argv[i + 1];
             } else if (strcmp(arg, "-h") == 0) {
                 print_help(argv[0]);
                 return 1;
             }
         } else {
             last_arg_index = i;
             break;
         }
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

     // WINDOWS: log
     // char ce_dir[MAX_PATH_LEN];
     // snprintf(ce_dir, MAX_PATH_LEN, "%s/.ce", getenv("HOME"));

     // struct stat st = {};
     // if(stat(ce_dir, &st) == -1){
     //      mode_t permissions = S_IRWXU | S_IRWXG;
     //      int rc = mkdir(ce_dir, permissions);
     //      if(rc != 0){
     //           fprintf(stderr, "mkdir('%s', %d) failed: '%s'\n", ce_dir, permissions, strerror(errno));
     //           return 1;
     //      }
     // }

     // char log_filepath[MAX_PATH_LEN];
     // snprintf(log_filepath, MAX_PATH_LEN, "%s/ce.log", ce_dir);
     // if(!ce_log_init(log_filepath)){
     //      return 1;
     // }

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
          config_options->vertical_scroll_off = 0;
          config_options->insert_spaces_on_tab = true;
          config_options->line_number = CE_LINE_NUMBER_NONE;
          config_options->completion_line_limit = 15;
          config_options->message_display_time_usec = 5000000; // 5 seconds
          config_options->apply_completion_key = CE_TAB;
          config_options->cycle_next_completion_key = ce_ctrl_key('n');
          config_options->cycle_prev_completion_key = ce_ctrl_key('p');
          config_options->show_line_extends_passed_view_as = '>';

          config_options->color_defs[COLOR_BLACK].red = 32;
          config_options->color_defs[COLOR_BLACK].green = 32;
          config_options->color_defs[COLOR_BLACK].blue = 32;

          config_options->color_defs[COLOR_RED].red = 137;
          config_options->color_defs[COLOR_RED].green = 56;
          config_options->color_defs[COLOR_RED].blue = 56;

          config_options->color_defs[COLOR_GREEN].red = 69;
          config_options->color_defs[COLOR_GREEN].green = 123;
          config_options->color_defs[COLOR_GREEN].blue = 77;

          config_options->color_defs[COLOR_YELLOW].red = 150;
          config_options->color_defs[COLOR_YELLOW].green = 111;
          config_options->color_defs[COLOR_YELLOW].blue = 78;

          config_options->color_defs[COLOR_BLUE].red = 70;
          config_options->color_defs[COLOR_BLUE].green = 107;
          config_options->color_defs[COLOR_BLUE].blue = 138;

          config_options->color_defs[COLOR_MAGENTA].red = 116;
          config_options->color_defs[COLOR_MAGENTA].green = 90;
          config_options->color_defs[COLOR_MAGENTA].blue = 160;

          config_options->color_defs[COLOR_CYAN].red = 55;
          config_options->color_defs[COLOR_CYAN].green = 125;
          config_options->color_defs[COLOR_CYAN].blue = 108;

          config_options->color_defs[COLOR_WHITE].red = 42;
          config_options->color_defs[COLOR_WHITE].green = 42;
          config_options->color_defs[COLOR_WHITE].blue = 42;

          config_options->color_defs[COLOR_BRIGHT_BLACK].red = 36;
          config_options->color_defs[COLOR_BRIGHT_BLACK].green = 36;
          config_options->color_defs[COLOR_BRIGHT_BLACK].blue = 36;

          config_options->color_defs[COLOR_BRIGHT_RED].red = 157;
          config_options->color_defs[COLOR_BRIGHT_RED].green = 110;
          config_options->color_defs[COLOR_BRIGHT_RED].blue = 127;

          config_options->color_defs[COLOR_BRIGHT_GREEN].red = 110;
          config_options->color_defs[COLOR_BRIGHT_GREEN].green = 137;
          config_options->color_defs[COLOR_BRIGHT_GREEN].blue = 106;

          config_options->color_defs[COLOR_BRIGHT_YELLOW].red = 156;
          config_options->color_defs[COLOR_BRIGHT_YELLOW].green = 148;
          config_options->color_defs[COLOR_BRIGHT_YELLOW].blue = 95;

          config_options->color_defs[COLOR_BRIGHT_BLUE].red = 114;
          config_options->color_defs[COLOR_BRIGHT_BLUE].green = 151;
          config_options->color_defs[COLOR_BRIGHT_BLUE].blue = 179;

          config_options->color_defs[COLOR_BRIGHT_MAGENTA].red = 147;
          config_options->color_defs[COLOR_BRIGHT_MAGENTA].green = 108;
          config_options->color_defs[COLOR_BRIGHT_MAGENTA].blue = 151;

          config_options->color_defs[COLOR_BRIGHT_CYAN].red = 124;
          config_options->color_defs[COLOR_BRIGHT_CYAN].green = 166;
          config_options->color_defs[COLOR_BRIGHT_CYAN].blue = 145;

          config_options->color_defs[COLOR_BRIGHT_WHITE].red = 255;
          config_options->color_defs[COLOR_BRIGHT_WHITE].green = 255;
          config_options->color_defs[COLOR_BRIGHT_WHITE].blue = 255;

          config_options->color_defs[COLOR_FOREGROUND].red = 218;
          config_options->color_defs[COLOR_FOREGROUND].green = 218;
          config_options->color_defs[COLOR_FOREGROUND].blue = 218;

          config_options->color_defs[COLOR_BACKGROUND].red = 25;
          config_options->color_defs[COLOR_BACKGROUND].green = 25;
          config_options->color_defs[COLOR_BACKGROUND].blue = 25;

          // GUI options
          config_options->gui_window_width = 1024;
          config_options->gui_window_height = 768;
          config_options->gui_font_size = 16;
          config_options->gui_font_line_separation = 1;
          strncpy(config_options->gui_font_path, "Inconsolata-SemiBold.ttf", MAX_PATH_LEN);

          // keybinds
          CeKeyBindDef_t normal_mode_bind_defs[] = {
               {{'\\', 'q'},             "quit"},
               {{ce_ctrl_key('w'), 'h'}, "select_adjacent_layout left"},
               {{ce_ctrl_key('w'), 'l'}, "select_adjacent_layout right"},
               {{ce_ctrl_key('w'), 'k'}, "select_adjacent_layout up"},
               {{ce_ctrl_key('w'), 'j'}, "select_adjacent_layout down"},
               {{ce_ctrl_key('s')},      "save_buffer"},
               {{'\\', 'b'},             "show_buffers"},
               {{ce_ctrl_key('f')},      "load_file"},
               {{'/'},                   "search forward"},
               {{'?'},                   "search backward"},
               {{':'},                   "command"},
               {{'g', 't'},              "select_adjacent_tab right"},
               {{'g', 'T'},              "select_adjacent_tab left"},
               {{'\\', '/'},             "regex_search forward"},
               {{'\\', '?'},             "regex_search backward"},
               {{'g', 'r'},              "redraw"},
               {{'\\', 'f'},             "reload_file"},
               {{ce_ctrl_key('b')},      "switch_buffer"},
               {{ce_ctrl_key('o')},      "jump_list previous"},
               {{ce_ctrl_key('i')},      "jump_list next"},
               {{'K'},                   "man_page_on_word_under_cursor"},
               {{'Z', 'Z'},              "wq"},
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
               syntax_defs[CE_SYNTAX_COLOR_COMPLETE_SELECTED].fg = COLOR_BLACK;
               syntax_defs[CE_SYNTAX_COLOR_COMPLETE_SELECTED].bg = COLOR_YELLOW;
               syntax_defs[CE_SYNTAX_COLOR_COMPLETE_MATCH].fg = COLOR_BRIGHT_BLUE;
               syntax_defs[CE_SYNTAX_COLOR_COMPLETE_MATCH].bg = CE_SYNTAX_USE_CURRENT_COLOR;
               syntax_defs[CE_SYNTAX_COLOR_LINE_NUMBER].fg = COLOR_DEFAULT;
               syntax_defs[CE_SYNTAX_COLOR_LINE_NUMBER].bg = COLOR_DEFAULT;
               syntax_defs[CE_SYNTAX_COLOR_MULTIPLE_CURSOR_INACTIVE].fg = COLOR_DEFAULT;
               syntax_defs[CE_SYNTAX_COLOR_MULTIPLE_CURSOR_INACTIVE].bg = COLOR_RED;
               syntax_defs[CE_SYNTAX_COLOR_MULTIPLE_CURSOR_ACTIVE].fg = COLOR_DEFAULT;
               syntax_defs[CE_SYNTAX_COLOR_MULTIPLE_CURSOR_ACTIVE].bg = COLOR_GREEN;
               syntax_defs[CE_SYNTAX_COLOR_LINE_EXTENDS_PASSED_VIEW].fg = COLOR_YELLOW;
               syntax_defs[CE_SYNTAX_COLOR_LINE_EXTENDS_PASSED_VIEW].bg = CE_SYNTAX_USE_CURRENT_COLOR;

               app.config_options.ui_fg_color = COLOR_BRIGHT_WHITE;
               app.config_options.ui_bg_color = COLOR_BLACK;
               app.config_options.message_fg_color = COLOR_BLUE;
               app.config_options.message_bg_color = COLOR_WHITE;

               app.syntax_defs = syntax_defs;
          }
     }

     // init buffers
     {
          app.buffer_list_buffer = new_buffer();
          app.bind_list_buffer = new_buffer();
          app.yank_list_buffer = new_buffer();
          app.complete_list_buffer = new_buffer();
          app.macro_list_buffer = new_buffer();
          app.mark_list_buffer = new_buffer();
          app.jump_list_buffer = new_buffer();
          app.shell_command_buffer = new_buffer();
          CeBuffer_t* scratch_buffer = new_buffer();

          ce_buffer_alloc(app.buffer_list_buffer, 1, "[buffers]");
          ce_buffer_node_insert(&app.buffer_node_head, app.buffer_list_buffer);
          ce_buffer_alloc(app.bind_list_buffer, 1, "[binds]");
          ce_buffer_node_insert(&app.buffer_node_head, app.bind_list_buffer);
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
          ce_buffer_alloc(app.shell_command_buffer, 1, "[shell command]");
          ce_buffer_node_insert(&app.buffer_node_head, app.shell_command_buffer);
          ce_buffer_alloc(scratch_buffer, 1, "scratch");
          ce_buffer_node_insert(&app.buffer_node_head, scratch_buffer);

          app.buffer_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.bind_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.yank_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.complete_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.macro_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.mark_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.jump_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.shell_command_buffer->status = CE_BUFFER_STATUS_NONE;
          scratch_buffer->status = CE_BUFFER_STATUS_NONE;

          app.buffer_list_buffer->no_line_numbers = true;
          app.bind_list_buffer->no_line_numbers = true;
          app.yank_list_buffer->no_line_numbers = true;
          app.complete_list_buffer->no_line_numbers = true;
          app.macro_list_buffer->no_line_numbers = true;
          app.mark_list_buffer->no_line_numbers = true;
          app.jump_list_buffer->no_line_numbers = true;
          app.shell_command_buffer->no_line_numbers = true;

          app.complete_list_buffer->no_highlight_current_line = true;

          CeAppBufferData_t* buffer_data = app.complete_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_completions;

          buffer_data = app.buffer_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.bind_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.yank_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.macro_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.mark_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.jump_list_buffer->app_data;
          buffer_data->syntax_function = ce_syntax_highlight_c;
          buffer_data = app.shell_command_buffer->app_data;
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

         app.cached_filepath_count = 0;
         app.shell_command_buffer_should_scroll = false;
         app.shell_command_thread_should_die = false;
     }

 #if defined(DISPLAY_TERMINAL)
     // init ncurses
     {
          initscr();
          noqiflush();
          keypad(stdscr, TRUE);
          cbreak();
          noecho();
          raw();
          set_escdelay(0);

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
          define_key("\x7F", KEY_BACKSPACE); // Backspace  (127) (0x7F) ASCII "DEL" Delete
     }
#elif defined(DISPLAY_GUI)
     CeGui_t gui;
     memset(&gui, 0, sizeof(gui));

     {
          int rc = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
          if(rc < 0){
              printf("SDL_Init() failed: %s\n", SDL_GetError());
              return 1;
          }

          SDL_StartTextInput();

          gui.application_name = "ce";
          gui.window_width = 1920;
          gui.window_height = 1080;
          printf("Create window: %s %d, %d\n", gui.application_name, gui.window_width, gui.window_height);

          gui.window = SDL_CreateWindow(gui.application_name,
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
                                        gui.window_width,
                                        gui.window_height,
                                        SDL_WINDOW_RESIZABLE);
          if(gui.window == NULL){
              printf("SDL_CreateWindow() failed: %s\n", SDL_GetError());
              return 1;
          }

          gui.window_surface = SDL_GetWindowSurface(gui.window);

          rc = TTF_Init();
          if (rc < 0) {
              printf("TTF_Init() failed: %s\n", TTF_GetError());
              return 1;
          }

          if (gui_load_font(&gui,
                            app.config_options.gui_font_path,
                            app.config_options.gui_font_size,
                            app.config_options.gui_font_line_separation) != 0) {
              return 1;
          }

          TTF_SetFontHinting(gui.font, TTF_HINTING_MONO);
          app.gui = &gui;
     }
#endif

     ce_app_init_default_commands(&app);
     ce_vim_init(&app.vim);

     // init layout
     {
          CeRect_t rect = {};
          CeLayout_t* tab_layout = ce_layout_tab_init(app.buffer_node_head->buffer, rect);
          app.tab_list_layout = ce_layout_tab_list_init(tab_layout);
#if defined(DISPLAY_TERMINAL)
          int terminal_width = 0;
          int terminal_height = 0;
          getmaxyx(stdscr, terminal_height, terminal_width);
          ce_app_update_terminal_view(&app, terminal_width, terminal_height);
#elif defined(DISPLAY_GUI)
          int calculated_terminal_width = gui.window_width / (gui.font_point_size / 2);
          int calculated_terminal_height = gui.window_height / (gui.font_point_size + gui.font_line_separation);
          ce_app_update_terminal_view(&app, calculated_terminal_width, calculated_terminal_height);
#endif
     }

     // setup input buffer
     {
          CeBuffer_t* buffer = new_buffer();
          ce_buffer_alloc(buffer, 1, "input");
          app.input_view.buffer = buffer;
          ce_buffer_node_insert(&app.buffer_node_head, buffer);
     }

     // setup message buffer
     {
          CeBuffer_t* buffer = new_buffer();
          ce_buffer_alloc(buffer, 1, "[message]");
          app.message_view.buffer = buffer;
          app.message_view.buffer->no_line_numbers = true;
          ce_buffer_node_insert(&app.buffer_node_head, buffer);
          app.message_view.buffer->status = CE_BUFFER_STATUS_READONLY;
     }

#if !defined(PLATFORM_WINDOWS)
     pipe(g_shell_command_ready_fds);
#endif

 #if defined(DISPLAY_TERMINAL)
     ce_draw_term(&app);
 #elif defined(DISPLAY_GUI)
     ce_draw_gui(&app, &gui);
 #endif

     // init draw thread
     // WINDOWS: time
     // struct timeval current_draw_time = {};
     uint64_t time_since_last_message = 0;

     // main loop
     while(!app.quit){
 #if defined(DISPLAY_TERMINAL)
          // TODO: add shell command buffer
          int input_fd_count = 2; // stdin and terminal_ready_fd
          struct pollfd input_fds[input_fd_count];

          // populate fd array
          {
               memset(input_fds, 0, input_fd_count * sizeof(*input_fds));

               input_fds[0].fd = STDIN_FILENO;
               input_fds[0].events = POLLIN;
               input_fds[1].fd = g_shell_command_ready_fds[0];
               input_fds[1].events = POLLIN;
          }

          int poll_rc = poll(input_fds, input_fd_count, 10);
          switch(poll_rc){
          default:
               break;
          case -1:
               assert(errno == EINTR);
          case 0:
               continue;
          }

          bool check_stdin = false;

          if(poll_rc < 0 || input_fds[0].revents != 0){
               check_stdin = true;
          }

          if(input_fds[1].revents != 0){
               char buffer[BUFSIZ];
               int rc;
               do{
                    rc = read(g_shell_command_ready_fds[0], buffer, BUFSIZ);
               }while(rc == -1 && errno == EINTR);
               if(rc < 0){
                    ce_log("failed to read from shell command ready fd: '%s'\n", strerror(errno));
                    errno = 0;
                    continue;
               }
          }
#endif

          if(app.message_mode){
               // WINDOWS: time
               // gettimeofday(&current_draw_time, NULL);
               // time_since_last_message = time_between(app.message_time, current_draw_time);
               // if(time_since_last_message > app.config_options.message_display_time_usec){
               //      app.message_mode = false;
               // }
          }

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

#if defined(DISPLAY_TERMINAL)
          int key = KEY_INVALID;
          if(check_stdin) { key = getch(); }
#elif defined(DISPLAY_GUI)
          bool check_stdin = false;
          bool window_resized = false;
          int64_t key_len = 0;
          int key = KEY_INVALID;
          int keydown_key = KEY_INVALID;
          SDL_Event event;
          memset(&event, 0, sizeof(event));
          while(SDL_PollEvent(&event)){
               switch(event.type){
               case SDL_QUIT:
                    app.quit = true;
                    break;
               case SDL_KEYDOWN:
#if defined(PLATFORM_WINDOWS)
                    if (__isascii(event.key.keysym.sym)) {
#else
                    if (isascii(event.key.keysym.sym)) {
#endif
                        keydown_key = event.key.keysym.sym;
                        if (event.key.keysym.mod & KMOD_SHIFT) {
                            keydown_key = toupper(keydown_key);
                        }
                        if (event.key.keysym.mod & KMOD_CTRL) {
                            keydown_key = ce_ctrl_key(keydown_key);
                        }
                    } else {
                        switch (event.key.keysym.scancode) {
                        default:
                            break;
                        case SDL_SCANCODE_LEFT:
                            keydown_key = KEY_LEFT_ARROW;
                            break;
                        case SDL_SCANCODE_RIGHT:
                            keydown_key = KEY_RIGHT_ARROW;
                            break;
                        case SDL_SCANCODE_UP:
                            keydown_key = KEY_UP_ARROW;
                            break;
                        case SDL_SCANCODE_DOWN:
                            keydown_key = KEY_DOWN_ARROW;
                            break;
                        }
                    }
                    break;
               case SDL_TEXTINPUT:
                    {
                        int decoded_key = ce_utf8_decode(event.text.text, &key_len);
                        if (isascii(decoded_key)) {
                            key = decoded_key;
                        }
                    }
                    break;
               case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                         window_resized = true;
                    }
                    break;
               }
          }

          if(window_resized){
              SDL_GetWindowSize(gui.window, &gui.window_width, &gui.window_height);
              printf("window resized to: %d, %d\n", gui.window_width, gui.window_height);
              gui.window_surface = SDL_GetWindowSurface(gui.window);
              int calculated_terminal_width = gui.window_width / (gui.font_point_size / 2);
              int calculated_terminal_height = gui.window_height / (gui.font_point_size + gui.font_line_separation);
              ce_app_update_terminal_view(&app, calculated_terminal_width, calculated_terminal_height);
          }

          if (key == KEY_INVALID && keydown_key != KEY_INVALID) {
              key = keydown_key;
          }
#endif

#ifdef ENABLE_DEBUG_KEY_PRESS_INFO
          if(check_stdin) g_last_key = key;
          if(app.log_key_presses) ce_log("key: %s %d\n", keyname_str(key), key);
#endif

          if (key != KEY_INVALID) {
              app.message_mode = false;
              // handle input from the user
              app_handle_key(&app, view, key);
          }

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
               if(view->buffer == app.shell_command_buffer){
                    ce_view_follow_cursor(view, app.config_options.horizontal_scroll_off, 1, app.config_options.tab_width);
               }else{
                    ce_view_follow_cursor(view, app.config_options.horizontal_scroll_off, app.config_options.vertical_scroll_off,
                                          app.config_options.tab_width);
               }

               // setup input view overlay if we are
               if(app.input_complete_func) input_view_overlay(&app.input_view, view);
          }

          // update any list buffers if they are in view
          if(ce_layout_buffer_in_view(tab_layout, app.buffer_list_buffer)){
               build_buffer_list(app.buffer_list_buffer, app.buffer_node_head);
          }

          if(ce_layout_buffer_in_view(tab_layout, app.bind_list_buffer)){
               build_bind_list(app.bind_list_buffer, &app.key_binds);
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

          if(view){
               CeLayout_t* shell_command_layout = ce_layout_buffer_in_view(tab_layout, app.shell_command_buffer);
               if(shell_command_layout){
                    CePoint_t end_of_buffer = ce_buffer_end_point(app.shell_command_buffer);
                    if(&shell_command_layout->view == view){
                         app.shell_command_buffer_should_scroll = (view->cursor.y == end_of_buffer.y);
                    }

                    if(app.shell_command_buffer_should_scroll){
                         shell_command_layout->view.cursor = end_of_buffer;
                         shell_command_layout->view.cursor.x = 0;
                         ce_view_follow_cursor(&shell_command_layout->view, 1, 1, app.config_options.tab_width);
                    }
               }
          }

 #if defined(DISPLAY_TERMINAL)
          ce_draw_term(&app);
 #elif defined(DISPLAY_GUI)
          ce_draw_gui(&app, &gui);
          SDL_Delay(0);
 #endif
     }

     // cleanup
     if(config_filepath){
          app.user_config.free_func(&app);
          user_config_free(&app.user_config);
     }

     ce_macros_free(&app.macros);
     ce_complete_free(&app.input_complete);

     CeKeyBinds_t* binds = &app.key_binds;
     for(int64_t i = 0; i < binds->count; ++i){
          ce_command_free(&binds->binds[i].command);
          if(!binds->binds[i].key_count) continue;
          free(binds->binds[i].keys);
     }
     free(binds->binds);

     free(app.command_entries);

     ce_layout_free(&app.tab_list_layout);
     ce_vim_free(&app.vim);
     ce_history_free(&app.command_history);
     ce_history_free(&app.search_history);

     ce_app_clear_filepath_cache(&app);

     ce_buffer_node_free(&app.buffer_node_head);

#if defined(DISPLAY_TERMINAL)
     endwin();
#elif defined(DISPLAY_GUI)
    TTF_CloseFont(gui.font);
    TTF_Quit();
    SDL_DestroyWindow(gui.window);
    SDL_Quit();
#endif
     return 0;
}
