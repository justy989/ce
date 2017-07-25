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

static void insert_mode(CeVim_t* vim){
     vim->mode = CE_VIM_MODE_INSERT;
     if(!vim->verb_last_action) ce_rune_node_free(&vim->insert_rune_head);
}

bool ce_vim_init(CeVim_t* vim){
     vim->chain_undo = false;

     add_key_bind(vim, 'i', &ce_vim_parse_verb_insert_mode);
     add_key_bind(vim, 'v', &ce_vim_parse_verb_visual_mode);
     add_key_bind(vim, 'V', &ce_vim_parse_verb_visual_line_mode);
     add_key_bind(vim, 27, &ce_vim_parse_verb_normal_mode);
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
     add_key_bind(vim, 'f', &ce_vim_parse_motion_find_forward);
     add_key_bind(vim, 'F', &ce_vim_parse_motion_find_backward);
     add_key_bind(vim, 't', &ce_vim_parse_motion_until_forward);
     add_key_bind(vim, 'T', &ce_vim_parse_motion_until_backward);
     add_key_bind(vim, 'd', &ce_vim_parse_verb_delete);
     add_key_bind(vim, 'c', &ce_vim_parse_verb_change);
     add_key_bind(vim, 'r', &ce_vim_parse_verb_set_character);
     add_key_bind(vim, 'y', &ce_vim_parse_verb_yank);
     add_key_bind(vim, '"', &ce_vim_parse_select_yank_register);
     add_key_bind(vim, 'P', &ce_vim_parse_verb_paste_before);
     add_key_bind(vim, 'p', &ce_vim_parse_verb_paste_after);
     add_key_bind(vim, 'u', &ce_vim_parse_verb_undo);
     add_key_bind(vim, KEY_REDO, &ce_vim_parse_verb_redo);
     add_key_bind(vim, 'O', &ce_vim_parse_verb_open_above);
     add_key_bind(vim, 'o', &ce_vim_parse_verb_open_below);
     add_key_bind(vim, '.', &ce_vim_parse_verb_last_action);
     add_key_bind(vim, 2, &ce_vim_parse_motion_page_up);
     add_key_bind(vim, 6, &ce_vim_parse_motion_page_down);
     add_key_bind(vim, 21, &ce_vim_parse_motion_half_page_up);
     add_key_bind(vim, 4, &ce_vim_parse_motion_half_page_down);

     return true;
}

bool ce_vim_free(CeVim_t* vim){
     for(int64_t i = 0; i < ASCII_PRINTABLE_CHARACTERS; i++){
          CeVimYank_t* yank = vim->yanks + i;
          if(yank->text){
               free(yank->text);
               yank->text = NULL;
          }

          yank->line = false;
     }
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

CeVimParseResult_t ce_vim_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, const CeConfigOptions_t* config_options){
     switch(vim->mode){
     default:
          return CE_VIM_PARSE_INVALID;
     case CE_VIM_MODE_INSERT:
          if(!vim->verb_last_action) ce_rune_node_insert(&vim->insert_rune_head, key);

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
                    char* removed_string = ce_buffer_dupe_string(view->buffer, remove_point, 1, false);
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
     case CE_VIM_MODE_VISUAL:
     case CE_VIM_MODE_VISUAL_LINE:
     {
          CeVimAction_t action = {};

          // append key to command
          int64_t command_len = istrlen(vim->current_command);
          if(command_len < (CE_VIM_MAX_COMMAND_LEN - 1)){
               vim->current_command[command_len] = key;
               vim->current_command[command_len + 1] = 0;
          }

          CeVimParseResult_t result = ce_vim_parse_action(&action, vim->current_command, vim->key_binds,
                                                          vim->key_bind_count, vim->mode);

          if(result == CE_VIM_PARSE_COMPLETE){
               ce_vim_apply_action(vim, &action, view, config_options);
               vim->current_command[0] = 0;

               if((action.verb.function != ce_vim_verb_motion &&
                   action.verb.function != ce_vim_verb_yank &&
                   action.verb.function != ce_vim_verb_last_action &&
                   action.verb.function != ce_vim_verb_undo &&
                   action.verb.function != ce_vim_verb_redo &&
                   action.verb.function != ce_vim_verb_normal_mode &&
                   action.verb.function != ce_vim_verb_visual_mode) ||
                  vim->mode == CE_VIM_MODE_INSERT){
                    vim->last_action = action;
               }
          }else if(result == CE_VIM_PARSE_INVALID ||
                   result == CE_VIM_PARSE_KEY_NOT_HANDLED){
               vim->current_command[0] = 0;
          }

          return result;
     } break;
     }

     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_action(CeVimAction_t* action, const CeRune_t* keys, CeVimKeyBind_t* key_binds,
                                       int64_t key_bind_count, CeVimMode_t vim_mode){
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
VIM_PARSE_CONTINUE:
     for(int64_t i = 0; i < key_bind_count; ++i){
          CeVimKeyBind_t* key_bind = key_binds + i;
          if(*keys == key_bind->key){
               result = key_bind->function(&build_action, *keys);
               if(result != CE_VIM_PARSE_KEY_NOT_HANDLED){
                    keys++;

                    // TODO: trust functions to no infinite loop ?
                    while(result == CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY){
                         if(*keys == 0) return result;
                         result = key_bind->function(&build_action, *keys);
                         keys++;
                         if(result == CE_VIM_PARSE_CONTINUE) goto VIM_PARSE_CONTINUE;
                    }

                    break;
               }
          }
     }

     // parse multiplier
     if(result != CE_VIM_PARSE_COMPLETE){
          if(vim_mode == CE_VIM_MODE_VISUAL || vim_mode == CE_VIM_MODE_VISUAL_LINE){
               build_action.motion.function = &ce_vim_motion_visual;
               result = CE_VIM_PARSE_COMPLETE;
          }else{
               multiplier = istrtol(keys, &end_of_multiplier);
               if(end_of_multiplier && multiplier != 0){
                    build_action.motion.multiplier = multiplier;
                    keys = end_of_multiplier;
               }

               // parse motion
               for(int64_t i = 0; i < key_bind_count; ++i){
                    CeVimKeyBind_t* key_bind = key_binds + i;
                    if(*keys == key_bind->key){
                         result = key_bind->function(&build_action, *keys);
                         if(result != CE_VIM_PARSE_KEY_NOT_HANDLED){
                              keys++;

                              // TODO: trust functions to no infinite loop ?
                              while(result == CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY){
                                   if(*keys == 0) return result;
                                   result = key_bind->function(&build_action, *keys);
                                   keys++;
                                   if(result == CE_VIM_PARSE_CONTINUE) goto VIM_PARSE_CONTINUE;
                              }

                              break;
                         }
                    }
               }
          }
     }

     if(result == CE_VIM_PARSE_COMPLETE) *action = build_action;
     return result;
}

bool ce_vim_apply_action(CeVim_t* vim, CeVimAction_t* action, CeView_t* view, const CeConfigOptions_t* config_options){
     CeVimMotionRange_t motion_range = {view->cursor, view->cursor};
     if(action->motion.function){
          int64_t total_multiplier = action->multiplier * action->motion.multiplier;
          for(int64_t i = 0; i < total_multiplier; ++i){
               if(!action->motion.function(vim, action, view, config_options, &motion_range)){
                    return false;
               }
          }

          ce_log("multiplier: %d, start: %d, %d end: %d, %d\n", total_multiplier,
                 motion_range.start.x, motion_range.start.y,
                 motion_range.end.x, motion_range.end.y);
     }
     if(action->verb.function){
          if(!action->verb.function(vim, action, motion_range, view, config_options)){
               return false;
          }
     }
     return true;
}

static bool is_little_word_character(int c){
     //return isalnum(c) || c == '_';
     return isalnum(c);
}

typedef enum{
     WORD_INSIDE_WORD,
     WORD_INSIDE_SPACE,
     WORD_INSIDE_OTHER,
     WORD_NEW_LINE,
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

     while(true){
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
          case WORD_NEW_LINE:
               if(isspace(rune)) state = WORD_INSIDE_SPACE;
               else goto END_LOOP;
               break;
          }

          itr += rune_len;
          start.x++;

          if(*itr == 0){
               if(start.y >= buffer->line_count - 1) break;
               start.x = 0;
               start.y++;
               itr = buffer->lines[start.y];
               state = WORD_NEW_LINE;
          }
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

     while(true){
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
          case WORD_NEW_LINE:
               if(isspace(rune)) state = WORD_INSIDE_SPACE;
               else goto END_LOOP;
               break;
          }

          itr += rune_len;
          start.x++;

          if(*itr == 0){
               if(start.y >= buffer->line_count - 1) break;
               start.x = 0;
               start.y++;
               itr = buffer->lines[start.y];
               state = WORD_NEW_LINE;
          }
     }

END_LOOP:

     return start;
}

CePoint_t ce_vim_move_end_little_word(CeBuffer_t* buffer, CePoint_t start){
     start.x++;
     if(!ce_buffer_point_is_valid(buffer, start)){
          if(start.x == 1){
               // line is empty
               start.x = 0;
          }else{
               return (CePoint_t){-1, -1};
          }
     }

     char* itr = ce_utf8_find_index(buffer->lines[start.y], start.x);
     int64_t rune_len = 0;
     CeRune_t rune = 0;
     WordState_t state = WORD_INSIDE_OTHER;

     if(*itr == 0){
          if(start.y >= buffer->line_count - 1) return (CePoint_t){-1, -1};
          start.x = 0;
          start.y++;
          itr = buffer->lines[start.y];

          rune = ce_utf8_decode(itr, &rune_len);
          itr += rune_len;
          if(isspace(rune)) state = WORD_INSIDE_SPACE;
          else if(is_little_word_character(rune)) state = WORD_INSIDE_WORD;
          else return start;
     }else{
          rune = ce_utf8_decode(itr, &rune_len);

          if(is_little_word_character(rune)){
               state = WORD_INSIDE_WORD;
          }else if(isspace(rune)){
               state = WORD_INSIDE_SPACE;
          }

          itr += rune_len;
     }

     while(true){
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

          if(*itr == 0){
               if(start.y >= buffer->line_count - 1) break;
               start.x = 0;
               start.y++;
               itr = buffer->lines[start.y];

               rune = ce_utf8_decode(itr, &rune_len);
               itr += rune_len;
               if(isspace(rune)) state = WORD_INSIDE_SPACE;
               else if(is_little_word_character(rune)) state = WORD_INSIDE_WORD;
               else break;
          }
     }

END_LOOP:

     return start;
}

CePoint_t ce_vim_move_end_big_word(CeBuffer_t* buffer, CePoint_t start){
     start.x++;
     if(!ce_buffer_point_is_valid(buffer, start)){
          if(start.x == 1){
               // line is empty
               start.x = 0;
          }else{
               return (CePoint_t){-1, -1};
          }
     }

     char* itr = ce_utf8_find_index(buffer->lines[start.y], start.x);

     int64_t rune_len = 0;
     CeRune_t rune = 0;
     WordState_t state = WORD_INSIDE_WORD;

     if(*itr == 0){
          if(start.y >= buffer->line_count - 1) return (CePoint_t){-1, -1};
          start.x = 0;
          start.y++;
          itr = buffer->lines[start.y];

          rune = ce_utf8_decode(itr, &rune_len);
          itr += rune_len;
          if(isspace(rune)) state = WORD_INSIDE_SPACE;
          else state = WORD_INSIDE_WORD;
     }else{
          rune = ce_utf8_decode(itr, &rune_len);
          if(isspace(rune)) state = WORD_INSIDE_SPACE;
          itr += rune_len;
     }

     while(true){
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

          if(*itr == 0) break;
     }

END_LOOP:

     return start;
}

CePoint_t ce_vim_move_begin_little_word(CeBuffer_t* buffer, CePoint_t start){
     WordState_t state = WORD_INSIDE_OTHER;

     start.x--;
     if(!ce_buffer_point_is_valid(buffer, start)){
          if(start.x == -1){
               start.y--;
               if(start.y < 0) return (CePoint_t){0, 0};
               start.x = ce_utf8_strlen(buffer->lines[start.y]);
               if(start.x > 0) start.x--;
               else return start;
               state = WORD_NEW_LINE;
          }else{
               return (CePoint_t){-1, -1};
          }
     }


     char* line_start = buffer->lines[start.y];
     char* itr = ce_utf8_find_index(line_start, start.x); // start one character back

     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);

     if(is_little_word_character(rune)){
          if(start.x == 0) return start;
          state = WORD_INSIDE_WORD;
     }else if(isspace(rune)){
          state = WORD_INSIDE_SPACE;
     }else{
          if(start.x == 0) return start;
     }

     itr -= rune_len;

     while(true){
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
          case WORD_NEW_LINE:
               if(is_little_word_character(rune)) state = WORD_INSIDE_WORD;
               if(isspace(rune)) state = WORD_INSIDE_SPACE;
               goto END_LOOP;
          }

          itr -= rune_len;
          start.x--;

          if(itr <= line_start){
               if(state == WORD_INSIDE_WORD || state == WORD_INSIDE_OTHER) break;

               start.y--;
               if(start.y < 0) return (CePoint_t){0, 0};
               start.x = ce_utf8_strlen(buffer->lines[start.y]);
               if(start.x > 0) start.x--;
               else break;
               line_start = buffer->lines[start.y];
               itr = ce_utf8_find_index(line_start, start.x); // start one character back
               state = WORD_NEW_LINE;
          }
     }

END_LOOP:
     return start;
}

CePoint_t ce_vim_move_begin_big_word(CeBuffer_t* buffer, CePoint_t start){
     start.x--;
     if(!ce_buffer_point_is_valid(buffer, start)){
          if(start.x == -1){
               start.y--;
               if(start.y < 0) return (CePoint_t){0, 0};
               start.x = ce_utf8_strlen(buffer->lines[start.y]);
               if(start.x > 0) start.x--;
               else return start;
          }else{
               return (CePoint_t){-1, -1};
          }
     }

     char* line_start = buffer->lines[start.y];
     char* itr = ce_utf8_find_index(line_start, start.x); // start one character back

     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);
     WordState_t state = WORD_INSIDE_OTHER;

     if(isspace(rune)){
          state = WORD_INSIDE_SPACE;
     }else{
          if(start.x == 0) return start;
          state = WORD_INSIDE_WORD;
     }

     itr -= rune_len;

     while(true){
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
          case WORD_NEW_LINE:
               if(isspace(rune)) state = WORD_INSIDE_SPACE;
               else state = WORD_INSIDE_WORD;
               break;
          }

          itr -= rune_len;
          start.x--;

          if(itr <= line_start){
               if(state == WORD_INSIDE_WORD) break;

               start.y--;
               if(start.y < 0) return (CePoint_t){0, 0};
               start.x = ce_utf8_strlen(buffer->lines[start.y]);
               if(start.x == 0) break;
               line_start = buffer->lines[start.y];
               itr = ce_utf8_find_index(line_start, start.x);
               state = WORD_NEW_LINE;
               start.x++;
          }
     }

END_LOOP:

     return start;
}

CePoint_t ce_vim_move_find_rune_forward(CeBuffer_t* buffer, CePoint_t start, CeRune_t match_rune, bool until){
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};
     int64_t match_x = until ? start.x + 2 : start.x + 1;
     char* str = ce_utf8_find_index(buffer->lines[start.y], match_x);

     while(*str){
          int64_t rune_len = 0;
          CeRune_t rune = ce_utf8_decode(str, &rune_len);
          if(rune == match_rune) return (CePoint_t){until ? match_x - 1 : match_x, start.y};
          str += rune_len;
          match_x++;
     }

     return start;
}

CePoint_t ce_vim_move_find_rune_backward(CeBuffer_t* buffer, CePoint_t start, CeRune_t match_rune, bool until){
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};
     if(start.x == 0) return (CePoint_t){-1, -1};

     char* start_of_line = buffer->lines[start.y];
     char* str = ce_utf8_find_index(start_of_line, start.x);
     int64_t match_x = start.x - 1;
     str--;

     if(until){
          match_x--;
          int64_t rune_len = 0;
          ce_utf8_decode_reverse(str, start_of_line, &rune_len);
          str -= rune_len;
     }

     while(str >= start_of_line){
          int64_t rune_len = 0;
          CeRune_t rune = ce_utf8_decode_reverse(str, start_of_line, &rune_len);
          if(rune == match_rune) return (CePoint_t){until ? match_x + 1 : match_x, start.y};
          str -= rune_len;
          match_x--;
     }

     return start;
}

CeVimParseResult_t ce_vim_parse_verb_insert_mode(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_insert_mode;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_visual_mode(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_visual_mode;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_visual_line_mode(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_visual_line_mode;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_normal_mode(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_normal_mode;
     return CE_VIM_PARSE_COMPLETE;
}

static CeVimParseResult_t parse_motion_direction(CeVimAction_t* action, CeVimMotionFunc_t* func){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     action->motion.function = func;

     if(action->verb.function) return CE_VIM_PARSE_COMPLETE;
     action->verb.function = ce_vim_verb_motion;

     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_motion_left(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_left);
}

CeVimParseResult_t ce_vim_parse_motion_right(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_right);
}

CeVimParseResult_t ce_vim_parse_motion_up(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_up);
}

CeVimParseResult_t ce_vim_parse_motion_down(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_down);
}

CeVimParseResult_t ce_vim_parse_motion_little_word(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_little_word);
}

CeVimParseResult_t ce_vim_parse_motion_big_word(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_big_word);
}

CeVimParseResult_t ce_vim_parse_motion_end_little_word(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_end_little_word);
}

CeVimParseResult_t ce_vim_parse_motion_end_big_word(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_end_big_word);
}

CeVimParseResult_t ce_vim_parse_motion_begin_little_word(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_begin_little_word);
}

CeVimParseResult_t ce_vim_parse_motion_begin_big_word(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_begin_big_word);
}

CeVimParseResult_t ce_vim_parse_motion_soft_begin_line(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_soft_begin_line);
}

CeVimParseResult_t ce_vim_parse_motion_hard_begin_line(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_hard_begin_line);
}

CeVimParseResult_t ce_vim_parse_motion_end_line(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_end_line);
}

CeVimParseResult_t ce_vim_parse_motion_page_up(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_page_up);
}

CeVimParseResult_t ce_vim_parse_motion_page_down(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_page_down);
}

CeVimParseResult_t ce_vim_parse_motion_half_page_up(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_half_page_up);
}

CeVimParseResult_t ce_vim_parse_motion_half_page_down(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_half_page_down);
}

CeVimParseResult_t ce_vim_parse_verb_delete(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == ce_vim_verb_delete){
          action->motion.function = &ce_vim_motion_entire_line;
          action->yank_line = true;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = &ce_vim_verb_delete;
     return CE_VIM_PARSE_IN_PROGRESS;
}

CeVimParseResult_t ce_vim_parse_verb_change(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == ce_vim_verb_change){
          action->motion.function = &ce_vim_motion_entire_line;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = &ce_vim_verb_change;
     action->chain_undo = true;
     return CE_VIM_PARSE_IN_PROGRESS;
}

CeVimParseResult_t ce_vim_parse_verb_set_character(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == NULL){
          action->verb.function = &ce_vim_verb_set_character;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else{
          action->verb.integer = key;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_yank(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == &ce_vim_verb_yank){
          action->motion.function = &ce_vim_motion_entire_line;
          action->yank_line = true;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = &ce_vim_verb_yank;
     if(action->verb.character == 0) action->verb.character = '"';
     return CE_VIM_PARSE_IN_PROGRESS;
}

CeVimParseResult_t ce_vim_parse_verb_paste_before(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_paste_before;
     if(action->verb.character == 0) action->verb.character = '"';
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_paste_after(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_paste_after;
     if(action->verb.character == 0) action->verb.character = '"';
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_open_above(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_open_above;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_open_below(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_open_below;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_undo(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_undo;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_redo(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_redo;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_select_yank_register(CeVimAction_t* action, CeRune_t key){
     if(action->verb.character == 0){
          action->verb.character = '"';
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->verb.character == '"'){
          action->verb.character = key;
          return CE_VIM_PARSE_CONTINUE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_last_action(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_last_action;
     return CE_VIM_PARSE_COMPLETE;
}

static CeVimParseResult_t vim_parse_motion_find(CeVimAction_t* action, CeRune_t key, CeVimMotionFunc_t* func){
     if(action->verb.function == NULL) action->verb.function = &ce_vim_verb_motion;

     if(action->motion.function == NULL){
          action->motion.function = func;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->motion.function == func){
          action->motion.integer = key;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}


CeVimParseResult_t ce_vim_parse_motion_find_forward(CeVimAction_t* action, CeRune_t key){
     return vim_parse_motion_find(action, key, ce_vim_motion_find_forward);
}

CeVimParseResult_t ce_vim_parse_motion_find_backward(CeVimAction_t* action, CeRune_t key){
     return vim_parse_motion_find(action, key, ce_vim_motion_find_backward);
}

CeVimParseResult_t ce_vim_parse_motion_until_forward(CeVimAction_t* action, CeRune_t key){
     return vim_parse_motion_find(action, key, ce_vim_motion_until_forward);
}

CeVimParseResult_t ce_vim_parse_motion_until_backward(CeVimAction_t* action, CeRune_t key){
     return vim_parse_motion_find(action, key, ce_vim_motion_until_backward);
}

static bool motion_direction(const CeView_t* view, CePoint_t delta, const CeConfigOptions_t* config_options,
                             CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_buffer_move_point(view->buffer, motion_range->end, delta, config_options->tab_width, CE_CLAMP_X_NONE);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

static bool motion_range_sort(CeVimMotionRange_t* motion_range){
     if(ce_point_after(motion_range->start, motion_range->end)){
          CePoint_t tmp = motion_range->start;
          motion_range->start = motion_range->end;
          motion_range->end = tmp;
          return true;
     }

     return false;
}

bool ce_vim_motion_left(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                        const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return motion_direction(view, (CePoint_t){-1, 0}, config_options, motion_range);
}

bool ce_vim_motion_right(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                         const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return motion_direction(view, (CePoint_t){1, 0}, config_options, motion_range);
}

bool ce_vim_motion_up(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                      const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     if(action->verb.function != ce_vim_verb_motion){
          if(motion_range->start.y > 0){
               // we use start instead of end so that we can sort them consistently through a motion multiplier
               motion_range->start.y--;
               motion_range->start.x = 0;
               motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
               action->yank_line = true;
               motion_range_sort(motion_range);
          }

          return true;
     }

     return motion_direction(view, (CePoint_t){0, -1}, config_options, motion_range);
}

bool ce_vim_motion_down(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                        const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     if(action->verb.function != ce_vim_verb_motion){
          if(motion_range->end.y < view->buffer->line_count - 1){
               motion_range->end.y++;
               motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
               motion_range->start.x = 0;
               action->yank_line = true;
               motion_range_sort(motion_range);
          }
          return true;
     }

     return motion_direction(view, (CePoint_t){0, 1}, config_options, motion_range);
}

bool ce_vim_motion_little_word(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_big_word(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                            const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_end_little_word(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                   const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_end_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_end_big_word(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_end_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_begin_little_word(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                     const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_begin_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_begin_big_word(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                     const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_begin_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_soft_begin_line(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
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

bool ce_vim_motion_hard_begin_line(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                   const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end = (CePoint_t){0, motion_range->end.y};
     return true;
}

bool ce_vim_motion_end_line(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                            const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     int64_t last_index = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
     motion_range->end = (CePoint_t){last_index, motion_range->end.y};
     return true;
}

bool ce_vim_motion_entire_line(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     int64_t last_index = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
     motion_range->start = (CePoint_t){0, motion_range->end.y};
     motion_range->end = (CePoint_t){last_index, motion_range->end.y};
     return true;
}

bool ce_vim_motion_page_up(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                           const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.y -= (view->rect.bottom - view->rect.top);
     return true;
}

bool ce_vim_motion_page_down(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                             const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.y += (view->rect.bottom - view->rect.top);
     return true;
}

bool ce_vim_motion_half_page_up(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.y -= (view->rect.bottom - view->rect.top) / 2;
     return true;
}

bool ce_vim_motion_half_page_down(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                  const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.y += (view->rect.bottom - view->rect.top) / 2;
     return true;
}

bool ce_vim_motion_visual(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                          const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->start = view->cursor;
     motion_range->end = vim->visual;
     motion_range_sort(motion_range);

     if(vim->mode == CE_VIM_MODE_VISUAL_LINE){
          motion_range->start.x = 0;;
          motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
          action->yank_line = true;
     }

     return true;
}

bool ce_vim_motion_find_forward(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, action->motion.integer, false);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

bool ce_vim_motion_find_backward(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                 const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, action->motion.integer, false);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

bool ce_vim_motion_until_forward(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, action->motion.integer, true);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

bool ce_vim_motion_until_backward(const CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                 const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, action->motion.integer, true);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

static int64_t yank_register_index(CeRune_t rune){
     if(rune < 33 || rune >= 177) return -1; // ascii printable character range
     return rune - '!';
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
     bool do_not_include_end = motion_range_sort(&motion_range);

     if(action->motion.function == ce_vim_motion_little_word ||
        action->motion.function == ce_vim_motion_big_word ||
        action->motion.function == ce_vim_motion_begin_little_word ||
        action->motion.function == ce_vim_motion_begin_big_word){
          do_not_include_end = true;
     }

     // delete the range
     if(do_not_include_end) motion_range.end = ce_buffer_advance_point(view->buffer, motion_range.end, -1);
     int64_t delete_len = ce_buffer_range_len(view->buffer, motion_range.start, motion_range.end);
     char* removed_string = ce_buffer_dupe_string(view->buffer, motion_range.start, delete_len, action->yank_line);
     if(!ce_buffer_remove_string(view->buffer, motion_range.start, delete_len, action->yank_line)){
          free(removed_string);
          return false;
     }

     // commit the change
     CeBufferChange_t change = {};
     change.chain = action->chain_undo;
     change.insertion = false;
     change.remove_line_if_empty = action->yank_line;
     change.string = removed_string;
     change.location = motion_range.start;
     change.cursor_before = view->cursor;
     change.cursor_after = motion_range.start;
     ce_buffer_change(view->buffer, &change);

     view->cursor = motion_range.start;
     vim->chain_undo = action->chain_undo;

     CeVimYank_t* yank = vim->yanks + yank_register_index('"');
     if(yank->text) free(yank->text);
     yank->text = strdup(removed_string);
     yank->line = action->yank_line;
     return true;
}

bool ce_vim_verb_change(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                        const CeConfigOptions_t* config_options){
     if(!ce_vim_verb_delete(vim, action, motion_range, view, config_options)) return false;
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_set_character(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                               const CeConfigOptions_t* config_options){
     motion_range_sort(&motion_range);

     // TODO: CE_NEWLINE doesn't work

     while(!ce_point_after(motion_range.start, motion_range.end)){
          // dupe previous rune
          int64_t rune_len = 0;
          char* str = ce_utf8_find_index(view->buffer->lines[view->cursor.y], motion_range.start.x);
          char* previous_string = malloc(5);
          CeRune_t previous_rune = ce_utf8_decode(str, &rune_len);
          ce_utf8_encode(previous_rune, previous_string, 5, &rune_len);
          previous_string[rune_len] = 0;

          // delete
          if(!ce_buffer_remove_string(view->buffer, motion_range.start, 1, false)) return false; // NOTE: leak previous_string

          // commit the deletion
          CeBufferChange_t change = {};
          change.chain = false;
          change.insertion = false;
          change.remove_line_if_empty = false;
          change.string = previous_string;
          change.location = view->cursor;
          change.cursor_before = view->cursor;
          change.cursor_after = view->cursor;
          ce_buffer_change(view->buffer, &change);

          // dupe current rune
          char* current_string = malloc(5);
          ce_utf8_encode(action->verb.integer, current_string, 5, &rune_len);
          current_string[rune_len] = 0;

          // insert
          if(!ce_buffer_insert_string(view->buffer, current_string, motion_range.start)) return false; // NOTE: leak previous_string

          // commit the insertion
          change.chain = true;
          change.insertion = true;
          change.remove_line_if_empty = true;
          change.string = current_string;
          change.location = view->cursor;
          change.cursor_before = view->cursor;
          change.cursor_after = view->cursor;
          ce_buffer_change(view->buffer, &change);

          motion_range.start = ce_buffer_advance_point(view->buffer, motion_range.start, 1);
          if(motion_range.start.x == -1) return false;
     }

     return true;
}

bool ce_vim_verb_yank(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                      const CeConfigOptions_t* config_options){
     CeVimYank_t* yank = vim->yanks + yank_register_index(action->verb.character);
     if(yank->text) free(yank->text);
     int64_t yank_len = ce_buffer_range_len(view->buffer, motion_range.start, motion_range.end);
     yank->text = ce_buffer_dupe_string(view->buffer, motion_range.start, yank_len, action->yank_line);
     yank->line = action->yank_line;
     return true;
}

static bool paste_text(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                       const CeConfigOptions_t* config_options, bool after){
     CeVimYank_t* yank = vim->yanks + yank_register_index(action->verb.character);
     if(!yank->text) return false;

     CePoint_t insertion_point = motion_range.end;

     if(yank->line){
          insertion_point.x = 0;
          // TODO: insert line if at end of buffer
          if(after) insertion_point.y++;
     }else{
          if(after){
               insertion_point.x++;
               int64_t line_len = ce_utf8_strlen(view->buffer->lines[insertion_point.y]);
               if(insertion_point.x > line_len) insertion_point.x = line_len - 1;
          }
     }

     if(!ce_buffer_insert_string(view->buffer, yank->text, insertion_point)) return false;

     CePoint_t cursor_end;
     if(yank->line){
          cursor_end = insertion_point;
     }else{
          cursor_end = ce_buffer_advance_point(view->buffer, view->cursor, ce_utf8_insertion_strlen(yank->text));
     }

     // commit the change
     CeBufferChange_t change = {};
     change.chain = action->chain_undo;
     change.insertion = true;
     change.remove_line_if_empty = true;
     change.string = strdup(yank->text);
     change.location = insertion_point;
     change.cursor_before = view->cursor;
     change.cursor_after = cursor_end;
     ce_buffer_change(view->buffer, &change);

     view->cursor = cursor_end;

     return true;
}

bool ce_vim_verb_paste_before(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                              const CeConfigOptions_t* config_options){
     return paste_text(vim, action, motion_range, view, config_options, false);
}

bool ce_vim_verb_paste_after(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                             const CeConfigOptions_t* config_options){
     return paste_text(vim, action, motion_range, view, config_options, true);
}

bool ce_vim_verb_open_above(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                            const CeConfigOptions_t* config_options){
     // TODO: insert whitespace for indentation
     // insert newline on next line
     char* insert_string = strdup("\n");
     motion_range.start.x = 0;
     if(!ce_buffer_insert_string(view->buffer, insert_string, motion_range.start)) return false;

     int64_t indentation = 0;
     CePoint_t cursor_end = {indentation, motion_range.start.y};

     // commit the change
     CeBufferChange_t change = {};
     change.chain = action->chain_undo;
     change.insertion = true;
     change.remove_line_if_empty = true;
     change.string = insert_string;
     change.location = (CePoint_t){0, cursor_end.y}; // lie about where we inserted the newline so we remove the complete line
     change.cursor_before = view->cursor;
     change.cursor_after = cursor_end;
     ce_buffer_change(view->buffer, &change);

     view->cursor = cursor_end;
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_open_below(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                            const CeConfigOptions_t* config_options){
     // TODO: insert whitespace for indentation
     // insert newline on next line
     char* insert_string = strdup("\n");
     motion_range.start.x = ce_utf8_strlen(view->buffer->lines[motion_range.start.y]);
     if(!ce_buffer_insert_string(view->buffer, insert_string, motion_range.start)) return false;

     int64_t indentation = 0;
     CePoint_t cursor_end = {indentation, motion_range.start.y + 1};

     // commit the change
     CeBufferChange_t change = {};
     change.chain = action->chain_undo;
     change.insertion = true;
     change.remove_line_if_empty = true;
     change.string = insert_string;
     change.location = (CePoint_t){0, cursor_end.y}; // lie about where we inserted the newline so we remove the complete line
     change.cursor_before = view->cursor;
     change.cursor_after = cursor_end;
     ce_buffer_change(view->buffer, &change);

     view->cursor = cursor_end;
     insert_mode(vim);
     insert_mode(vim);
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

bool ce_vim_verb_insert_mode(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                             const CeConfigOptions_t* config_options){
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_visual_mode(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                             const CeConfigOptions_t* config_options){
     vim->visual = view->cursor;
     vim->mode = CE_VIM_MODE_VISUAL;
     return true;
}

bool ce_vim_verb_visual_line_mode(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                             const CeConfigOptions_t* config_options){
     vim->visual = view->cursor;
     vim->mode = CE_VIM_MODE_VISUAL_LINE;
     return true;
}

bool ce_vim_verb_normal_mode(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                             const CeConfigOptions_t* config_options){
     vim->mode = CE_VIM_MODE_NORMAL;
     return true;
}

bool ce_vim_verb_last_action(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                             const CeConfigOptions_t* config_options){
     vim->verb_last_action = true;
     if(!ce_vim_apply_action(vim, &vim->last_action, view, config_options)){
          vim->verb_last_action = false;
          return false;
     }

     view->buffer->change_node->change.chain = false;

     if(vim->insert_rune_head){
          CeRune_t* rune_string = ce_rune_node_string(vim->insert_rune_head);
          CeRune_t* itr = rune_string;

          while(*itr){
               ce_vim_handle_key(vim, view, *itr, config_options);
               itr++;
          }

          free(rune_string);
     }

     vim->verb_last_action = false;
     return true;
}
