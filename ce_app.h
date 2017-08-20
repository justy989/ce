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

typedef struct BufferNode_t{
     CeBuffer_t* buffer;
     struct BufferNode_t* next;
}BufferNode_t;

typedef struct StringNode_t{
     char* string;
     struct StringNode_t* next;
     struct StringNode_t* prev;
}StringNode_t;

typedef struct{
     StringNode_t* head;
     StringNode_t* current;
}History_t;

typedef struct{
     int* keys;
     int64_t key_count;
     CeCommand_t command;
     CeVimMode_t vim_mode;
}KeyBind_t;

typedef struct{
     KeyBind_t* binds;
     int64_t count;
}KeyBinds_t;

typedef struct{
     int keys[4];
     const char* command;
}KeyBindDef_t;

typedef struct{
     CeVimBufferData_t vim;
     int64_t last_goto_destination; // TODO: use
}BufferUserData_t;

struct App_t;

typedef bool CeUserConfigFunc(struct App_t*);

typedef struct{
     void* handle;
     char* filepath;
     CeUserConfigFunc* init_func;
     CeUserConfigFunc* free_func;
}UserConfig_t;

typedef struct App_t{
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
     BufferNode_t* buffer_node_head;
     CeCommandEntry_t* command_entries;
     int64_t command_entry_count;
     CeVimParseResult_t last_vim_handle_result;
     CeBuffer_t* buffer_list_buffer;
     CeBuffer_t* yank_list_buffer;
     CeBuffer_t* complete_list_buffer;
     CeBuffer_t* macro_list_buffer;
     CeBuffer_t* mark_list_buffer;
     CeComplete_t command_complete;
     CeComplete_t load_file_complete;
     CeComplete_t switch_buffer_complete;
     History_t command_history;
     KeyBinds_t key_binds[CE_VIM_MODE_COUNT];
     CeRune_t keys[APP_MAX_KEY_COUNT];
     int64_t key_count;
     char edit_register;
     CeTerminal_t terminal;
     CeMacros_t macros;
     CePoint_t search_start;
     void* user_config_data;
     bool record_macro;
     bool replay_macro;
     bool ready_to_draw;
     bool quit;
     UserConfig_t user_config;
}App_t;

bool buffer_node_insert(BufferNode_t** head, CeBuffer_t* buffer);
bool buffer_node_delete(BufferNode_t** head, CeBuffer_t* buffer);
void buffer_node_free(BufferNode_t** head);

StringNode_t* string_node_insert(StringNode_t** head, const char* string);
void string_node_free(StringNode_t** head);

bool history_insert(History_t* history, const char* string);
char* history_previous(History_t* history);
char* history_next(History_t* history);

void convert_bind_defs(KeyBinds_t* binds, KeyBindDef_t* bind_defs, int64_t bind_def_count);
void set_vim_key_bind(CeVimKeyBind_t* key_binds, int64_t* key_bind_count, CeRune_t key, CeVimParseFunc_t* parse_func);
void extend_commands(CeCommandEntry_t** command_entries, int64_t* command_entry_count, CeCommandEntry_t* new_command_entries,
                     int64_t new_command_entry_count);

void app_update_terminal_view(App_t* app);
CeComplete_t* app_is_completing(App_t* app);
