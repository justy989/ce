#pragma once

#include "ce.h"

#define ASCII_PRINTABLE_CHARACTERS 95

#define CE_VIM_MAX_COMMAND_LEN 16
#define CE_VIM_MAX_KEY_BINDS 256

#define CE_VIM_DECLARE_MOTION_FUNC(function_name)                                              \
bool function_name(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,            \
                   const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range);

#define CE_VIM_DECLARE_VERB_FUNC(function_name)                                                                \
bool function_name(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view, \
                   const CeConfigOptions_t* config_options);


struct CeVimAction_t;
struct CeVim_t;
struct CeVimMotionRange_t;
enum CeVimParseResult_t;

typedef enum CeVimParseResult_t CeVimParseFunc_t(struct CeVimAction_t*, CeRune_t key);
typedef bool CeVimMotionFunc_t(const struct CeVim_t*, struct CeVimAction_t*, const CeView_t*,
                               const CeConfigOptions_t*, struct CeVimMotionRange_t*);
typedef bool CeVimVerbFunc_t(struct CeVim_t*, const struct CeVimAction_t*, struct CeVimMotionRange_t, CeView_t*,
                             const CeConfigOptions_t*);

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
}CeVimMode_t;

typedef struct CeVimMotionRange_t{
     CePoint_t start;
     CePoint_t end;
}CeVimMotionRange_t;

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
     bool yank_line; // TODO: consider rename as more than just yanking looks at this
     bool chain_undo;
}CeVimAction_t;

typedef struct{
     CeRune_t key;
     CeVimParseFunc_t* function;
}CeVimKeyBind_t;

typedef struct{
     char* text;
     bool line;
}CeVimYank_t;

typedef struct CeVim_t{
     CeVimMode_t mode;
     CeVimKeyBind_t key_binds[CE_VIM_MAX_KEY_BINDS];
     int64_t key_bind_count;
     CeRune_t current_command[CE_VIM_MAX_COMMAND_LEN];
     bool chain_undo;
     int64_t motion_column;
     CeVimYank_t yanks[ASCII_PRINTABLE_CHARACTERS];
     CePoint_t visual;
     CeVimAction_t last_action;
}CeVim_t;

bool ce_vim_init(CeVim_t* vim); // sets up default keybindings that can be overriden
bool ce_vim_free(CeVim_t* vim);
bool ce_vim_rebind(CeVim_t* vim, CeRune_t key, CeVimParseFunc_t function);
CeVimParseResult_t ce_vim_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, CeConfigOptions_t* config_options);

// action
CeVimParseResult_t ce_vim_parse_action(CeVimAction_t* action, const CeRune_t* keys, CeVimKeyBind_t* key_binds,
                                       int64_t key_bind_count, CeVimMode_t vim_mode);
bool ce_vim_apply_action(CeVim_t* vim, CeVimAction_t* action, CeView_t* view, const CeConfigOptions_t* config_options);

// util
CePoint_t ce_vim_move_little_word(CeBuffer_t* buffer, CePoint_t start); // returns -1, -1 on error
CePoint_t ce_vim_move_big_word(CeBuffer_t* buffer, CePoint_t start);
CePoint_t ce_vim_move_end_little_word(CeBuffer_t* buffer, CePoint_t start);
CePoint_t ce_vim_move_end_big_word(CeBuffer_t* buffer, CePoint_t start);
CePoint_t ce_vim_move_begin_little_word(CeBuffer_t* buffer, CePoint_t start);
CePoint_t ce_vim_move_begin_big_word(CeBuffer_t* buffer, CePoint_t start);

// parse functions
CeVimParseResult_t ce_vim_parse_set_insert_mode(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_set_visual_mode(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_set_normal_mode(CeVimAction_t* action, CeRune_t key);
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
CeVimParseResult_t ce_vim_parse_verb_delete(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_change(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_set_character(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_yank(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_paste_before(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_paste_after(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_undo(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_redo(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_verb_last_action(CeVimAction_t* action, CeRune_t key);
CeVimParseResult_t ce_vim_parse_select_yank_register(CeVimAction_t* action, CeRune_t key);

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

// verb functions
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_motion);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_delete);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_change);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_set_character);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_yank);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_paste_before);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_paste_after);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_undo);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_redo);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_set_insert);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_set_visual);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_set_normal);
CE_VIM_DECLARE_VERB_FUNC(ce_vim_verb_last_action);
