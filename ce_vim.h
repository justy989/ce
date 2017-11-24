#pragma once

#include "ce.h"

#define CE_VIM_MAX_COMMAND_LEN 16
#define CE_VIM_MAX_KEY_BINDS 256

#define CE_VIM_DECLARE_MOTION_FUNC(function_name)                                                              \
CeVimMotionResult_t function_name(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,               \
                                  const CeConfigOptions_t* config_options, CeVimBufferData_t* buffer_data, \
                                  CeRange_t* motion_range);

#define CE_VIM_DECLARE_VERB_FUNC(function_name)                                                       \
bool function_name(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view, \
                   CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options);


typedef enum{
     CE_VIM_MOTION_RESULT_FAIL,
     CE_VIM_MOTION_RESULT_SUCCESS,
     CE_VIM_MOTION_RESULT_SUCCESS_NO_MULTIPLY,
}CeVimMotionResult_t;

struct CeVimAction_t;
struct CeVim_t;
struct CeVimBufferData_t;
enum CeVimParseResult_t;

typedef enum CeVimParseResult_t CeVimParseFunc_t(struct CeVimAction_t*, CeRune_t);
typedef CeVimMotionResult_t CeVimMotionFunc_t(struct CeVim_t*, struct CeVimAction_t*, const CeView_t*,
                                              const CeConfigOptions_t*, struct CeVimBufferData_t*, CeRange_t*);
typedef bool CeVimVerbFunc_t(struct CeVim_t*, const struct CeVimAction_t*, CeRange_t, CeView_t*,
                             struct CeVimBufferData_t*, const CeConfigOptions_t*);

typedef enum CeVimParseResult_t{
     CE_VIM_PARSE_INVALID,
     CE_VIM_PARSE_KEY_NOT_HANDLED,
     CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY, // send the next key to the same key bind
     CE_VIM_PARSE_CONTINUE,
     CE_VIM_PARSE_IN_PROGRESS,
     CE_VIM_PARSE_COMPLETE,
}CeVimParseResult_t;

typedef enum{
     CE_VIM_MODE_NORMAL,
     CE_VIM_MODE_INSERT,
     CE_VIM_MODE_VISUAL,
     CE_VIM_MODE_VISUAL_LINE,
     CE_VIM_MODE_VISUAL_BLOCK,
     CE_VIM_MODE_REPLACE,
     CE_VIM_MODE_COUNT,
}CeVimMode_t;

typedef enum{
     CE_VIM_YANK_TYPE_STRING,
     CE_VIM_YANK_TYPE_LINE,
     CE_VIM_YANK_TYPE_BLOCK,
}CeVimYankType_t;

typedef struct{
     CeVimYankType_t type;
     union{
          char* text;
          char** block;
     };
     int64_t block_line_count;
}CeVimYank_t;

typedef struct{
     int64_t multiplier;
     CeVimMotionFunc_t* function;
     union{
          char character;
          int integer;
          double decimal;
     };
     bool cursor_at_end_of_selection; // opposite is where the cursor is at the beginning of the selection
}CeVimMotion_t;

typedef struct{
     CeVimVerbFunc_t* function;
     union{
          char character;
          int integer;
          double decimal;
          char* string;
     };
}CeVimVerb_t;

typedef struct CeVimAction_t{
     int64_t multiplier;
     CeVimMotion_t motion;
     CeVimVerb_t verb;
     // NOTE: after enough bools, should we just make some bit flags?
     CeVimYankType_t yank_type; // TODO: consider rename as more than just yanking looks at this
     bool chain_undo;
     bool repeatable;
     bool exclude_end;
     bool do_not_yank;
     CeClampX_t clamp_x;
}CeVimAction_t;

typedef struct{
     CeRune_t key;
     CeVimParseFunc_t* function;
}CeVimKeyBind_t;

typedef enum{
     CE_VIM_SEARCH_MODE_FORWARD,
     CE_VIM_SEARCH_MODE_BACKWARD,
     CE_VIM_SEARCH_MODE_REGEX_FORWARD,
     CE_VIM_SEARCH_MODE_REGEX_BACKWARD,
}CeVimSearchMode_t;

typedef struct CeVimBufferData_t{
     CePoint_t marks[CE_ASCII_PRINTABLE_CHARACTERS];
     int64_t motion_column;
}CeVimBufferData_t;

typedef enum{
     CE_VIM_FIND_CHAR_STATE_FIND_FORWARD,
     CE_VIM_FIND_CHAR_STATE_FIND_BACKWARD,
     CE_VIM_FIND_CHAR_STATE_UNTIL_FORWARD,
     CE_VIM_FIND_CHAR_STATE_UNTIL_BACKWARD,
}CeVimFindCharState_t;

typedef struct{
     CeRune_t rune;
     CeVimFindCharState_t state;
}CeVimFindChar_t;

typedef struct CeVim_t{
     CeVimMode_t mode;
     CeVimKeyBind_t key_binds[CE_VIM_MAX_KEY_BINDS];
     int64_t key_bind_count;
     CeRune_t current_command[CE_VIM_MAX_COMMAND_LEN];
     CeVimYank_t yanks[CE_ASCII_PRINTABLE_CHARACTERS];
     int64_t motion_column;
     CePoint_t visual;
     CeVimAction_t last_action;
     CeVimAction_t current_action;
     CeRuneNode_t* insert_rune_head;
     CeRuneNode_t* last_insert_rune_head;
     bool chain_undo;
     bool verb_last_action; // flag whether or not we are repeating our last action
     bool pasting;
     CePoint_t visual_block_top_left;
     CePoint_t visual_block_bottom_right;
     CeVimSearchMode_t search_mode;
     CeVimFindChar_t find_char;
}CeVim_t;

bool ce_vim_init(CeVim_t* vim); // sets up default keybindings that can be overriden
bool ce_vim_free(CeVim_t* vim);
bool ce_vim_rebind(CeVim_t* vim, CeRune_t key, CeVimParseFunc_t function);
CeVimParseResult_t ce_vim_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, CeVimBufferData_t* buffer_data,
                                     const CeConfigOptions_t* config_options);

bool vim_mode_is_visual(CeVimMode_t mode);

// action
CeVimParseResult_t ce_vim_parse_action(CeVimAction_t* action, const CeRune_t* keys, CeVimKeyBind_t* key_binds,
                                       int64_t key_bind_count, CeVimMode_t vim_mode);
bool ce_vim_apply_action(CeVim_t* vim, CeVimAction_t* action, CeView_t* view, CeVimBufferData_t* buffer_data,
                         const CeConfigOptions_t* config_options);
bool ce_vim_append_key(CeVim_t* vim, CeRune_t key);

// util
int64_t ce_vim_register_index(CeRune_t rune);
void ce_vim_yank_free(CeVimYank_t* yank);

// TODO: add const, since most of these are just readonly
int64_t ce_vim_soft_begin_line(CeBuffer_t* buffer, int64_t line); // returns -1
CePoint_t ce_vim_move_little_word(CeBuffer_t* buffer, CePoint_t start); // returns -1, -1 on error
CePoint_t ce_vim_move_big_word(CeBuffer_t* buffer, CePoint_t start);
CePoint_t ce_vim_move_end_little_word(CeBuffer_t* buffer, CePoint_t start);
CePoint_t ce_vim_move_end_big_word(CeBuffer_t* buffer, CePoint_t start);
CePoint_t ce_vim_move_begin_little_word(CeBuffer_t* buffer, CePoint_t start);
CePoint_t ce_vim_move_begin_big_word(CeBuffer_t* buffer, CePoint_t start);
CePoint_t ce_vim_move_find_rune_forward(CeBuffer_t* buffer, CePoint_t start, CeRune_t rune, bool until);
CePoint_t ce_vim_move_find_rune_backward(CeBuffer_t* buffer, CePoint_t start, CeRune_t rune, bool until);
CeRange_t ce_vim_find_little_word_boundaries(CeBuffer_t* buffer, CePoint_t start); // returns -1
CeRange_t ce_vim_find_big_word_boundaries(CeBuffer_t* buffer, CePoint_t start); // returns -1
CeRange_t ce_vim_find_pair(CeBuffer_t* buffer, CePoint_t start, CeRune_t rune, bool inside, int level);
void ce_vim_add_key_bind(CeVimKeyBind_t* key_binds, int64_t* key_bind_count, CeRune_t key, CeVimParseFunc_t* function);
int64_t ce_vim_get_indentation(CeBuffer_t* buffer, CePoint_t point, int64_t tab_length);
bool ce_vim_join_next_line(CeBuffer_t* buffer, int64_t line, CePoint_t cursor, bool chain_undo);

// parse functions
CeVimParseResult_t ce_vim_parse_motion_left(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_right(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_up(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_down(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_little_word(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_big_word(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_end_little_word(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_end_big_word(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_begin_little_word(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_begin_big_word(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_soft_begin_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_hard_begin_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_end_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_page_down(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_page_up(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_half_page_down(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_half_page_up(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_visual(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_find_forward(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_find_backward(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_until_forward(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_until_backward(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_next_find_char(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_prev_find_char(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_inside(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_around(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_end_of_file(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_search_next(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_search_prev(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_search_word_forward(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_search_word_backward(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_match_pair(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_mark(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_mark_soft_begin_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_top_of_view(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_middle_of_view(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_bottom_of_view(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_next_blank_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_previous_blank_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_next_zero_indentation_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_motion_previous_zero_indentation_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_delete(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_delete_to_end_of_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_change(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_change_to_end_of_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_set_character(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_delete_character(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_substitute_character(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_substitute_soft_begin_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_yank(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_yank_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_paste_before(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_paste_after(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_open_above(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_open_below(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_undo(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_redo(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_last_action(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_insert_mode(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_replace_mode(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_visual_mode(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_visual_line_mode(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_normal_mode(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_append(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_append_at_end_of_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_insert_at_soft_begin_line(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_select_yank_register(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_z_command(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_g_command(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_indent(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_unindent(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_join(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_flip_case(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_set_mark(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_increment_number(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_decrement_number(CeVimAction_t* action, CeRune_t key);

// motion functions
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_left);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_right);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_up);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_down);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_little_word);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_big_word);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_end_little_word);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_end_big_word);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_begin_little_word);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_begin_big_word);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_soft_begin_line);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_hard_begin_line);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_end_line);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_entire_line);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_page_up);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_page_down);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_half_page_up);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_half_page_down);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_visual);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_find_forward);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_find_backward);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_until_forward);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_until_backward);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_next_find_char);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_prev_find_char);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_inside);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_around);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_end_of_file);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_search_next);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_search_prev);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_search_word_forward);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_search_word_backward);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_match_pair);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_mark);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_mark_soft_begin_line);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_top_of_view);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_middle_of_view);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_bottom_of_view);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_next_blank_line);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_previous_blank_line);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_next_zero_indentation_line);
CE_VIM_DECLARE_MOTION_FUNC(ce_vim_motion_previous_zero_indentation_line);

// verb functions
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_motion);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_delete);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_change);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_set_character);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_delete_character);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_substitute_character);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_substitute_soft_begin_line);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_yank);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_paste_before);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_paste_after);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_open_above);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_open_below);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_undo);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_redo);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_insert_mode);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_replace_mode);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_visual_mode);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_visual_line_mode);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_normal_mode);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_append);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_append_at_end_of_line);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_insert_at_soft_begin_line);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_last_action);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_z_command);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_g_command);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_indent);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_unindent);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_join);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_flip_case);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_set_mark);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_increment_number);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_decrement_number);
