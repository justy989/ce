#pragma once

#include "ce.h"
#include "ce_command.h"
#include "ce_vim.h"
#include "ce_layout.h"
#include "ce_syntax.h"
#include "ce_terminal.h"
#include "ce_complete.h"
#include "ce_macros.h"

#define APP_MAX_KEY_COUNT 16
#define JUMP_LIST_COUNT 16

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
}History_t;

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
     CeVimBufferData_t vim;
     int64_t last_goto_destination;
     CeSyntaxHighlightFunc_t* syntax_function;
}CeAppBufferData_t;

typedef struct{
     CeDestination_t destinations[JUMP_LIST_COUNT];
     int64_t count;
     int64_t current;
}CeJumpList_t;

struct CeApp_t;

typedef bool CeUserConfigFunc(struct CeApp_t*);

typedef struct{
     void* handle;
     char* filepath;
     CeUserConfigFunc* init_func;
     CeUserConfigFunc* free_func;
}CeUserConfig_t;

typedef struct CeApp_t{
     CeRect_t terminal_rect;
     CeVim_t vim;
     CeConfigOptions_t config_options;
     int terminal_width;
     int terminal_height;
     CeView_t input_view;
     CeView_t complete_view;
     bool input_mode;
     CeLayout_t* tab_list_layout;
     CeSyntaxDef_t* syntax_defs;
     CeBufferNode_t* buffer_node_head;
     CeCommandEntry_t* command_entries;
     int64_t command_entry_count;
     CeVimParseResult_t last_vim_handle_result;
     CeBuffer_t* buffer_list_buffer;
     CeBuffer_t* yank_list_buffer;
     CeBuffer_t* complete_list_buffer;
     CeBuffer_t* macro_list_buffer;
     CeBuffer_t* mark_list_buffer;
     CeBuffer_t* last_goto_buffer;
     CeComplete_t command_complete;
     CeComplete_t load_file_complete;
     CeComplete_t switch_buffer_complete;
     History_t command_history;
     CeKeyBinds_t key_binds;
     CeRune_t keys[APP_MAX_KEY_COUNT];
     int64_t key_count;
     char edit_register;
     CeTerminal_t terminal;
     CeMacros_t macros;
     CePoint_t search_start;
     CeJumpList_t jump_list;
     void* user_config_data;
     bool record_macro;
     bool replay_macro;
     bool ready_to_draw;
     bool quit;
     bool highlight_search;
     pthread_mutex_t draw_lock;
     CeUserConfig_t user_config;
}CeApp_t;

bool ce_buffer_node_insert(CeBufferNode_t** head, CeBuffer_t* buffer);
bool ce_buffer_node_delete(CeBufferNode_t** head, CeBuffer_t* buffer);
void ce_buffer_node_free(CeBufferNode_t** head);

CeStringNode_t* ce_string_node_insert(CeStringNode_t** head, const char* string);
void ce_string_node_free(CeStringNode_t** head);

bool ce_history_insert(History_t* history, const char* string);
char* ce_history_previous(History_t* history);
char* ce_history_next(History_t* history);

void ce_convert_bind_defs(CeKeyBinds_t* binds, CeKeyBindDef_t* bind_defs, int64_t bind_def_count);
void ce_set_vim_key_bind(CeVimKeyBind_t* key_binds, int64_t* key_bind_count, CeRune_t key, CeVimParseFunc_t* parse_func);
void ce_extend_commands(CeCommandEntry_t** command_entries, int64_t* command_entry_count, CeCommandEntry_t* new_command_entries,
                     int64_t new_command_entry_count);

void ce_app_update_terminal_view(CeApp_t* app);
CeComplete_t* ce_app_is_completing(CeApp_t* app);

void ce_syntax_highlight_terminal(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                  CeSyntaxDef_t* syntax_defs, void* user_data);
void ce_syntax_highlight_completions(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                     CeSyntaxDef_t* syntax_defs, void* user_data);

bool ce_jump_list_insert(CeJumpList_t* jump_list, CeDestination_t destination);
CeDestination_t* ce_jump_list_previous(CeJumpList_t* jump_list);
CeDestination_t* ce_jump_list_next(CeJumpList_t* jump_list);

void ce_view_switch_buffer(CeView_t* view, CeBuffer_t* buffer, CeVim_t* vim, CeConfigOptions_t* config_options);
void ce_run_command_in_terminal(CeTerminal_t* terminal, const char* command);
CeView_t* ce_switch_to_terminal(CeApp_t* app, CeView_t* view, CeLayout_t* tab_layout);
