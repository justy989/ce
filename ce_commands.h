#pragma once

#include "ce_app.h"

#define UNSAVED_BUFFERS_DIALOGUE "UNSAVED BUFFERS, QUIT? [Y/N]"

CeCommandStatus_t command_quit(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_select_adjacent_layout(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_save_buffer(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_show_buffers(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_show_yanks(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_split_layout(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_select_parent_layout(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_delete_layout(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_load_file(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_new_tab(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_select_adjacent_tab(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_search(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_noh(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_regex_search(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_command(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_switch_to_terminal(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_switch_buffer(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_redraw(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_goto_destination_in_line(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_goto_next_destination(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_goto_prev_destination(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_replace_all(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_reload_file(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_reload_config(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_buffer_type(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_new_buffer(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_rename_buffer(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_jump_list(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_line_number(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_terminal_command(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_terminal_command_in_view(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_man_page_on_word_under_cursor(CeCommand_t* command, void* user_data);
