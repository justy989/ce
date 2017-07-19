#pragma once

#include "ce.h"

#define CE_VIM_MAX_COMMAND_LEN 16
#define CE_VIM_MAX_KEY_BINDS 256

struct CeVimAction_t;
struct CeVim_t;
enum CeVimParseResult_t;

typedef enum CeVimParseResult_t CeVimParseFunc_t(struct CeVimAction_t*);
typedef void CeVimMotionFunc_t(struct CeVim_t*, struct CeVimAction_t*, CeBuffer_t*);
typedef void CeVimVerbFunc_t(struct CeVim_t*, struct CeVimAction_t*, CeBuffer_t*);

typedef enum{
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

typedef struct{
     int64_t multiplier;
     CeVimMotionFunc_t* function;
     union{
          char character;
          int integer;
          double decimal;
     };
     bool cursor_at_end_of_visual_selection; // opposite is beginning
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

typedef struct{
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

typedef struct{
     CeVimMode_t mode;
     CeVimKeyBind_t key_binds[CE_VIM_MAX_KEY_BINDS];
     int64_t key_bind_count;
     CeRune_t current_command[CE_VIM_MAX_COMMAND_LEN];
     bool chain_undo;
}CeVim_t;

bool ce_vim_init(CeVim_t* vim); // sets up default keybindings that can be overriden
bool ce_vim_free(CeVim_t* vim);
bool ce_vim_rebind(CeVim_t* vim, CeRune_t key, CeVimParseFunc_t function);
CeVimParseResult_t ce_vim_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, CeConfigOptions_t* config_options);
CeVimParseResult_t ce_vim_parse_action(const CeRune_t* keys, CeVimAction_t* action);
