#include "ce_commands.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>

static bool get_layout_and_view(CeApp_t* app, CeView_t** view, CeLayout_t** tab_layout){ *tab_layout = app->tab_list_layout->tab_list.current;

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
     int64_t current_index = 0;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
          if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
               current_index = i;
               break;
          }
     }

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

     if(app->tab_list_layout->tab_list.tab_count > 1 && ce_layout_tab_get_layout_count(tab_layout) == 1){
          ce_layout_delete(app->tab_list_layout, tab_layout);
          if(current_index >= app->tab_list_layout->tab_list.tab_count){
               current_index = app->tab_list_layout->tab_list.tab_count - 1;
          }
          app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[current_index];
          tab_layout = app->tab_list_layout->tab_list.current;
     }else{
          ce_layout_delete(tab_layout, tab_layout->tab.current);
     }

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

          char* base_directory = buffer_base_directory(view->buffer, &app->terminal);
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

     app->highlight_search = true;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_noh(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     app->highlight_search = false;
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

CeCommandStatus_t command_new_terminal(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = NULL;

     if(!get_layout_and_view(app, &view, &tab_layout)) return CE_COMMAND_NO_ACTION;

     int64_t width = view->rect.right - view->rect.left;
     int64_t height = view->rect.bottom - view->rect.top;

     CeTerminal_t* terminal = ce_terminal_list_new_terminal(&app->terminal_list, width, height, app->config_options.terminal_scroll_back);
     ce_buffer_node_insert(&app->buffer_node_head, terminal->buffer);

     // TODO: compress with main.c's terminal_init stuff
     terminal->lines_buffer->app_data = calloc(1, sizeof(CeAppBufferData_t));
     terminal->lines_buffer->no_line_numbers = true;
     CeAppBufferData_t* buffer_data = terminal->lines_buffer->app_data;
     buffer_data->syntax_function = ce_syntax_highlight_terminal;
     terminal->lines_buffer->syntax_data = terminal;

     terminal->alternate_lines_buffer->app_data = calloc(1, sizeof(CeAppBufferData_t));
     terminal->alternate_lines_buffer->no_line_numbers = true;
     buffer_data = terminal->alternate_lines_buffer->app_data;
     buffer_data->syntax_function = ce_syntax_highlight_terminal;
     terminal->alternate_lines_buffer->syntax_data = terminal;

     ce_view_switch_buffer(view, terminal->buffer, &app->vim, &app->config_options);
     app->vim.mode = CE_VIM_MODE_INSERT;

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
                                       CeVim_t* vim, const char* base_directory, CeDestination_t* destination){
     char full_path[PATH_MAX];
     if(!base_directory) base_directory = ".";
     strncpy(full_path, base_directory, PATH_MAX);
     snprintf(full_path, PATH_MAX, "%s/%s", base_directory, destination->filepath);
     CeBuffer_t* load_buffer = load_file_into_view(buffer_node_head, view, config_options, vim, full_path);
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

     char* base_directory = buffer_base_directory(view->buffer, &app->terminal);
     CeBuffer_t* buffer = load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                                     base_directory, &destination);
     free(base_directory);
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
          if(destination.point.x < 0 || destination.point.y < 0) continue;

          char* base_directory = buffer_base_directory(buffer, &app->terminal);
          CeBuffer_t* loaded_buffer = load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                                                 base_directory, &destination);
          free(base_directory);
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
          if(destination.point.x >= 0 && destination.point.y >= 0){
               CeLayout_t* layout = ce_layout_buffer_in_view(tab_layout, buffer);
               if(layout) layout->view.scroll.y = save_destination;
               char* base_directory = buffer_base_directory(buffer, &app->terminal);
               load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                          base_directory, &destination);
               free(base_directory);
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
          if(destination.point.x < 0 || destination.point.y < 0) continue;

          char* base_directory = buffer_base_directory(buffer, &app->terminal);
          CeBuffer_t* loaded_buffer = load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                                                 base_directory, &destination);
          free(base_directory);
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
          if(destination.point.x >= 0 && destination.point.y >= 0){
               char* base_directory = buffer_base_directory(buffer, &app->terminal);
               load_destination_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, base_directory, &destination);
               free(base_directory);
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

#if 0
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
#endif

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
          terminal_layout->view.scroll.y = app->terminal.cursor.y + app->terminal.start_line;
          terminal_layout->view.scroll.x = 0;
     }

     update_terminal_last_goto_using_cursor(&app->terminal);
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

     update_terminal_last_goto_using_cursor(&app->terminal);
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
