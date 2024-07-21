#include "ce_commands.h"
#include "ce_draw_gui.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#if defined(DISPLAY_TERMINAL)
    #include <ncurses.h>
#elif defined(DISPLAY_GUI)
    #include <SDL2/SDL_clipboard.h>
#endif

#if !defined(PLATFORM_WINDOWS)
    #include <unistd.h>
#endif

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
          ce_view_switch_buffer(command_context.view, app->buffer_list_buffer, &app->vim,
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
     ce_view_switch_buffer(command_context.view, buffer, &app->vim, &app->config_options, true);
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
     bool always_add_last = false;
     CeLayout_t* new_layout = ce_layout_split(tab_layout, vertical, always_add_last);

     if(new_layout){
          ce_view_follow_cursor(&new_layout->view, app->config_options.horizontal_scroll_off,
                                app->config_options.vertical_scroll_off, app->config_options.tab_width);
          tab_layout->tab.current = new_layout;

          CeLayout_t* parent = ce_layout_find_parent(tab_layout, new_layout);
          if(parent && parent->type == CE_LAYOUT_TYPE_LIST){
               for(int64_t i = 0; i < parent->list.layout_count; i++){
                    CeLayout_t* layout = parent->list.layouts[i];
                    ce_view_follow_cursor(&layout->view, app->config_options.horizontal_scroll_off,
                                          app->config_options.vertical_scroll_off, app->config_options.tab_width);

               }
          }
     }

     return new_layout;
}

CeCommandStatus_t command_balance_layout(CeCommand_t* command, void* user_data){
     CeApp_t* app = user_data;
     ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);
     return CE_COMMAND_SUCCESS;
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

     CeLayout_t* new_layout = split_layout(app, vertical);
     if(new_layout == NULL){
         return CE_COMMAND_FAILURE;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_resize_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 3) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
     if(command->args[1].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
     if(command->args[2].type != CE_COMMAND_ARG_INTEGER) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     CeLayout_t* current_layout = tab_layout->tab.current;
     bool expand = false;
     CeDirection_t direction;

     if(strcmp(command->args[0].string, "expand") == 0){
         expand = true;
     }else if(strcmp(command->args[0].string, "shrink") == 0){
          // pass
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     if(strcmp(command->args[1].string, "left") == 0){
          direction = CE_LEFT;
     }else if(strcmp(command->args[1].string, "right") == 0){
          direction = CE_RIGHT;
     }else if(strcmp(command->args[1].string, "up") == 0){
          direction = CE_UP;
     }else if(strcmp(command->args[1].string, "down") == 0){
          direction = CE_DOWN;
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     if(ce_layout_resize_rect(app->tab_list_layout, current_layout, app->terminal_rect, direction, expand, command->args[2].integer)){
          return CE_COMMAND_SUCCESS;
     }

     return CE_COMMAND_FAILURE;
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

     CeLayout_t* parent_of_current = ce_layout_find_parent(tab_layout->tab.root, tab_layout->tab.current);

     // check if this is the only view, and ignore the delete request
     if(app->tab_list_layout->tab_list.tab_count == 1 &&
        tab_layout->tab.root->type == CE_LAYOUT_TYPE_LIST &&
        tab_layout->tab.root->list.layout_count == 1 &&
        parent_of_current == tab_layout->tab.root){
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

     CeLayout_t* layout = ce_layout_find_at(tab_layout, cursor);
     if(layout) tab_layout->tab.current = layout;

     return true;
}

CeCommandStatus_t command_delete_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     if(!delete_layout(user_data)) return CE_COMMAND_FAILURE;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_open_popup_view(CeCommand_t* command, void* user_data){
     if(command->arg_count < 0 || command->arg_count > 1) return CE_COMMAND_PRINT_HELP;
     if(command->arg_count == 1 &&
        command->args[0].type != CE_COMMAND_ARG_STRING){
         printf("this man\n");
         return CE_COMMAND_PRINT_HELP;
     }
     CeApp_t* app = user_data;
     CeBuffer_t* buffer = app->shell_command_buffer;
     if(command->arg_count == 0){
         if(app->last_popup_buffer){
             buffer = app->last_popup_buffer;
         }
     }else{
         CeBufferNode_t* itr = app->buffer_node_head;
         while(itr){
              if(strcmp(itr->buffer->name, command->args[0].string) == 0){
                  buffer = itr->buffer;
                  break;
              }
              itr = itr->next;
         }
     }
     if(!ce_app_open_popup_view(app, buffer)) return CE_COMMAND_FAILURE;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_close_popup_view(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     CeApp_t* app = user_data;
     if(!ce_app_close_popup_view(app)) return CE_COMMAND_FAILURE;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_create_file(CeCommand_t* command, void* user_data){
     if(command->arg_count < 0 || command->arg_count > 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     FILE* f = fopen(command->args[0].string, "w+");
     if(f == NULL){
         ce_app_message(app, "Failed to create file: %s", strerror(errno));
         return CE_COMMAND_FAILURE;
     }
     fclose(f);
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_remove_file(CeCommand_t* command, void* user_data){
     if(command->arg_count < 0 || command->arg_count > 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;

     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     int rc = remove(command->args[0].string);
     if(rc != 0){
         ce_app_message(app, "Failed to remove file: %s", strerror(errno));
         return CE_COMMAND_FAILURE;
     }
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
                                  true, command->args[0].string)){
               ce_app_message(app, "failed to load file %s: '%s'", command->args[0].string, strerror(errno));
          }else{
               ce_clangd_file_open(&app->clangd, app->buffer_node_head->buffer);
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
     char* path = ce_strndup((char*)directory, MAX_PATH_LEN);
     CeListDirResult_t list_dir_result = ce_list_dir(path);

     int64_t path_len = strnlen(path, MAX_PATH_LEN);

    // Strip the search characters at the end of the path if they exist.
#if defined(PLATFORM_WINDOWS)
     if(path_len > 0 && path_len < MAX_PATH_LEN){
         if(path[path_len - 1] == CE_CURRENT_DIR_SEARCH){
             path[path_len - 1] = 0;
             path_len--;
         }
         if(path_len > 0 && path[path_len - 1] == CE_PATH_SEPARATOR){
             path[path_len - 1] = 0;
             path_len--;
         }
     }
#else
     if(path_len > 2){
         if(path[path_len - 1] == CE_CURRENT_DIR_SEARCH){
             path[path_len - 1] = 0;
             path_len--;
         }
         if(path_len > 0 && path[path_len - 1] == CE_PATH_SEPARATOR){
             path[path_len - 1] = 0;
             path_len--;
         }
     }

#endif

     bool path_is_current_dir = false;
#if defined(PLATFORM_WINDOWS)
     path_is_current_dir = path_len == 2 &&
                           path[0] == '.' &&
                           path[1] == '\\';
#else
     path_is_current_dir = path_len == 1 &&
                           path[0] == '.';
#endif


     char full_path[MAX_PATH_LEN];
     for(int64_t i = 0; i < list_dir_result.count; i++){
          if(strcmp(list_dir_result.filenames[i], ".") == 0 ||
             strcmp(list_dir_result.filenames[i], "..") == 0){
              continue;
          }
          int full_path_len = 0;
          if (path_is_current_dir){
              full_path_len = snprintf(full_path, MAX_PATH_LEN, "%s", list_dir_result.filenames[i]);
          }else{
              full_path_len = snprintf(full_path, MAX_PATH_LEN, "%s%c%s", path,
                                       CE_PATH_SEPARATOR, list_dir_result.filenames[i]);
          }
          if(list_dir_result.is_directories[i] && full_path_len < (MAX_PATH_LEN - 3)){
               bool ignore_dir = false;
               for(uint64_t j = 0; j < ignore_dir_count; j++){
                    if(full_path_len >= ignore_dir_lens[j] &&
                       strcmp(full_path + full_path_len - ignore_dir_lens[j], ignore_dirs[j]) == 0){
                        ignore_dir = true;
                        break;
                    }
               }
               if(ignore_dir) continue;
               full_path[full_path_len] = CE_PATH_SEPARATOR;
               full_path[full_path_len + 1] = CE_CURRENT_DIR_SEARCH;
               full_path[full_path_len + 2] = 0;
               node = find_files_in_directory_recursively(full_path, node, ignore_dirs,
                                                          ignore_dir_lens, ignore_dir_count);
          }else{
               CeStrNode_t* new_node = malloc(sizeof(*new_node));
               new_node->string = strdup(full_path);
               new_node->next = NULL;
               node->next = new_node;
               node = new_node;
          }
     }
     ce_free_list_dir_result(&list_dir_result);
     free(path);

     return node;
}

CeCommandStatus_t command_load_directory_files(CeCommand_t* command, void* user_data){
    CeApp_t* app = user_data;
    CommandContext_t command_context = {};

    if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

    if(command->arg_count <= 0 ||
       command->args[0].type != CE_COMMAND_ARG_STRING){
        return CE_COMMAND_PRINT_HELP;
    }

    char base_directory[MAX_PATH_LEN];
#if defined(PLATFORM_WINDOWS)
    snprintf(base_directory, MAX_PATH_LEN, "%s\\*", command->args[0].string);
#else
    snprintf(base_directory, MAX_PATH_LEN, "%s", command->args[0].string);
#endif

    // setup our ignore dirs
    int ignore_dir_count = (command->arg_count - 1);
    char** ignore_dirs = malloc(sizeof(*ignore_dirs) * ignore_dir_count);
    for(int64_t i = 1; i < command->arg_count; i++){
        // TODO: this imposes a restriction that folders cannot be named numbers like 5
        if(command->args[i].type != CE_COMMAND_ARG_STRING){
            return CE_COMMAND_PRINT_HELP;
        }
        ignore_dirs[i - 1] = strdup(command->args[i].string);
    }

    int* ignore_dir_lens = malloc(sizeof(*ignore_dir_lens) * ignore_dir_count);
    for(int64_t i = 0; i < ignore_dir_count; i++){
        ignore_dir_lens[i] = strlen(ignore_dirs[i]);
    }

    // head is a dummy node that doesn't contain any info, just makes the find_files_in_directory_recursively()
    // interface better
    CeStrNode_t* head = calloc(sizeof(*head), 1);
    find_files_in_directory_recursively(base_directory, head, ignore_dirs, ignore_dir_lens, ignore_dir_count);

    // cleanup our ignore dirs
    for(int64_t i = 0; i < ignore_dir_count; i++){
        free(ignore_dirs[i]);
    }
    free(ignore_dirs);
    free(ignore_dir_lens);

    // convert linked list of strings to array of strings
    CeStrNode_t* save_head = head;

    ce_app_clear_filepath_cache(app);

    app->cached_filepath_count = 0;
    while(head){
        if(head->string){
            app->cached_filepath_count++;
        }
        head = head->next;
    }

    app->cached_filepaths = malloc(sizeof(*app->cached_filepaths) * app->cached_filepath_count);

    head = save_head;

    uint64_t file_index = 0;
    while(head){
        if(head->string){
            app->cached_filepaths[file_index] = strdup(head->string);
            file_index++;
        }
        CeStrNode_t* node = head;
        head = head->next;
        free(node->string);
        free(node);
    }

    // setup input and completion
    ce_app_input(app, "Load Directory File", load_project_file_input_complete_func);
    ce_complete_init(&app->input_complete, (const char**)(app->cached_filepaths), NULL, app->cached_filepath_count);
    build_complete_list(app->complete_list_buffer, &app->input_complete);

    return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_load_cached_files(CeCommand_t* command, void* user_data){
    CeApp_t* app = user_data;
    CommandContext_t command_context = {};

    if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

    if(app->cached_filepath_count <= 0) return CE_COMMAND_NO_ACTION;

    // setup input and completion
    ce_app_input(app, "Load Directory File", load_project_file_input_complete_func);
    ce_complete_init(&app->input_complete, (const char**)(app->cached_filepaths), NULL, app->cached_filepath_count);
    build_complete_list(app->complete_list_buffer, &app->input_complete);

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

bool select_tab_left(CeApp_t* app){
     for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
          if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
               if(i > 0){
                    app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i - 1];
                    return true;
               }else{
                    // wrap around
                    app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[app->tab_list_layout->tab_list.tab_count - 1];
                    return true;
               }
               break;
          }
     }
     return false;
}

bool select_tab_right(CeApp_t* app){
     for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
          if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
               if(i < (app->tab_list_layout->tab_list.tab_count - 1)){
                    app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i + 1];
                    return true;
               }else{
                    // wrap around
                    app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[0];
                    return true;
               }
               break;
          }
     }
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
#if defined(DISPLAY_TERMINAL)
     clear();
#endif
     return CE_COMMAND_SUCCESS;
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
                                                     true, base_directory, &destination);
     free(base_directory);
     if(!buffer) return CE_COMMAND_NO_ACTION;
     ce_clangd_file_open(&app->clangd, app->buffer_node_head->buffer);

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
                                                                 true, base_directory, &destination);
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
                                          true, base_directory, &destination);
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
                                                                 true, base_directory,
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
                                          true, base_directory, &destination);
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
          int64_t index = ce_vim_register_index(CE_PATH_SEPARATOR);
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

     bool file_backed = false;

#if defined(PLATFORM_WINDOWS)
     file_backed = (_access(command_context.view->buffer->name, 0) == 0);
#else
     file_backed = (access(command_context.view->buffer->name, F_OK) == 0);
#endif

     if(!file_backed){
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
#if defined(PLATFORM_WINDOWS)
     if(app->user_config.library_instance == NULL) return CE_COMMAND_NO_ACTION;
#else
     if(!app->user_config.handle) return CE_COMMAND_NO_ACTION;
#endif

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
                                 false, destination->filepath)){
               command_context.view->cursor = destination->point;
          }else{
               ce_clangd_file_open(&app->clangd, app->buffer_node_head->buffer);
               CeBufferNode_t* itr = app->buffer_node_head;
               while(itr){
                    if(strcmp(itr->buffer->name, destination->filepath) == 0){
                         itr->buffer->cursor_save = destination->point;
                         ce_view_switch_buffer(command_context.view, itr->buffer, &app->vim,
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
     ce_app_run_shell_command(app, cmd, command_context.tab_layout, command_context.view, false);

     return CE_COMMAND_SUCCESS;
}

static char* build_string_from_command_args(CeCommand_t* command){
     char buffer[256];
     int64_t buffer_consumed = 0;

     char* result = NULL;
     for(int64_t i = 0; i < command->arg_count; i++){
          int rc = 0;

          switch(command->args[i].type){
          default:
              return result;
          case CE_COMMAND_ARG_INTEGER:
               rc = snprintf(buffer + buffer_consumed, 256 - buffer_consumed, "%" PRId64 " ", command->args[i].integer);
               break;
          case CE_COMMAND_ARG_DECIMAL:
               rc = snprintf(buffer + buffer_consumed, 256 - buffer_consumed, "%f ", command->args[i].decimal);
               break;
          case CE_COMMAND_ARG_STRING:
               rc = snprintf(buffer + buffer_consumed, 256 - buffer_consumed, "%s ", command->args[i].string);
               break;
          }

          if(rc < 0){
               ce_log("snprintf() failed: %s\n", strerror(errno));
               return result;
          }

          buffer_consumed += rc;
     }

     result = strdup(buffer);

     // strip off extra space at the end
     size_t len = strlen(result);
     if(len >= 1) result[len - 1] = 0;

     return result;
}

CeCommandStatus_t command_shell_command(CeCommand_t* command, void* user_data){
     if(command->arg_count < 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     char* command_args = build_string_from_command_args(command);

     if(!ce_app_run_shell_command(app, command_args, command_context.tab_layout, command_context.view, false)){
          free(command_args);
          return CE_COMMAND_FAILURE;
     }

     free(command_args);
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_shell_command_relative(CeCommand_t* command, void* user_data){
     if(command->arg_count < 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;
#if defined(PLATFORM_WINDOWS)
     ce_app_message(app, "shell_command_relative not yet supported on windows. Please use shell_command.");
     return CE_COMMAND_FAILURE;
#else
     char* command_args = build_string_from_command_args(command);

     if(!ce_app_run_shell_command(app, command_args, command_context.tab_layout, command_context.view, true)){
          free(command_args);
          return CE_COMMAND_FAILURE;
     }

     free(command_args);
     return CE_COMMAND_SUCCESS;
#endif
}

CeCommandStatus_t command_font_adjust_size(CeCommand_t* command, void* user_data) {
     if(command->arg_count < 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_INTEGER) return CE_COMMAND_PRINT_HELP;

#if defined(DISPLAY_TERMINAL)
     return CE_COMMAND_SUCCESS;
#elif defined(DISPLAY_GUI)
     CeApp_t* app = (CeApp_t*)(user_data);
     CeGui_t* gui = app->gui;
     int new_font_size = gui->font_point_size + command->args[0].integer;
     if (new_font_size % 2 != 0) {
         ce_app_message(app, "requested font size %d, but only even font sizes are supported", new_font_size);
         return CE_COMMAND_FAILURE;
     }
     if (gui_load_font(gui,
                       app->config_options.gui_font_path,
                       new_font_size,
                       gui->font_line_separation) != 0) {
         return CE_COMMAND_FAILURE;
     }

     int calculated_terminal_width = gui->window_width / (gui->font_point_size / 2);
     int calculated_terminal_height = gui->window_height / (gui->font_point_size + gui->font_line_separation);
     ce_app_update_terminal_view(app, calculated_terminal_width, calculated_terminal_height);

     return CE_COMMAND_SUCCESS;
#endif
}

CeCommandStatus_t command_paste_clipboard(CeCommand_t* command, void* user_data) {
     if(command->arg_count >= 1) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};
     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     CePoint_t resulting_cursor = ce_paste_clipboard_into_buffer(command_context.view->buffer,
                                                                 command_context.view->cursor);
     if(resulting_cursor.x <= 0){
          return CE_COMMAND_FAILURE;
     }

     command_context.view->cursor = resulting_cursor;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_clang_goto_def(CeCommand_t* command, void* user_data){
     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};
     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(!ce_clangd_request_goto_def(&app->clangd,
                                    command_context.view->buffer,
                                    command_context.view->cursor)){
         return CE_COMMAND_FAILURE;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_clang_goto_decl(CeCommand_t* command, void* user_data){
     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};
     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(!ce_clangd_request_goto_decl(&app->clangd,
                                     command_context.view->buffer,
                                     command_context.view->cursor)){
         return CE_COMMAND_FAILURE;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_clang_goto_type_def(CeCommand_t* command, void* user_data){
     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};
     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(!ce_clangd_request_goto_type_def(&app->clangd,
                                         command_context.view->buffer,
                                         command_context.view->cursor)){
         return CE_COMMAND_FAILURE;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_clang_find_references(CeCommand_t* command, void* user_data){
     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};
     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     if(!ce_clangd_request_find_references(&app->clangd,
                                           command_context.view->buffer,
                                           command_context.view->cursor)){
         return CE_COMMAND_FAILURE;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_clang_format_file(CeCommand_t* command, void* user_data){
     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};
     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;
     if(strlen(app->config_options.clang_format_path) == 0){
         ce_app_message(app, "clang format binary/executable path not configured");
         return CE_COMMAND_FAILURE;
     }
     if(!ce_clang_format_buffer(app->config_options.clang_format_path, command_context.view->buffer,
                                command_context.view->cursor)){
         return CE_COMMAND_FAILURE;
     }
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_clang_format_selection(CeCommand_t* command, void* user_data){
     CeApp_t* app = (CeApp_t*)(user_data);
     CommandContext_t command_context = {};
     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;
     if(strlen(app->config_options.clang_format_path) == 0){
         ce_app_message(app, "clang format binary/executable path not configured");
         return CE_COMMAND_FAILURE;
     }
     if(!ce_clang_format_selection(app->config_options.clang_format_path, command_context.view,
                                   app->vim.mode, &app->visual)){
         return CE_COMMAND_FAILURE;
     }
     app->vim.mode = CE_VIM_MODE_NORMAL;
     return CE_COMMAND_SUCCESS;
}

void buffer_replace_all(CeBuffer_t* buffer, CePoint_t cursor, const char* match, const char* replacement, CePoint_t start, CePoint_t end,
                        bool regex_search){
     bool chain_undo = false;
     int64_t match_len = 0;
#if !defined(PLATFORM_WINDOWS)
     CeRegex_t regex = NULL;
#endif

     if(regex_search){
#if defined(PLATFORM_WINDOWS)
          return;
#else
          CeRegexResult_t regex_result = ce_regex_init(match,
                                                       &regex);
          if(regex_result.error_message != NULL){
               ce_log("ce_regex_init() failed: '%s'", regex_result.error_message);
               free(regex_result.error_message);
               return;
          }
#endif
     }else{
           match_len = strlen(match);
     }

     while(true){
          CePoint_t match_point;

          // find the match
          if(regex_search){
#if defined(PLATFORM_WINDOWS)
               return;
#else
               CeRegexSearchResult_t result = ce_buffer_regex_search_forward(buffer, start, regex);
               match_point = result.point;
               match_len = result.length;
               break;
#endif
          }else{
               match_point = ce_buffer_search_forward(buffer, start, match);
          }

          if(match_point.x < 0) break;
          if(ce_point_after(match_point, end)) break;

          ce_buffer_remove_string_change(buffer, match_point, match_len, &cursor, cursor, chain_undo);
          chain_undo = true;

          ce_buffer_insert_string_change(buffer, strdup(replacement), match_point, &cursor, cursor, chain_undo);

          start = ce_buffer_advance_point(buffer, match_point, ce_utf8_strlen(replacement));
     }
#if !defined(PLATFORM_WINDOWS)
     ce_regex_free(regex);
#endif
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

     if(load_file_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                         true, command->args[0].string)){
          ce_clangd_file_open(&app->clangd, app->buffer_node_head->buffer);
     }else{
          return CE_COMMAND_FAILURE;
     }

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
          if(load_file_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                              true, command->args[0].string)){
               ce_clangd_file_open(&app->clangd, app->buffer_node_head->buffer);
          }else{
               return CE_COMMAND_FAILURE;
          }
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
          if(load_file_into_view(&app->buffer_node_head, command_context.view, &app->config_options, &app->vim,
                              true, command->args[0].string)){
               ce_clangd_file_open(&app->clangd, app->buffer_node_head->buffer);
          }else{
               return CE_COMMAND_FAILURE;
          }
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
          ce_app_run_shell_command(app, "make", command_context.tab_layout, command_context.view, false);
     }else if(command->arg_count == 1 &&
              command->args[0].type == CE_COMMAND_ARG_STRING){
          CommandContext_t command_context = {};
          if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;
          char command_string[256];
          snprintf(command_string, 256, "make %s", command->args[0].string);
          ce_app_run_shell_command(app, command_string, command_context.tab_layout, command_context.view, false);
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

static void open_file_in_dir_recursively(char* path, char* match, CeApp_t* app, CeView_t* view){
     CeListDirResult_t list_dir_result = ce_list_dir(path);

#if defined(PLATFORM_WINDOWS)
     int64_t path_len = strnlen(path, MAX_PATH_LEN);
     if(path_len > 0 && path_len < MAX_PATH_LEN){
         if(path[path_len - 1] == CE_CURRENT_DIR_SEARCH){
             path[path_len - 1] = '.';
         }
     }
#endif

     for(int64_t i = 0; i < list_dir_result.count; i++){
          if(list_dir_result.is_directories[i] &&
             strcmp(list_dir_result.filenames[i], ".") != 0 &&
             strcmp(list_dir_result.filenames[i], "..") != 0){
               char next_dir[MAX_PATH_LEN];
               snprintf(next_dir, MAX_PATH_LEN, "%s%c%s%c%c", path, CE_PATH_SEPARATOR, list_dir_result.filenames[i],
                        CE_PATH_SEPARATOR, CE_CURRENT_DIR_SEARCH);
               open_file_in_dir_recursively(next_dir, match, app, view);
          }else if(strcmp(list_dir_result.filenames[i], match) == 0){
               char full_path[MAX_PATH_LEN];
               snprintf(full_path, MAX_PATH_LEN, "%s%c%s", path, CE_PATH_SEPARATOR, list_dir_result.filenames[i]);
               if(load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim,
                                   true, full_path)){
                    ce_clangd_file_open(&app->clangd, app->buffer_node_head->buffer);
               }
          }
     }
     ce_free_list_dir_result(&list_dir_result);
}

CeCommandStatus_t command_vim_find(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     CeApp_t* app = user_data;
     CommandContext_t command_context = {};

     if(!get_command_context(app, &command_context)) return CE_COMMAND_NO_ACTION;

     char* base_directory = buffer_base_directory(command_context.view->buffer);
     if(!base_directory) base_directory = strdup(CE_CURRENT_DIR_SEARCH_STR);
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
