#pragma once

#include "ce.h"
#include "ce_clangd.h"
#include "ce_command.h"
#include "ce_complete.h"
#include "ce_layout.h"
#include "ce_macros.h"
#include "ce_syntax.h"
#include "ce_vim.h"

#include <time.h>

#if defined(PLATFORM_WINDOWS)
    #include <handleapi.h>
#else
    #include <pthread.h>
#endif

#define ENABLE_DEBUG_KEY_PRESS_INFO

#define APP_MAX_KEY_COUNT 16
#define JUMP_LIST_DESTINATION_COUNT 16

typedef struct CeBufferNode_t{
     CeBuffer_t* buffer;
     struct CeBufferNode_t* next;
}CeBufferNode_t;

typedef struct CeStringNode_t{
     char* string;
     struct CeStringNode_t* next;
     struct CeStringNode_t* prev;
}CeStringNode_t;

typedef struct{
     CeStringNode_t* head;
     CeStringNode_t* current;
}CeHistory_t;

typedef struct{
     int* keys;
     int64_t key_count;
     CeCommand_t command;
     CeVimMode_t vim_mode;
}CeKeyBind_t;

typedef struct{
     CeKeyBind_t* binds;
     int64_t count;
}CeKeyBinds_t;

typedef struct{
     int keys[4];
     const char* command;
}CeKeyBindDef_t;

typedef struct{
     CeDestination_t destinations[JUMP_LIST_DESTINATION_COUNT];
     int64_t count;
     int64_t itr;
     int64_t current;
}CeJumpList_t;

typedef struct{
     CeVimBufferData_t vim;
     int64_t last_goto_destination;
     CeSyntaxHighlightFunc_t* syntax_function;
     char* base_directory;
     CeClangDDiagnostics_t clangd_diagnostics;
}CeAppBufferData_t;

typedef struct{
     CeJumpList_t jump_list;
     CeBuffer_t* prev_buffer;
}CeAppViewData_t;

struct CeApp_t;

#if defined(PLATFORM_WINDOWS)
typedef bool (WINAPI CeUserConfigFunc)(struct CeApp_t*);
#else
typedef bool CeUserConfigFunc(struct CeApp_t*);
#endif

typedef bool CeOnSaveFunc(struct CeApp_t*, CeBuffer_t*);

typedef struct{
#if defined(PLATFORM_WINDOWS)
     HINSTANCE library_instance;
#else
     void* handle;
#endif
     char* filepath;
     CeUserConfigFunc* init_func;
     CeUserConfigFunc* free_func;
     CeOnSaveFunc* save_func;
}CeUserConfig_t;

typedef struct{
     CeVimMode_t mode;
     CePoint_t visual_point;
}CeVimVisualSave_t;

typedef struct{
     CePoint_t start;
     CePoint_t initiate;
     CeComplete_t* complete;
     CeBuffer_t* buffer;
     CeView_t view;
}CeClangDCompletion_t;

typedef bool CeInputCompleteFunc(struct CeApp_t*, CeBuffer_t* input_buffer);

typedef struct CeApp_t{
     CeRect_t terminal_rect;
     CeVim_t vim;
     CeVimVisualSave_t vim_visual_save;
     CeVimVisualData_t visual;
     CeConfigOptions_t config_options;
     int terminal_width;
     int terminal_height;
     CeView_t input_view;
     CeView_t message_view;
     CeView_t complete_view;
     CeInputCompleteFunc* input_complete_func;
     bool message_mode;
     struct timespec message_time;
     CeLayout_t* tab_list_layout;
     CeSyntaxDef_t* syntax_defs;
     CeBufferNode_t* buffer_node_head;
     CeCommandEntry_t* command_entries;
     int64_t command_entry_count;
     CeVimParseResult_t last_vim_handle_result;
     CeBuffer_t* buffer_list_buffer;
     CeBuffer_t* bind_list_buffer;
     CeBuffer_t* yank_list_buffer;
     CeBuffer_t* complete_list_buffer;
     CeBuffer_t* macro_list_buffer;
     CeBuffer_t* mark_list_buffer;
     CeBuffer_t* jump_list_buffer;
     CeBuffer_t* shell_command_buffer;
     CeBuffer_t* last_goto_buffer;
     CeBuffer_t* clangd_diagnostics_buffer;
     CeBuffer_t* clangd_references_buffer;
     CeComplete_t input_complete;
     CeHistory_t command_history;
     CeHistory_t search_history;
     CeKeyBinds_t key_binds;
     CeRune_t keys[APP_MAX_KEY_COUNT];
     int64_t key_count;
     char edit_register;
     CeMacros_t macros;
     CePoint_t search_start;
     void* user_config_data;
     bool record_macro;
     bool replay_macro;
     int64_t macro_multiplier;
     char last_macro_register;
     int64_t last_macro_multiplier;
     bool quit;
     bool highlight_search;
     CeUserConfig_t user_config;

#if defined(PLATFORM_WINDOWS)
     HANDLE shell_command_thread;
     DWORD shell_command_thread_id;
#else
     pthread_t shell_command_thread;
#endif

     int64_t discovered_filepath_count;
     char** discovered_filepaths;

     bool shell_command_buffer_should_scroll;
     bool shell_command_thread_should_die;

     void* gui;

     CeClangD_t clangd;
     CeClangDCompletion_t clangd_completion;

     CeBuffer_t* last_popup_buffer;

     // debug
     bool log_key_presses;
}CeApp_t;

bool ce_buffer_node_insert(CeBufferNode_t** head, CeBuffer_t* buffer);
CeBufferNode_t* ce_buffer_node_unlink(CeBufferNode_t** head, CeBuffer_t* buffer);
bool ce_buffer_node_delete(CeBufferNode_t** head, CeBuffer_t* buffer);
void ce_buffer_node_free(CeBufferNode_t** head);

CeStringNode_t* ce_string_node_insert(CeStringNode_t** head, const char* string);
void ce_string_node_free(CeStringNode_t** head);

bool ce_history_insert(CeHistory_t* history, const char* string);
char* ce_history_previous(CeHistory_t* history);
char* ce_history_next(CeHistory_t* history);
void ce_history_reset_current(CeHistory_t* history);
void ce_history_free(CeHistory_t* history);

void ce_convert_bind_defs(CeKeyBinds_t* binds, CeKeyBindDef_t* bind_defs, int64_t bind_def_count);
void ce_set_vim_key_bind(CeVimKeyBind_t* key_binds, int64_t* key_bind_count, CeRune_t key, CeVimParseFunc_t* parse_func);
void ce_extend_commands(CeCommandEntry_t** command_entries, int64_t* command_entry_count, CeCommandEntry_t* new_command_entries,
                     int64_t new_command_entry_count);

CeComplete_t* ce_app_is_completing(CeApp_t* app);

void ce_syntax_highlight_completions(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                     CeSyntaxDef_t* syntax_defs, void* user_data);
void ce_syntax_highlight_message(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                 CeSyntaxDef_t* syntax_defs, void* user_data);

void ce_jump_list_insert(CeJumpList_t* jump_list, CeDestination_t destination);
CeDestination_t* ce_jump_list_previous(CeJumpList_t* jump_list);
CeDestination_t* ce_jump_list_next(CeJumpList_t* jump_list);
CeDestination_t* ce_jump_list_current(CeJumpList_t* jump_list);

void ce_view_switch_buffer(CeView_t* view, CeBuffer_t* buffer, CeVim_t* vim,
                           CeConfigOptions_t* config_options, bool insert_into_jump_list);

void input_view_overlay(CeView_t* input_view, CeView_t* view);
CePoint_t view_cursor_on_screen(CeView_t* view, int64_t tab_width, CeLineNumber_t line_number);
CeBuffer_t* load_file_into_view(CeBufferNode_t** buffer_node_head, CeView_t* view,
                                CeConfigOptions_t* config_options, CeVim_t* vim,
                                bool insert_into_jump_list, const char* filepath);
CeBuffer_t* new_buffer();
void determine_buffer_syntax(CeBuffer_t* buffer);
char* buffer_base_directory(CeBuffer_t* buffer);
void complete_files(CeComplete_t* complete, const char* line, const char* base_directory);
void build_complete_list(CeBuffer_t* buffer, CeComplete_t* complete);
bool buffer_append_on_new_line(CeBuffer_t* buffer, const char* string);
CeDestination_t scan_line_for_destination(const char* line);
void replace_all(CeView_t* view, CeVimVisualSave_t* vim_visual_save, const char* match, const char* replace);

bool user_config_init(CeUserConfig_t* user_config, const char* filepath);
void user_config_free(CeUserConfig_t* user_config);

int64_t istrtol(const CeRune_t* istr, const CeRune_t** end_of_numbers);
int64_t istrlen(const CeRune_t* istr);

bool ce_destination_in_view(CeDestination_t* destination, CeView_t* view);

void ce_app_clear_filepath_cache(CeApp_t* app);

void ce_app_update_terminal_view(CeApp_t* app, int width, int height);

void ce_app_init_default_commands(CeApp_t* app);
void ce_app_init_command_completion(CeApp_t* app, CeComplete_t* complete);
void ce_app_message(CeApp_t* app, const char* fmt, ...);
void ce_app_input(CeApp_t* app, const char* dialogue, CeInputCompleteFunc* input_complete_func);

bool apply_completion_to_buffer(CeComplete_t* complete,
                                CeBuffer_t* buffer,
                                int64_t start_x,
                                CePoint_t* cursor);

void ce_app_handle_clangd_response(CeApp_t* app);
void build_clangd_completion_view(CeView_t* view,
                                  CePoint_t start,
                                  CeView_t* completed_view,
                                  CeBuffer_t* buffer,
                                  CeConfigOptions_t* config_options,
                                  CeRect_t* terminal_rect);
void build_clangd_diagnostics_buffer(CeBuffer_t* buffer,
                                     CeBuffer_t* source);

bool command_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);
bool load_file_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);
bool load_project_file_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);
bool search_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);
bool switch_buffer_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);
bool replace_all_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);
bool edit_yank_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);
bool edit_macro_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);
bool unsaved_buffers_input_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);
bool buffer_modified_outside_editor_complete_func(CeApp_t* app, CeBuffer_t* input_buffer);

bool ce_app_switch_to_prev_buffer_in_view(CeApp_t* app, CeView_t* view, bool switch_if_deleted);
bool ce_app_run_shell_command(CeApp_t* app, const char* command, CeLayout_t* tab_layout, CeView_t* view, bool relative);

bool ce_clang_format_buffer(char* clang_format_exe, CeBuffer_t* buffer, CePoint_t cursor);
bool ce_clang_format_selection(char* clang_format_exe, CeView_t* view, CeVimMode_t vim_mode, CeVimVisualData_t* visual);

bool ce_app_open_popup_view(CeApp_t* app, CeBuffer_t* buffer);
bool ce_app_close_popup_view(CeApp_t* app);

CeBuffer_t* load_destination_into_view(CeBufferNode_t** buffer_node_head, CeView_t* view, CeConfigOptions_t* config_options,
                                       CeVim_t* vim, bool insert_into_jump_list,
                                       const char* base_directory, CeDestination_t* destination);

bool ce_get_cwd(char* buffer, size_t size);

// Returns the point at the end of the paste if text was pasted.
CePoint_t ce_paste_clipboard_into_buffer(CeBuffer_t* buffer, CePoint_t point);
bool ce_set_clipboard_from_buffer(CeBuffer_t* buffer, CePoint_t start, CePoint_t end);

extern int g_shell_command_ready_fds[2];
