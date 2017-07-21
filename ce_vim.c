#include "ce_vim.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>
#include <assert.h>

// TODO: move these to utils somewhere?
static int64_t istrtol(const CeRune_t* istr, const CeRune_t** end_of_numbers){
     int64_t value = 0;
     const CeRune_t* itr = istr;

     while(*itr){
          if(isdigit(*itr)){
               value *= 10;
               value += *itr - '0';
          }else{
               if(itr != istr) *end_of_numbers = itr;
               break;
          }

          itr++;
     }

     if(!(*itr)) *end_of_numbers = itr;

     return value;
}

static int64_t istrlen(const CeRune_t* istr){
     int64_t len = 0;
     while(*istr){
          istr++;
          len++;
     }
     return len;
}

static void add_key_bind(CeVim_t* vim, CeRune_t key, CeVimParseFunc_t* function){
     assert(vim->key_bind_count < CE_VIM_MAX_KEY_BINDS);
     vim->key_binds[vim->key_bind_count].key = key;
     vim->key_binds[vim->key_bind_count].function = function;
     vim->key_bind_count++;
}

bool ce_vim_init(CeVim_t* vim){
     vim->chain_undo = false;

     add_key_bind(vim, 'i', &ce_vim_parse_set_insert_mode);
     add_key_bind(vim, 'w', &ce_vim_parse_motion_little_word);
     add_key_bind(vim, 'W', &ce_vim_parse_motion_big_word);
     add_key_bind(vim, 'e', &ce_vim_parse_motion_end_little_word);
     add_key_bind(vim, 'E', &ce_vim_parse_motion_end_big_word);
     add_key_bind(vim, 'b', &ce_vim_parse_motion_begin_little_word);
     add_key_bind(vim, 'B', &ce_vim_parse_motion_begin_big_word);
     add_key_bind(vim, 'h', &ce_vim_parse_motion_left);
     add_key_bind(vim, 'l', &ce_vim_parse_motion_right);
     add_key_bind(vim, 'k', &ce_vim_parse_motion_up);
     add_key_bind(vim, 'j', &ce_vim_parse_motion_down);
     add_key_bind(vim, '^', &ce_vim_parse_motion_soft_begin_line);
     add_key_bind(vim, '0', &ce_vim_parse_motion_hard_begin_line);
     add_key_bind(vim, '$', &ce_vim_parse_motion_end_line);
     add_key_bind(vim, 'd', &ce_vim_parse_verb_delete);
     add_key_bind(vim, 'c', &ce_vim_parse_verb_change);
     add_key_bind(vim, 'u', &ce_vim_parse_verb_undo);
     add_key_bind(vim, KEY_REDO, &ce_vim_parse_verb_redo);

     return true;
}

bool ce_vim_free(CeVim_t* vim){
     (void)(vim);
     return true;
}

bool ce_vim_bind_key(CeVim_t* vim, CeRune_t key, CeVimParseFunc_t function){
     for(int64_t i = 0; i < vim->key_bind_count; ++i){
          if(vim->key_binds[i].key == key){
               vim->key_binds[i].function = function;
               return true;
          }
     }

     if(vim->key_bind_count >= CE_VIM_MAX_KEY_BINDS) return false;

     vim->key_binds[vim->key_bind_count].key = key;
     vim->key_binds[vim->key_bind_count].function = function;
     vim->key_bind_count++;
     return true;
}

CeVimParseResult_t ce_vim_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, CeConfigOptions_t* config_options){
     switch(vim->mode){
     default:
          return CE_VIM_PARSE_INVALID;
     case CE_VIM_MODE_INSERT:
          switch(key){
          default:
               if(isprint(key) || key == CE_NEWLINE){
                    if(ce_buffer_insert_rune(view->buffer, key, view->cursor)){
                         const char str[2] = {key, 0};
                         CePoint_t new_cursor = ce_buffer_advance_point(view->buffer, view->cursor, 1);

                         // TODO: convenience function
                         CeBufferChange_t change = {};
                         change.chain = vim->chain_undo;
                         change.insertion = true;
                         change.remove_line_if_empty = true;
                         change.string = strdup(str);
                         change.location = view->cursor;
                         change.cursor_before = view->cursor;
                         change.cursor_after = new_cursor;
                         ce_buffer_change(view->buffer, &change);

                         view->cursor = new_cursor;
                         vim->chain_undo = true;
                    }
               }
               break;
          case KEY_BACKSPACE:
               if(!ce_points_equal(view->cursor, (CePoint_t){0, 0})){
                    CePoint_t remove_point = ce_buffer_advance_point(view->buffer, view->cursor, -1);
                    char* removed_string = ce_buffer_dupe_string(view->buffer, remove_point, 1);
                    if(ce_buffer_remove_string(view->buffer, remove_point, 1, true)){

                         // TODO: convenience function
                         CeBufferChange_t change = {};
                         change.chain = vim->chain_undo;
                         change.insertion = false;
                         change.remove_line_if_empty = true;
                         change.string = removed_string;
                         change.location = remove_point;
                         change.cursor_before = view->cursor;
                         change.cursor_after = remove_point;
                         ce_buffer_change(view->buffer, &change);

                         view->cursor = remove_point;
                         vim->chain_undo = true;
                    }
               }
               break;
          case KEY_LEFT:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){-1, 0}, config_options->tab_width, CE_CLAMP_X_ON);
               view->scroll = ce_view_follow_cursor(view, config_options->horizontal_scroll_off,
                                                    config_options->vertical_scroll_off, config_options->tab_width);
               vim->chain_undo = false;
               break;
          case KEY_DOWN:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){0, 1}, config_options->tab_width, CE_CLAMP_X_ON);
               view->scroll = ce_view_follow_cursor(view, config_options->horizontal_scroll_off,
                                                    config_options->vertical_scroll_off, config_options->tab_width);
               vim->chain_undo = false;
               break;
          case KEY_UP:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){0, -1}, config_options->tab_width, CE_CLAMP_X_ON);
               view->scroll = ce_view_follow_cursor(view, config_options->horizontal_scroll_off,
                                                    config_options->vertical_scroll_off, config_options->tab_width);
               vim->chain_undo = false;
               break;
          case KEY_RIGHT:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){1, 0}, config_options->tab_width, CE_CLAMP_X_ON);
               view->scroll = ce_view_follow_cursor(view, config_options->horizontal_scroll_off,
                                                    config_options->vertical_scroll_off, config_options->tab_width);
               vim->chain_undo = false;
               break;
          case 27: // escape
               vim->mode = CE_VIM_MODE_NORMAL;
               break;
          }
          break;
     case CE_VIM_MODE_NORMAL:
     {
          CeVimAction_t action = {};

          // append key to command
          int64_t command_len = istrlen(vim->current_command);
          if(command_len < (CE_VIM_MAX_COMMAND_LEN - 1)){
               vim->current_command[command_len] = key;
               vim->current_command[command_len + 1] = 0;
          }

          CeVimParseResult_t result = ce_vim_parse_action(&action, vim->current_command, vim->key_binds, vim->key_bind_count);

          if(result == CE_VIM_PARSE_COMPLETE){
               ce_vim_apply_action(vim, &action, view, config_options);
               vim->current_command[0] = 0;
          }else if(result == CE_VIM_PARSE_INVALID){
               vim->current_command[0] = 0;
          }

          return result;
     } break;
     }

     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_action(CeVimAction_t* action, const CeRune_t* keys, CeVimKeyBind_t* key_binds,
                                       int64_t key_bind_count){
     CeVimParseResult_t result = CE_VIM_PARSE_INVALID;
     CeVimAction_t build_action = {};

     // setup default multipliers
     build_action.multiplier = 1;
     build_action.motion.multiplier = 1;

     // parse multiplier if it exists
     const CeRune_t* end_of_multiplier = NULL;
     int64_t multiplier = istrtol(keys, &end_of_multiplier);
     if(end_of_multiplier && multiplier != 0){
          build_action.multiplier = multiplier;
          keys = end_of_multiplier;
          result = CE_VIM_PARSE_IN_PROGRESS;
     }

     // parse verb
     for(int64_t i = 0; i < key_bind_count; ++i){
          CeVimKeyBind_t* key_bind = key_binds + i;
          if(*keys == key_bind->key){
               result = key_bind->function(&build_action);
               if(result != CE_VIM_PARSE_KEY_NOT_HANDLED){
                    keys++;
                    break;
               }
          }
     }

     // parse multiplier
     if(result != CE_VIM_PARSE_COMPLETE){
          multiplier = istrtol(keys, &end_of_multiplier);
          if(end_of_multiplier && multiplier != 0){
               build_action.motion.multiplier = multiplier;
               keys = end_of_multiplier;
          }

          // parse motion
          for(int64_t i = 0; i < key_bind_count; ++i){
               CeVimKeyBind_t* key_bind = key_binds + i;
               if(*keys == key_bind->key){
                    result = key_bind->function(&build_action);
                    if(result != CE_VIM_PARSE_KEY_NOT_HANDLED) break;
               }
          }
     }

     if(result == CE_VIM_PARSE_COMPLETE) *action = build_action;
     return result;
}

bool ce_vim_apply_action(CeVim_t* vim, const CeVimAction_t* action, CeView_t* view, const CeConfigOptions_t* config_options){
     CeVimMotionRange_t motion_range = {view->cursor, view->cursor};
     if(action->motion.function){
          int64_t total_multiplier = action->multiplier * action->motion.multiplier;
          for(int64_t i = 0; i < total_multiplier; ++i){
               if(!action->motion.function(vim, action, view, config_options, &motion_range)){
                    return false;
               }
          }
     }
     if(action->verb.function){
          if(!action->verb.function(vim, action, motion_range, view, config_options)){
               return false;
          }
     }
     vim->mode = action->end_in_mode;
     return true;
}

static bool is_little_word_character(int c){
     return isalnum(c) || c == '_';
}

typedef enum{
     WORD_INSIDE_WORD,
     WORD_INSIDE_SPACE,
     WORD_INSIDE_OTHER,
}WordState_t;

CePoint_t ce_vim_move_little_word(CeBuffer_t* buffer, CePoint_t start){
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};

     char* itr = ce_utf8_find_index(buffer->lines[start.y], start.x);

     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode(itr, &rune_len);
     WordState_t state = WORD_INSIDE_OTHER;

     if(is_little_word_character(rune)){
          state = WORD_INSIDE_WORD;
     }else if(isspace(rune)){
          state = WORD_INSIDE_SPACE;
     }

     itr += rune_len;
     start.x++;

     // TODO: handle multiple lines
     while(*itr){
          rune = ce_utf8_decode(itr, &rune_len);

          switch(state){
          default:
               assert(0);
               break;
          case WORD_INSIDE_WORD:
               if(isspace(rune)) state = WORD_INSIDE_SPACE;
               else if(!is_little_word_character(rune)) goto END_LOOP;
               break;
          case WORD_INSIDE_SPACE:
               if(is_little_word_character(rune) || !isspace(rune)) goto END_LOOP;
               break;
          case WORD_INSIDE_OTHER:
               if(isspace(rune)) state = WORD_INSIDE_SPACE;
               else if(is_little_word_character(rune)) goto END_LOOP;
               break;
          }

          itr += rune_len;
          start.x++;
     }

END_LOOP:

     return start;
}

CePoint_t ce_vim_move_big_word(CeBuffer_t* buffer, CePoint_t start){
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};

     char* itr = ce_utf8_find_index(buffer->lines[start.y], start.x);

     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode(itr, &rune_len);
     WordState_t state = WORD_INSIDE_WORD;

     if(isspace(rune)) state = WORD_INSIDE_SPACE;

     itr += rune_len;
     start.x++;

     // TODO: handle multiple lines
     while(*itr){
          rune = ce_utf8_decode(itr, &rune_len);

          switch(state){
          default:
               assert(0);
               break;
          case WORD_INSIDE_WORD:
               if(isspace(rune)) state = WORD_INSIDE_SPACE;
               break;
          case WORD_INSIDE_SPACE:
               if(!isspace(rune)) goto END_LOOP;
               break;
          }

          itr += rune_len;
          start.x++;
     }

END_LOOP:

     return start;
}

CePoint_t ce_vim_move_end_little_word(CeBuffer_t* buffer, CePoint_t start){
     start.x++;
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};

     char* itr = ce_utf8_find_index(buffer->lines[start.y], start.x);

     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode(itr, &rune_len);
     WordState_t state = WORD_INSIDE_OTHER;

     rune = ce_utf8_decode(itr, &rune_len);

     if(is_little_word_character(rune)){
          state = WORD_INSIDE_WORD;
     }else if(isspace(rune)){
          state = WORD_INSIDE_SPACE;
     }

     itr += rune_len;

     // TODO: handle multiple lines
     while(*itr){
          rune = ce_utf8_decode(itr, &rune_len);

          switch(state){
          default:
               assert(0);
               break;
          case WORD_INSIDE_WORD:
               if(isspace(rune)) goto END_LOOP;
               else if(!is_little_word_character(rune)) goto END_LOOP;
               break;
          case WORD_INSIDE_SPACE:
               if(is_little_word_character(rune)) state = WORD_INSIDE_WORD;
               else if(!isspace(rune)){
                    start.x++;
                    goto END_LOOP;
               }
               break;
          case WORD_INSIDE_OTHER:
               goto END_LOOP;
          }

          itr += rune_len;
          start.x++;
     }

END_LOOP:

     return start;
}

CePoint_t ce_vim_move_end_big_word(CeBuffer_t* buffer, CePoint_t start){
     start.x++;
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};

     char* itr = ce_utf8_find_index(buffer->lines[start.y], start.x);

     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode(itr, &rune_len);
     WordState_t state = WORD_INSIDE_WORD;

     rune = ce_utf8_decode(itr, &rune_len);

     if(isspace(rune)) state = WORD_INSIDE_SPACE;

     itr += rune_len;

     // TODO: handle multiple lines
     while(*itr){
          rune = ce_utf8_decode(itr, &rune_len);

          switch(state){
          default:
               assert(0);
               break;
          case WORD_INSIDE_WORD:
               if(isspace(rune)) goto END_LOOP;
               break;
          case WORD_INSIDE_SPACE:
               if(!isspace(rune)) state = WORD_INSIDE_WORD;
               break;
          }

          itr += rune_len;
          start.x++;
     }

END_LOOP:

     return start;
}

CePoint_t ce_vim_move_begin_little_word(CeBuffer_t* buffer, CePoint_t start){
     start.x--;
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};

     char* line_start = buffer->lines[start.y];
     char* itr = ce_utf8_find_index(line_start, start.x); // start one character back

     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);
     WordState_t state = WORD_INSIDE_OTHER;

     if(is_little_word_character(rune)){
          state = WORD_INSIDE_WORD;
     }else if(isspace(rune)){
          state = WORD_INSIDE_SPACE;
     }

     itr -= rune_len;

     // TODO: handle multiple lines
     while(*itr){
          rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);

          switch(state){
          default:
               assert(0);
               break;
          case WORD_INSIDE_WORD:
               if(isspace(rune)) goto END_LOOP;
               else if(!is_little_word_character(rune)) goto END_LOOP;
               break;
          case WORD_INSIDE_SPACE:
               if(is_little_word_character(rune)) state = WORD_INSIDE_WORD;
               else if(!isspace(rune)) goto END_LOOP;
               break;
          case WORD_INSIDE_OTHER:
               if(isspace(rune)) goto END_LOOP;
               else if(is_little_word_character(rune)) goto END_LOOP;
               break;
          }

          itr -= rune_len;
          start.x--;
     }

END_LOOP:
     return start;
}

CePoint_t ce_vim_move_begin_big_word(CeBuffer_t* buffer, CePoint_t start){
     start.x--;
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};

     char* line_start = buffer->lines[start.y];
     char* itr = ce_utf8_find_index(line_start, start.x); // start one character back

     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);
     WordState_t state = WORD_INSIDE_OTHER;

     if(isspace(rune)){
          state = WORD_INSIDE_SPACE;
     }else{
          state = WORD_INSIDE_WORD;
     }

     itr -= rune_len;

     // TODO: handle multiple lines
     while(*itr){
          rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);

          switch(state){
          default:
               assert(0);
               break;
          case WORD_INSIDE_WORD:
               if(isspace(rune)) goto END_LOOP;
               break;
          case WORD_INSIDE_SPACE:
               if(!isspace(rune)) state = WORD_INSIDE_WORD;
               break;
          }

          itr -= rune_len;
          start.x--;
     }

END_LOOP:

     return start;
}

CeVimParseResult_t ce_vim_parse_set_insert_mode(CeVimAction_t* action){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->end_in_mode = CE_VIM_MODE_INSERT;
     return CE_VIM_PARSE_COMPLETE;
}

static CeVimParseResult_t parse_motion_direction(CeVimAction_t* action, CeVimMotionFunc_t* func){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     action->motion.function = func;

     if(action->verb.function) return CE_VIM_PARSE_COMPLETE;
     action->verb.function = ce_vim_verb_motion;

     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_motion_left(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_left);
}

CeVimParseResult_t ce_vim_parse_motion_right(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_right);
}

CeVimParseResult_t ce_vim_parse_motion_up(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_up);
}

CeVimParseResult_t ce_vim_parse_motion_down(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_down);
}

CeVimParseResult_t ce_vim_parse_motion_little_word(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_little_word);
}

CeVimParseResult_t ce_vim_parse_motion_big_word(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_big_word);
}

CeVimParseResult_t ce_vim_parse_motion_end_little_word(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_end_little_word);
}

CeVimParseResult_t ce_vim_parse_motion_end_big_word(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_end_big_word);
}

CeVimParseResult_t ce_vim_parse_motion_begin_little_word(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_begin_little_word);
}

CeVimParseResult_t ce_vim_parse_motion_begin_big_word(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_begin_big_word);
}

CeVimParseResult_t ce_vim_parse_motion_soft_begin_line(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_soft_begin_line);
}

CeVimParseResult_t ce_vim_parse_motion_hard_begin_line(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_hard_begin_line);
}

CeVimParseResult_t ce_vim_parse_motion_end_line(CeVimAction_t* action){
     return parse_motion_direction(action, ce_vim_motion_end_line);
}

CeVimParseResult_t ce_vim_parse_verb_delete(CeVimAction_t* action){
     if(action->verb.function == ce_vim_verb_delete){
          action->motion.function = &ce_vim_motion_entire_line;
     }else{
          action->verb.function = &ce_vim_verb_delete;
     }

     return CE_VIM_PARSE_IN_PROGRESS;
}

CeVimParseResult_t ce_vim_parse_verb_change(CeVimAction_t* action){
     CeVimParseResult_t result = ce_vim_parse_verb_delete(action);
     action->end_in_mode = CE_VIM_MODE_INSERT;
     action->chain_undo = true;
     return result;
}

CeVimParseResult_t ce_vim_parse_verb_undo(CeVimAction_t* action){
     action->verb.function = &ce_vim_verb_undo;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_redo(CeVimAction_t* action){
     action->verb.function = &ce_vim_verb_redo;
     return CE_VIM_PARSE_COMPLETE;
}

static bool motion_direction(const CeView_t* view, CePoint_t delta, const CeConfigOptions_t* config_options,
                             CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_buffer_move_point(view->buffer, motion_range->end, delta, config_options->tab_width, CE_CLAMP_X_NONE);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_left(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                        const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return motion_direction(view, (CePoint_t){-1, 0}, config_options, motion_range);
}

bool ce_vim_motion_right(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                         const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return motion_direction(view, (CePoint_t){1, 0}, config_options, motion_range);
}

bool ce_vim_motion_up(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                      const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return motion_direction(view, (CePoint_t){0, -1}, config_options, motion_range);
}

bool ce_vim_motion_down(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                        const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return motion_direction(view, (CePoint_t){0, 1}, config_options, motion_range);
}

bool ce_vim_motion_little_word(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_big_word(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                            const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_end_little_word(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                                   const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_end_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_end_big_word(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                                const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_end_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_begin_little_word(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                                     const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_begin_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_begin_big_word(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                                     const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_begin_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_soft_begin_line(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                                   const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     if(!ce_buffer_point_is_valid(view->buffer, motion_range->end)) return false;
     const char* itr = view->buffer->lines[motion_range->end.y];
     int64_t index = 0;
     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode(itr, &rune_len);

     while(true){
          rune = ce_utf8_decode(itr, &rune_len);
          itr += rune_len;
          if(isspace(rune)) index++;
          else break;
     }

     motion_range->end = (CePoint_t){index, motion_range->end.y};
     return true;
}

bool ce_vim_motion_hard_begin_line(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                                   const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end = (CePoint_t){0, motion_range->end.y};
     return true;
}

bool ce_vim_motion_end_line(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                            const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     int64_t len = ce_utf8_strlen(view->buffer->lines[motion_range->end.y]);
     motion_range->end = (CePoint_t){len - 1, motion_range->end.y};
     return true;
}

bool ce_vim_motion_entire_line(const CeVim_t* vim, const CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     int64_t len = ce_utf8_strlen(view->buffer->lines[motion_range->end.y]);
     motion_range->start = (CePoint_t){0, motion_range->end.y};
     motion_range->end   = (CePoint_t){len - 1, motion_range->end.y};
     return true;
}

bool ce_vim_verb_motion(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                        const CeConfigOptions_t* config_options){
     if(action->motion.function == ce_vim_motion_up || action->motion.function == ce_vim_motion_down){
          motion_range.end.x = vim->motion_column;
     }
     view->cursor = ce_buffer_clamp_point(view->buffer, motion_range.end, CE_CLAMP_X_ON);
     if(ce_points_equal(motion_range.end, view->cursor)) vim->motion_column = view->cursor.x;
     view->scroll = ce_view_follow_cursor(view, config_options->horizontal_scroll_off,
                                          config_options->vertical_scroll_off, config_options->tab_width);
     return true;
}

bool ce_vim_verb_delete(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                        const CeConfigOptions_t* config_options){
     bool delete_backwards = false;
     // sort the range
     if(ce_point_after(motion_range.start, motion_range.end)){
          CePoint_t tmp = motion_range.start;
          motion_range.start = motion_range.end;
          motion_range.end = tmp;
          delete_backwards = true;
     }

     // delete the range
     if(delete_backwards) motion_range.end = ce_buffer_advance_point(view->buffer, motion_range.end, -1);
     int64_t delete_len = ce_buffer_range_len(view->buffer, motion_range.start, motion_range.end);
     char* removed_string = ce_buffer_dupe_string(view->buffer, motion_range.start, delete_len);
     if(!ce_buffer_remove_string(view->buffer, motion_range.start, delete_len, true)){
          free(removed_string);
          return false;
     }

     // commit the change
     CeBufferChange_t change = {};
     change.chain = action->chain_undo;
     change.insertion = false;
     change.remove_line_if_empty = true;
     change.string = removed_string;
     change.location = motion_range.start;
     change.cursor_before = view->cursor;
     change.cursor_after = motion_range.start;
     ce_buffer_change(view->buffer, &change);

     view->cursor = motion_range.start;
     vim->chain_undo = action->chain_undo;
     return true;
}

bool ce_vim_verb_undo(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                      const CeConfigOptions_t* config_options){
     return ce_buffer_undo(view->buffer, &view->cursor);
}

bool ce_vim_verb_redo(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                      const CeConfigOptions_t* config_options){
     return ce_buffer_redo(view->buffer, &view->cursor);
}
