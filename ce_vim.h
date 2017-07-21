#pragma once

#include "ce.h"

#define CE_VIM_MAX_COMMAND_LEN 16
#define CE_VIM_MAX_KEY_BINDS 256

struct CeVimAction_t;
struct CeVim_t;
struct CeVimMotionRange_t;
enum CeVimParseResult_t;

typedef enum CeVimParseResult_t CeVimParseFunc_t(struct CeVimAction_t*);
typedef bool CeVimMotionFunc_t(const struct CeVim_t*, const struct CeVimAction_t*, const CeView_t*,
                               const CeConfigOptions_t*, struct CeVimMotionRange_t*);
typedef bool CeVimVerbFunc_t(struct CeVim_t*, const struct CeVimAction_t*, struct CeVimMotionRange_t, CeView_t*,
                             const CeConfigOptions_t*);

typedef enum CeVimParseResult_t{
     CE_VIM_PARSE_INVALID,
     CE_VIM_PARSE_KEY_NOT_HANDLED,
     CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY,
     CE_VIM_PARSE_IN_PROGRESS,
     CE_VIM_PARSE_COMPLETE,
}CeVimParseResult_t;

typedef enum{
     CE_VIM_MODE_NORMAL,
     CE_VIM_MODE_INSERT,
     CE_VIM_MODE_VISUAL_RANGE,
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
     CeVimMode_t end_in_mode;
     bool yank;
}CeVimAction_t;

typedef struct{
     CeRune_t key;
     CeVimParseFunc_t* function;
}CeVimKeyBind_t;

typedef struct CeVim_t{
     CeVimMode_t mode;
     CeVimKeyBind_t key_binds[CE_VIM_MAX_KEY_BINDS];
     int64_t key_bind_count;
     CeRune_t current_command[CE_VIM_MAX_COMMAND_LEN];
     bool chain_undo;
     int64_t motion_column;
}CeVim_t;

bool ce_vim_init(CeVim_t* vim); // sets up default keybindings that can be overriden
bool ce_vim_free(CeVim_t* vim);
bool ce_vim_rebind(CeVim_t* vim, CeRune_t key, CeVimParseFunc_t function);
CeVimParseResult_t ce_vim_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, CeConfigOptions_t* config_options);

// action
CeVimParseResult_t ce_vim_parse_action(CeVimAction_t* action, const CeRune_t* keys, CeVimKeyBind_t* key_binds,
                                       int64_t key_bind_count);
bool ce_vim_apply_action(CeVim_t* vim, const CeVimAction_t* action, CeView_t* view, const CeConfigOptions_t* config_options);

// util
CePoint_t ce_vim_move_little_word(CeBuffer_t* buffer, CePoint_t start); // returns -1, -1 on error

// parse functions
CeVimParseResult_t ce_vim_parse_set_insert_mode(CeVimAction_t* action);
CeVimParseResult_t ce_vim_parse_motion_left(CeVimAction_t* action);
CeVimParseResult_t ce_vim_parse_motion_right(CeVimAction_t* action);
CeVimParseResult_t ce_vim_parse_motion_up(CeVimAction_t* action);
CeVimParseResult_t ce_vim_parse_motion_down(CeVimAction_t* action);
CeVimParseResult_t ce_vim_parse_motion_little_word(CeVimAction_t* action);

// motion functions
bool ce_vim_motion_left(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                        const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range);
bool ce_vim_motion_right(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                         const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range);
bool ce_vim_motion_up(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                      const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range);
bool ce_vim_motion_down(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                        const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range);
bool ce_vim_motion_little_word(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range);

// verb functions
bool ce_vim_motion_verb(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                        const CeConfigOptions_t* config_options);
