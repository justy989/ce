#pragma once

#include "ce_app.h"

#define UNSAVED_BUFFERS_DIALOGUE "Unsaved buffers, quit? [y/n]"
#define BUFFER_MODIFIED_OUTSIDE_EDITOR "Buffer modified outside editor, save anyway? [y/n]"

CeCommandStatus_t command_add_cursor(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_clear_cursors(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_toggle_cursors_active(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_blank(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_quit(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_select_adjacent_layout(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_save_buffer(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_save_all_and_quit(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_show_buffers(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_show_yanks(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_show_macros(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_show_marks(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_show_jumps(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_split_layout(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_select_parent_layout(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_delete_layout(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_load_file(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_load_project_file(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_new_tab(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_select_adjacent_tab(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_search(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_noh(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_setpaste(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_setnopaste(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_regex_search(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_command(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_switch_buffer(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_redraw(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_goto_destination_in_line(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_goto_next_destination(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_goto_prev_destination(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_goto_prev_buffer_in_view(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_replace_all(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_reload_file(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_reload_config(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_syntax(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_new_buffer(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_rename_buffer(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_jump_list(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_line_number(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_man_page_on_word_under_cursor(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_shell_command(CeCommand_t* command, void* user_data);

// NOTE: these are to make vim users feel at home
CeCommandStatus_t command_vim_e(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_w(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_wq(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_q(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_wqa(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_q_exclam(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_qa_exclam(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_xa(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_vsp(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_sp(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_sp(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_tabnew(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_tabnext(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_tabprevious(CeCommand_t* command, void* user_data);
// CeCommandStatus_t command_vim_tabclose(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_cn(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_cp(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_make(CeCommand_t* command, void* user_data);
CeCommandStatus_t command_vim_find(CeCommand_t* command, void* user_data);

#ifdef ENABLE_DEBUG_KEY_PRESS_INFO
CeCommandStatus_t command_toggle_log_keys_pressed(CeCommand_t* command, void* user_data);
#endif
