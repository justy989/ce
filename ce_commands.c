#include "ce_commands.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <errno.h>
#include <sys/stat.h>

typedef struct{
     CeLayout_t* tab_layout;
     CeView_t* view;
}CommandContext_t;

static bool get_command_context(CeApp_t* app, CommandContext_t* command_context){
     command_context->tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_complete_func) return false;

     if(command_context->tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          command_context->view = &command_context->tab_layout->tab.current->view;
          return true;
     }

     return true;
}

static bool try_save_buffer(CeApp_t* app, CeBuffer_t* buffer){
     struct stat statbuf;
     if(stat(buffer->name, &statbuf) == 0){
          if(statbuf.st_mtime > buffer->file_modified_time){
               ce_app_input(app, BUFFER_MODIFIED_OUTSIDE_EDITOR, buffer_modified_outside_editor_complete_func);
               return false;
          }
     }

     ce_buffer_save(buffer);
     return true;
}

CeCommandStatus_t command_add_cursor(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     ce_multiple_cursors_add(&app->multiple_cursors, command_context.view->cursor);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_clear_cursors(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     ce_multiple_cursors_clear(&app->multiple_cursors);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_toggle_cursors_active(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     ce_multiple_cursors_toggle_active(&app->multiple_cursors);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_blank(CeCommand_t* command, void* user_data){
     (void)(command);
     (void)(user_data);
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_quit(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

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
          ce_view_switch_buffer(command_context.view, app->buffer_list_buffer, &app->vim, &app->multiple_cursors,
                                &app->config_options, true);
          ce_app_input(app, UNSAVED_BUFFERS_DIALOGUE, unsaved_buffers_input_complete_func);
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
          app->input_complete_func = NULL;
          ce_multiple_cursors_clear(&app->multiple_cursors);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_save_buffer(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     try_save_buffer(app, command_context.view->buffer);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_save_all_and_quit(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     CeBufferNode_t* itr = app->buffer_node_head;
     while(itr){
          if(itr->buffer->status == CE_BUFFER_STATUS_MODIFIED){
               if(!try_save_buffer(app, itr->buffer)){
                    return CE_COMMAND_SUCCESS;
               }
          }
          itr = itr->next;
     }

     return command_quit(command, user_data);
}

CeCommandStatus_t command_show_info_buffer(CeCommand_t* command, void* user_data, CeBuffer_t* buffer){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CommandContext_t command_context = {};
     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;
     ce_view_switch_buffer(command_context.view, buffer, &app->vim, &app->multiple_cursors, &app->config_options, true);
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_show_buffers(CeCommand_t* command, void* user_data){
     CeApp_t* app = user_data;
     return command_show_info_buffer(command, user_data, app->buffer_list_buffer);
}

CeCommandStatus_t command_show_yanks(CeCommand_t* command, void* user_data){
     CeApp_t* app = user_data;
     return command_show_info_buffer(command, user_data, app->yank_list_buffer);
}

CeCommandStatus_t command_show_macros(CeCommand_t* command, void* user_data){
     CeApp_t* app = user_data;
     return command_show_info_buffer(command, user_data, app->macro_list_buffer);
}

CeCommandStatus_t command_show_marks(CeCommand_t* command, void* user_data){
     CeApp_t* app = user_data;
     return command_show_info_buffer(command, user_data, app->mark_list_buffer);
}

CeCommandStatus_t command_show_jumps(CeCommand_t* command, void* user_data){
     CeApp_t* app = user_data;
     return command_show_info_buffer(command, user_data, app->jump_list_buffer);
}

CeLayout_t* split_layout(CeApp_t* app, bool vertical){
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     CeLayout_t* new_layout = ce_layout_split(tab_layout, vertical);

     if(new_layout){
          ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);
          ce_view_follow_cursor(&new_layout->view, app->config_options.horizontal_scroll_off,
                                app->config_options.vertical_scroll_off, app->config_options.tab_width);
          tab_layout->tab.current = new_layout;
     }

     return new_layout;
}

CeCommandStatus_t command_split_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     bool vertical = false;

     if(strcmp(command->args[0].string, "vertical") == 0){
          vertical = true;
     }else if(strcmp(command->args[0].string, "horizontal") == 0){
          // pass
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     split_layout(app, vertical);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_select_parent_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     CeLayout_t* layout = ce_layout_find_parent(tab_layout, tab_layout->tab.current);
     if(layout){
          tab_layout->tab.current = layout;
     }else{
          ce_app_message(app, "current layout has no parent");
     }

     return CE_COMMAND_SUCCESS;
}

static bool delete_layout(CeApp_t* app){
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
          return false;
     }

     if(app->input_complete_func) return false;

     if(!get_view_info_from_tab(tab_layout, &view, &view_rect)){
          return false;
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

     ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);
     CeLayout_t* layout = ce_layout_find_at(tab_layout, cursor);
     if(layout) tab_layout->tab.current = layout;

     return true;
}

CeCommandStatus_t command_delete_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     if(!delete_layout(user_data)) return CE_COMMAND_FAILURE;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_load_file(CeCommand_t* command, void* user_data){
     if(command->arg_count < 0 || command->arg_count > 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(command->arg_count == 1){
          if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
          if(!load_file_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                                  &app->multiple_cursors, true, command->args[0].string)){
               ce_app_message(app, "failed to load file %s: '%s'", command->args[0].string, strerror(errno));
          }
     }else{ // it's 0
          ce_app_input(app, "Load File", load_file_input_complete_func);

          char* base_directory = buffer_base_directory(command_context.view->buffer);
          complete_files(&app->input_complete, app->input_view.buffer->lines[0], base_directory);
          free(base_directory);
          build_complete_list(app->complete_list_buffer, &app->input_complete);
     }

     return CE_COMMAND_SUCCESS;
}

typedef struct CeStrNode_t{
    char* string;
    struct CeStrNode_t* next;
}CeStrNode_t;

CeStrNode_t* find_files_in_directory_recursively(const char* directory, CeStrNode_t* node, char** ignore_dirs,
                                                 int* ignore_dir_lens, uint64_t ignore_dir_count){
     char full_path[PATH_MAX];
     struct stat info;
     struct dirent *dir_node;

     DIR* os_dir = opendir(directory);
     if(!os_dir) return node;

     while((dir_node = readdir(os_dir)) != NULL){
          if(strcmp(dir_node->d_name, ".") == 0 || strcmp(dir_node->d_name, "..") == 0) continue;
          int full_path_len = snprintf(full_path, PATH_MAX, "%s/%s", directory, dir_node->d_name);
          stat(full_path, &info);
          if(S_ISDIR(info.st_mode) && access(full_path, W_OK) == 0){
               bool ignore_dir = false;
               for(uint64_t i = 0; i < ignore_dir_count; i++){
                    if(full_path_len >= ignore_dir_lens[i] && strcmp(full_path + full_path_len - ignore_dir_lens[i], ignore_dirs[i]) == 0){
                        ignore_dir = true;
                        break;
                    }
               }
               if(ignore_dir) continue;

               node = find_files_in_directory_recursively(full_path, node, ignore_dirs, ignore_dir_lens, ignore_dir_count);
          }else{
               CeStrNode_t* new_node = malloc(sizeof(*new_node));
               new_node->string = strdup(full_path);
               new_node->next = NULL;
               node->next = new_node;
               node = new_node;
          }
     }

     closedir(os_dir);
     return node;
}

CeCommandStatus_t command_load_project_file(CeCommand_t* command, void* user_data){
    CeApp_t* app = user_data;
    CommandContext_t command_context = {};

    if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

    char* base_directory = buffer_base_directory(command_context.view->buffer);
    if(!base_directory){
         // if the base_directory is NULL, it's the directory where ce was opened, so fill it out with CWD
         base_directory = malloc(PATH_MAX);
         getcwd(base_directory, PATH_MAX);
    }

    // search up the tree for a .git folder
    char project_marker_path[PATH_MAX + 1];
    while(strlen(base_directory) > 0){
         // TODO: support other version control besides git
         snprintf(project_marker_path, PATH_MAX, "%s/.git", base_directory);

         struct stat statbuf;
         if(stat(project_marker_path, &statbuf) == 0){
              break;
         }

         char* last_slash = strrchr(base_directory, '/');
         if(last_slash){
              // truncate directory
              *last_slash = 0;
         }else{
              free(base_directory);
              return CE_COMMAND_NO_ACTION;
         }
    }

    // setup our ignore dirs
    char** ignore_dirs = malloc(sizeof(*ignore_dirs) * command->arg_count);
    for(int64_t i = 0; i < command->arg_count; i++){
        // TODO: this imposes a restriction that folders cannot be named numbers like 5
        if(command->args[i].type != CE_COMMAND_ARG_STRING){
            free(base_directory);
            return CE_COMMAND_PRINT_HELP;
        }
        ignore_dirs[i] = strdup(command->args[i].string);
    }

    int* ignore_dir_lens = malloc(sizeof(*ignore_dir_lens) * command->arg_count);
    for(int64_t i = 0; i < command->arg_count; i++){
        ignore_dir_lens[i] = strlen(ignore_dirs[i]);
    }

    // head is a dummy node that doesn't contain any info, just makes the find_files_in_directory_recursively()
    // interface better
    CeStrNode_t* head = calloc(sizeof(*head), 1);
    find_files_in_directory_recursively(base_directory, head, ignore_dirs, ignore_dir_lens, command->arg_count);
    free(base_directory);

    // cleanup our ignore dirs
    for(int64_t i = 0; i < command->arg_count; i++){
        free(ignore_dirs[i]);
    }
    free(ignore_dirs);
    free(ignore_dir_lens);

    // convert linked list of strings to array of strings
    CeStrNode_t* save_head = head;

    uint64_t file_count = 0;
    while(head){
        if(head->string) file_count++;
        head = head->next;
    }

    char** files = malloc(sizeof(*files) * file_count);

    head = save_head;

    uint64_t file_index = 0;
    while(head){
        if(head->string){
            files[file_index] = strdup(head->string);
            file_index++;
        }
        CeStrNode_t* node = head;
        head = head->next;
        free(node->string);
        free(node);
    }

    // setup input and completion
    ce_app_input(app, "Load Project File", load_project_file_input_complete_func);
    ce_complete_init(&app->input_complete, (const char**)(files), NULL, file_count);
    build_complete_list(app->complete_list_buffer, &app->input_complete);

    for(uint64_t i = 0; i < file_count; i++){
        free(files[i]);
    }
    free(files);

    return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_new_tab(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     CeLayout_t* new_tab_layout = ce_layout_tab_list_add(app->tab_list_layout);
     if(!new_tab_layout) return CE_COMMAND_NO_ACTION;
     app->tab_list_layout->tab_list.current = new_tab_layout;
     ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);

     return CE_COMMAND_SUCCESS;
}

bool select_tab_left(CeApp_t* app){
     for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
          if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
               if(i > 0){
                    app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i - 1];
                    ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);
                    return true;
               }else{
                    // wrap around
                    app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[app->tab_list_layout->tab_list.tab_count - 1];
                    ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);
                    return true;
               }
               break;
          }
     }

     ce_multiple_cursors_clear(&app->multiple_cursors);

     return false;
}

bool select_tab_right(CeApp_t* app){
     for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
          if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
               if(i < (app->tab_list_layout->tab_list.tab_count - 1)){
                    app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i + 1];
                    ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);
                    return true;
               }else{
                    // wrap around
                    app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[0];
                    ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);
                    return true;
               }
               break;
          }
     }

     ce_multiple_cursors_clear(&app->multiple_cursors);

     return false;
}

CeCommandStatus_t command_select_adjacent_tab(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     if(strcmp(command->args[0].string, "left") == 0){
          if(app->tab_list_layout->tab_list.tab_count == 1){
               ce_app_message(app, "cannot select adjacent tab, there is only 1 tab");
               return CE_COMMAND_NO_ACTION;
          }

          if(select_tab_left(app)){
               app->message_mode = false;
               return CE_COMMAND_SUCCESS;
          }

          return CE_COMMAND_NO_ACTION;
     }else if(strcmp(command->args[0].string, "right") == 0){
          if(app->tab_list_layout->tab_list.tab_count == 1){
               ce_app_message(app, "cannot select adjacent tab, there is only 1 tab");
               return CE_COMMAND_NO_ACTION;
          }

          if(select_tab_right(app)){
               app->message_mode = false;
               return CE_COMMAND_SUCCESS;
          }

          return CE_COMMAND_NO_ACTION;
     }

     return CE_COMMAND_PRINT_HELP;
}

CeCommandStatus_t command_search(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(strcmp(command->args[0].string, "forward") == 0){
          ce_app_input(app, "Search", search_input_complete_func);
          app->vim.search_mode = CE_VIM_SEARCH_MODE_FORWARD;
          app->search_start = command_context.view->cursor;
     }else if(strcmp(command->args[0].string, "backward") == 0){
          ce_app_input(app, "Reverse Search", search_input_complete_func);
          app->vim.search_mode = CE_VIM_SEARCH_MODE_BACKWARD;
          app->search_start = command_context.view->cursor;
     }else{
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

CeCommandStatus_t command_setpaste(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     app->vim.pasting = true;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_setnopaste(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     app->vim.pasting = false;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_regex_search(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(strcmp(command->args[0].string, "forward") == 0){
          ce_app_input(app, "Regex Search", search_input_complete_func);
          app->vim.search_mode = CE_VIM_SEARCH_MODE_REGEX_FORWARD;
          app->search_start = command_context.view->cursor;
     }else if(strcmp(command->args[0].string, "backward") == 0){
          ce_app_input(app, "Reverse Regex Search", search_input_complete_func);
          app->vim.search_mode = CE_VIM_SEARCH_MODE_REGEX_BACKWARD;
          app->search_start = command_context.view->cursor;
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     app->highlight_search = true;
     return CE_COMMAND_SUCCESS;
}

// lol
CeCommandStatus_t command_command(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(command_context.view){
          ce_app_input(app, "Run Command", command_input_complete_func);
          ce_app_init_command_completion(app, &app->input_complete);
          build_complete_list(app->complete_list_buffer, &app->input_complete);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_switch_buffer(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     ce_app_input(app, "Switch Buffer", switch_buffer_input_complete_func);

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

     ce_complete_init(&app->input_complete, (const char**)filenames, NULL, buffer_count);
     build_complete_list(app->complete_list_buffer, &app->input_complete);

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
                                       CeVim_t* vim, CeMultipleCursors_t* multiple_cursors, bool insert_into_jump_list,
                                       const char* base_directory, CeDestination_t* destination){
     char full_path[PATH_MAX];
     if(!base_directory && destination->filepath[0] != '/') base_directory = ".";
     if(base_directory){
          snprintf(full_path, PATH_MAX, "%s/%s", base_directory, destination->filepath);
     }else{
          strncpy(full_path, destination->filepath, PATH_MAX);
     }
     CeBuffer_t* load_buffer = load_file_into_view(buffer_node_head, view, config_options, vim,
                                                   multiple_cursors, insert_into_jump_list, full_path);
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
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(command_context.view->buffer->line_count == 0) return CE_COMMAND_NO_ACTION;

     CeDestination_t destination = scan_line_for_destination(command_context.view->buffer->lines[command_context.view->cursor.y]);
     if(destination.point.x < 0 || destination.point.y < 0){
          ce_app_message(app, "failed to determine file destination at %s:%d", command_context.view->buffer->name, command_context.view->cursor.y);
          return CE_COMMAND_NO_ACTION;
     }

     CeAppBufferData_t* buffer_data = command_context.view->buffer->app_data;
     buffer_data->last_goto_destination = command_context.view->cursor.y;

     char* base_directory = buffer_base_directory(command_context.view->buffer);
     CeBuffer_t* buffer = load_destination_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                                                     &app->multiple_cursors, true, base_directory, &destination);
     free(base_directory);
     if(!buffer) return CE_COMMAND_NO_ACTION;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_goto_next_destination(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     CeBuffer_t* buffer = app->last_goto_buffer;
     if(!buffer || buffer->line_count == 0) return CE_COMMAND_SUCCESS;

     CeAppBufferData_t* buffer_data = buffer->app_data;

     int64_t save_destination = buffer_data->last_goto_destination;
     for(int64_t i = buffer_data->last_goto_destination + 1; i != buffer_data->last_goto_destination; i++){
          if(i >= buffer->line_count){
               i = 0;
               if(i == buffer_data->last_goto_destination) break;
          }

          CeDestination_t destination = scan_line_for_destination(buffer->lines[i]);
          if(destination.point.x < 0 || destination.point.y < 0) continue;

          char* base_directory = buffer_base_directory(buffer);
          CeBuffer_t* loaded_buffer = load_destination_into_view(&app->buffer_node_head, command_context.view,
                                                                 &app->config_options, &app->vim,
                                                                 &app->multiple_cursors, true, base_directory, &destination);
          free(base_directory);
          if(loaded_buffer){
               CeLayout_t* layout = ce_layout_buffer_in_view(command_context.tab_layout, buffer);
               if(layout) layout->view.scroll.y = i;
               buffer_data->last_goto_destination = i;
               break;
          }
     }

     // we didn't find anything, and since the user asked for a destination, find this one
     if(buffer_data->last_goto_destination == save_destination && save_destination < buffer->line_count){
          CeDestination_t destination = scan_line_for_destination(buffer->lines[save_destination]);
          if(destination.point.x >= 0 && destination.point.y >= 0){
               CeLayout_t* layout = ce_layout_buffer_in_view(command_context.tab_layout, buffer);
               if(layout) layout->view.scroll.y = save_destination;
               char* base_directory = buffer_base_directory(buffer);
               load_destination_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                                          &app->multiple_cursors, true, base_directory, &destination);
               free(base_directory);
          }
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_goto_prev_destination(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     CeBuffer_t* buffer = app->last_goto_buffer;
     if(!buffer || buffer->line_count == 0) return CE_COMMAND_SUCCESS;

     CeAppBufferData_t* buffer_data = buffer->app_data;

     int64_t save_destination = buffer_data->last_goto_destination;
     for(int64_t i = buffer_data->last_goto_destination - 1; i != buffer_data->last_goto_destination; i--){
          if(i < 0){
               i = buffer->line_count - 1;
               if(i == buffer_data->last_goto_destination) break;
          }

          CeDestination_t destination = scan_line_for_destination(buffer->lines[i]);
          if(destination.point.x < 0 || destination.point.y < 0) continue;

          char* base_directory = buffer_base_directory(buffer);
          CeBuffer_t* loaded_buffer = load_destination_into_view(&app->buffer_node_head, command_context.view,
                                                                 &app->config_options, &app->vim,
                                                                 &app->multiple_cursors, true, base_directory,
                                                                 &destination);
          free(base_directory);
          if(loaded_buffer){
               CeLayout_t* layout = ce_layout_buffer_in_view(command_context.tab_layout, buffer);
               if(layout) layout->view.scroll.y = i;
               buffer_data->last_goto_destination = i;
               break;
          }
     }

     // we didn't find anything, and since the user asked for a destination, find this one
     if(buffer_data->last_goto_destination == save_destination && save_destination < buffer->line_count){
          CeDestination_t destination = scan_line_for_destination(buffer->lines[save_destination]);
          if(destination.point.x >= 0 && destination.point.y >= 0){
               char* base_directory = buffer_base_directory(buffer);
               load_destination_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                                          &app->multiple_cursors, true, base_directory, &destination);
               free(base_directory);
          }
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_goto_prev_buffer_in_view(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(!ce_app_switch_to_prev_buffer_in_view(app, command_context.view, false)){
          return CE_COMMAND_NO_ACTION;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_replace_all(CeCommand_t* command, void* user_data){
     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(command->arg_count == 1 && command->args[0].type == CE_COMMAND_ARG_STRING){
          int64_t index = ce_vim_register_index('/');
          CeVimYank_t* yank = app->vim.yanks + index;
          if(yank->text){
               replace_all(command_context.view, &app->vim_visual_save, yank->text, command->args[0].string);
          }else{
               ce_app_message(app, "only 1 argument used for replace_all, but search yank register is empty");
               return CE_COMMAND_NO_ACTION;
          }
     }else if(command->arg_count == 2 && command->args[0].type == CE_COMMAND_ARG_STRING && command->args[1].type == CE_COMMAND_ARG_STRING){
          replace_all(command_context.view, &app->vim_visual_save, command->args[0].string, command->args[1].string);
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_reload_file(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(access(command_context.view->buffer->name, F_OK) == -1){
          ce_app_message(app, "buffer '%s' is not file backed, unable to reload\n", command_context.view->buffer->name);
          return CE_COMMAND_NO_ACTION;
     }

     char* filename = strdup(command_context.view->buffer->name);
     CeAppBufferData_t* buffer_data = command_context.view->buffer->app_data;
     ce_buffer_free(command_context.view->buffer);
     command_context.view->buffer->app_data = buffer_data; // NOTE: not great that I need to save user data and reset it
     ce_buffer_load_file(command_context.view->buffer, filename);
     free(filename);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_reload_config(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     if(!app->user_config.handle) return CE_COMMAND_NO_ACTION;

     if(app->command_entries) free(app->command_entries);
     ce_app_init_default_commands(app);
     char* config_path = strdup(app->user_config.filepath);
     user_config_free(&app->user_config);
     if(user_config_init(&app->user_config, config_path)){
          app->user_config.init_func(app);
     }else{
          ce_app_message(app, "failed to reload config: '%s', see log for details", config_path);
          return CE_COMMAND_FAILURE;
     }
     free(config_path);
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_syntax(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     CeAppBufferData_t* buffer_data = command_context.view->buffer->app_data;

     if(strcmp(command->args[0].string, "c") == 0){
          buffer_data->syntax_function = ce_syntax_highlight_c;
     }else if(strcmp(command->args[0].string, "cpp") == 0 ||
              strcmp(command->args[0].string, "c++") == 0){
          buffer_data->syntax_function = ce_syntax_highlight_cpp;
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
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     const char* buffer_name = "unnamed";
     if(command->arg_count == 1 && command->args[0].type == CE_COMMAND_ARG_STRING) buffer_name = command->args[0].string;

     CeBuffer_t* buffer = new_buffer();
     ce_buffer_alloc(buffer, 1, buffer_name);
     command_context.view->buffer = buffer;
     command_context.view->cursor = (CePoint_t){0, 0};
     ce_buffer_node_insert(&app->buffer_node_head, buffer);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_rename_buffer(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     free(command_context.view->buffer->name);
     command_context.view->buffer->name = strdup(command->args[0].string);
     if(command_context.view->buffer->status == CE_BUFFER_STATUS_NONE) command_context.view->buffer->status = CE_BUFFER_STATUS_MODIFIED;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_jump_list(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     CeDestination_t* destination = NULL;
     int64_t view_width = ce_view_width(command_context.view);
     int64_t view_height = ce_view_height(command_context.view);
     CeRect_t view_rect = {command_context.view->scroll.x, command_context.view->scroll.x + view_width,
                           command_context.view->scroll.y, command_context.view->scroll.y + view_height};
     CeAppViewData_t* view_data = command_context.view->user_data;
     CeJumpList_t* jump_list = &view_data->jump_list;

     // TODO: I think these names are backwards ?
     if(strcmp(command->args[0].string, "next")){
          // ignore destinations on screen
          while((destination = ce_jump_list_next(jump_list))){
               if(strcmp(destination->filepath, command_context.view->buffer->name) != 0 || !ce_point_in_rect(destination->point, view_rect)){
                    break;
               }
          }
     }else if(strcmp(command->args[0].string, "previous")){
          // ignore destinations on screen
          while((destination = ce_jump_list_previous(jump_list))){
               if(strcmp(destination->filepath, command_context.view->buffer->name) != 0 || !ce_point_in_rect(destination->point, view_rect)){
                    break;
               }
          }
     }

     if(destination){
          if(load_file_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                                 &app->multiple_cursors, false, destination->filepath)){
               command_context.view->cursor = destination->point;
          }else{
               CeBufferNode_t* itr = app->buffer_node_head;
               while(itr){
                    if(strcmp(itr->buffer->name, destination->filepath) == 0){
                         itr->buffer->cursor_save = destination->point;
                         ce_view_switch_buffer(command_context.view, itr->buffer, &app->vim, &app->multiple_cursors,
                                               &app->config_options, false);
                         break;
                    }
                    itr = itr->next;
               }
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

CeCommandStatus_t command_man_page_on_word_under_cursor(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     CeRange_t range = ce_vim_find_little_word_boundaries(command_context.view->buffer, command_context.view->cursor); // returns -1
     char* word = ce_buffer_dupe_string(command_context.view->buffer, range.start, (range.end.x - range.start.x) + 1);
     if(!word) return CE_COMMAND_NO_ACTION;
     char cmd[128];
     snprintf(cmd, 128, "man %s", word);
     free(word);
     ce_app_run_shell_command(app, cmd, command_context.tab_layout, command_context.view);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_shell_command(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(!ce_app_run_shell_command(app, command->args[0].string, command_context.tab_layout, command_context.view)){
          return CE_COMMAND_FAILURE;
     }

     return CE_COMMAND_SUCCESS;
}

void buffer_replace_all(CeBuffer_t* buffer, CePoint_t cursor, const char* match, const char* replacement, CePoint_t start, CePoint_t end,
                        bool regex_search){
     bool chain_undo = false;
     int64_t match_len = 0;
     regex_t regex = {};
     if(regex_search){
          int rc = regcomp(&regex, match, REG_EXTENDED);
          if(rc != 0){
               char error_buffer[BUFSIZ];
               regerror(rc, &regex, error_buffer, BUFSIZ);
               ce_log("regcomp() failed: '%s'", error_buffer);
               return;
          }
     }else{
           match_len = strlen(match);
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

void replace_all(CeView_t* view, CeVimVisualSave_t* vim_visual_save, const char* match, const char* replace){
     CePoint_t start;
     CePoint_t end;
     if(vim_visual_save->mode == CE_VIM_MODE_VISUAL){
          if(ce_point_after(view->cursor, vim_visual_save->visual_point)){
               start = vim_visual_save->visual_point;
               end = view->cursor;
          }else{
               start = view->cursor;
               end = vim_visual_save->visual_point;
          }
     }else if(vim_visual_save->mode == CE_VIM_MODE_VISUAL_LINE){
          if(ce_point_after(view->cursor, vim_visual_save->visual_point)){
               start = (CePoint_t){0, vim_visual_save->visual_point.y};
               end = (CePoint_t){ce_utf8_last_index(view->buffer->lines[view->cursor.y]), view->cursor.y};
          }else{
               start = (CePoint_t){0, view->cursor.y};
               end = (CePoint_t){ce_utf8_last_index(view->buffer->lines[vim_visual_save->visual_point.y]), vim_visual_save->visual_point.y};
          }
     }else{
          start = view->cursor;
          end = ce_buffer_end_point(view->buffer);
     }

     if(ce_point_after(end, start)){
          buffer_replace_all(view->buffer, view->cursor, match, replace, start, end, false);
     }
}

CeCommandStatus_t command_vim_e(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     load_file_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                         &app->multiple_cursors, true, command->args[0].string);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_vim_w(CeCommand_t* command, void* user_data){
     return command_save_buffer(command, user_data);
}

CeCommandStatus_t command_vim_q(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     if(app->tab_list_layout->tab_list.tab_count == 1 &&
        ce_layout_tab_get_layout_count(app->tab_list_layout->tab_list.current) == 1){
          return command_quit(command, user_data);
     }

     if(!delete_layout(user_data)) return CE_COMMAND_FAILURE;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_vim_q_exclam(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     if(app->tab_list_layout->tab_list.tab_count == 1 &&
        ce_layout_tab_get_layout_count(app->tab_list_layout->tab_list.current) == 1){
          app->quit = true;
          return command_quit(command, user_data);
     }

     if(!delete_layout(user_data)) return CE_COMMAND_FAILURE;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_vim_qa_exclam(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     app->quit = true;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_vim_wq(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     try_save_buffer(app, command_context.view->buffer);

     if(app->tab_list_layout->tab_list.tab_count == 1 &&
        ce_layout_tab_get_layout_count(app->tab_list_layout->tab_list.current) == 1){
          return command_quit(command, user_data);
     }

     if(!delete_layout(user_data)) return CE_COMMAND_FAILURE;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_vim_sp(CeCommand_t* command, void* user_data){
     if(command->arg_count > 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     split_layout(app, true);

     if(command->arg_count == 1){
          if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
          CommandContext_t command_context = {};
          if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;
          load_file_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                              &app->multiple_cursors, true, command->args[0].string);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_vim_vsp(CeCommand_t* command, void* user_data){
     if(command->arg_count > 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     split_layout(app, false);

     if(command->arg_count == 1){
          if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
          CommandContext_t command_context = {};
          if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;
          load_file_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                              &app->multiple_cursors, true, command->args[0].string);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_vim_tabnew(CeCommand_t* command, void* user_data){
     return command_new_tab(command, user_data);
}

CeCommandStatus_t command_vim_tabnext(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     if(select_tab_right(app)){
          return CE_COMMAND_SUCCESS;
     }
     return CE_COMMAND_NO_ACTION;
}

CeCommandStatus_t command_vim_tabprevious(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     if(select_tab_left(app)){
          return CE_COMMAND_SUCCESS;
     }
     return CE_COMMAND_NO_ACTION;
}

CeCommandStatus_t command_vim_cn(CeCommand_t* command, void* user_data){
     return command_goto_next_destination(command, user_data);
}

CeCommandStatus_t command_vim_cp(CeCommand_t* command, void* user_data){
     return command_goto_prev_destination(command, user_data);
}

CeCommandStatus_t command_vim_make(CeCommand_t* command, void* user_data){
     CeApp_t* app = user_data;

     if(command->arg_count == 0){
          CommandContext_t command_context = {};
          if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;
          ce_app_run_shell_command(app, "make", command_context.tab_layout, command_context.view);
     }else if(command->arg_count == 1 &&
              command->args[0].type == CE_COMMAND_ARG_STRING){
          CommandContext_t command_context = {};
          if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;
          char command_string[256];
          snprintf(command_string, 256, "make %s", command->args[0].string);
          ce_app_run_shell_command(app, command_string, command_context.tab_layout, command_context.view);
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

static void open_file_in_dir_recursively(char* path, char* match, CeApp_t* app, CeView_t* view){
     DIR* dir;
     struct dirent *ent;
     if((dir = opendir(path)) != NULL){
          while((ent = readdir(dir)) != NULL){
               if(ent->d_type == DT_DIR && strcmp(ent->d_name, ".") != 0  && strcmp(ent->d_name, "..") != 0){
                    char new_path[PATH_MAX];
                    snprintf(new_path, PATH_MAX, "%s/%s", path, ent->d_name);
                    open_file_in_dir_recursively(new_path, match, app, view);
               }else if(strcmp(ent->d_name, match) == 0){
                    char full_path[PATH_MAX];
                    snprintf(full_path, PATH_MAX, "%s/%s", path, ent->d_name);
                    load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                        &app->multiple_cursors, true, full_path);
               }
          }
          closedir(dir);
     }
}

CeCommandStatus_t command_vim_find(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     char* base_directory = buffer_base_directory(command_context.view->buffer);
     if(!base_directory) base_directory = strdup(".");
     open_file_in_dir_recursively(base_directory, command->args[0].string, app, command_context.view);
     free(base_directory);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_vim_wqa(CeCommand_t* command, void* user_data){
     return command_save_all_and_quit(command, user_data);
}

CeCommandStatus_t command_vim_xa(CeCommand_t* command, void* user_data){
     return command_save_all_and_quit(command, user_data);
}

#ifdef ENABLE_DEBUG_KEY_PRESS_INFO
CeCommandStatus_t command_toggle_log_keys_pressed(CeCommand_t* command, void* user_data){
     CeApp_t* app = user_data;
     app->log_key_presses = !app->log_key_presses;
     return CE_COMMAND_SUCCESS;
}
#endif
