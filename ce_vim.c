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

void ce_vim_add_key_bind(CeVim_t* vim, CeRune_t key, CeVimParseFunc_t* function){
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

     // TODO: 's', 'S', ctrl + a, ctrl + x
     ce_vim_add_key_bind(vim, 'w', &ce_vim_parse_motion_little_word);
     ce_vim_add_key_bind(vim, 'W', &ce_vim_parse_motion_big_word);
     ce_vim_add_key_bind(vim, 'e', &ce_vim_parse_motion_end_little_word);
     ce_vim_add_key_bind(vim, 'E', &ce_vim_parse_motion_end_big_word);
     ce_vim_add_key_bind(vim, 'b', &ce_vim_parse_motion_begin_little_word);
     ce_vim_add_key_bind(vim, 'B', &ce_vim_parse_motion_begin_big_word);
     ce_vim_add_key_bind(vim, 'h', &ce_vim_parse_motion_left);
     ce_vim_add_key_bind(vim, 'l', &ce_vim_parse_motion_right);
     ce_vim_add_key_bind(vim, 'k', &ce_vim_parse_motion_up);
     ce_vim_add_key_bind(vim, 'j', &ce_vim_parse_motion_down);
     ce_vim_add_key_bind(vim, '^', &ce_vim_parse_motion_soft_begin_line);
     ce_vim_add_key_bind(vim, '0', &ce_vim_parse_motion_hard_begin_line);
     ce_vim_add_key_bind(vim, '$', &ce_vim_parse_motion_end_line);
     ce_vim_add_key_bind(vim, 'f', &ce_vim_parse_motion_find_forward);
     ce_vim_add_key_bind(vim, 'F', &ce_vim_parse_motion_find_backward);
     ce_vim_add_key_bind(vim, 't', &ce_vim_parse_motion_until_forward);
     ce_vim_add_key_bind(vim, 'T', &ce_vim_parse_motion_until_backward);
     ce_vim_add_key_bind(vim, 'i', &ce_vim_parse_motion_inside);
     ce_vim_add_key_bind(vim, 'a', &ce_vim_parse_motion_around);
     ce_vim_add_key_bind(vim, 'G', &ce_vim_parse_motion_end_of_file);
     ce_vim_add_key_bind(vim, 'n', &ce_vim_parse_motion_search_next);
     ce_vim_add_key_bind(vim, 'N', &ce_vim_parse_motion_search_prev);
     ce_vim_add_key_bind(vim, '*', &ce_vim_parse_motion_search_word_forward);
     ce_vim_add_key_bind(vim, '#', &ce_vim_parse_motion_search_word_backward);
     ce_vim_add_key_bind(vim, 'i', &ce_vim_parse_verb_insert_mode);
     ce_vim_add_key_bind(vim, 'v', &ce_vim_parse_verb_visual_mode);
     ce_vim_add_key_bind(vim, 'V', &ce_vim_parse_verb_visual_line_mode);
     ce_vim_add_key_bind(vim, 27, &ce_vim_parse_verb_normal_mode);
     ce_vim_add_key_bind(vim, 'a', &ce_vim_parse_verb_append);
     ce_vim_add_key_bind(vim, 'A', &ce_vim_parse_verb_append_at_end_of_line);
     ce_vim_add_key_bind(vim, 'd', &ce_vim_parse_verb_delete);
     ce_vim_add_key_bind(vim, 'D', &ce_vim_parse_verb_delete_to_end_of_line);
     ce_vim_add_key_bind(vim, 'c', &ce_vim_parse_verb_change);
     ce_vim_add_key_bind(vim, 'C', &ce_vim_parse_verb_change_to_end_of_line);
     ce_vim_add_key_bind(vim, 'r', &ce_vim_parse_verb_set_character);
     ce_vim_add_key_bind(vim, 'x', &ce_vim_parse_verb_delete_character);
     ce_vim_add_key_bind(vim, 's', &ce_vim_parse_verb_substitute_character);
     ce_vim_add_key_bind(vim, 'S', &ce_vim_parse_verb_substitute_soft_begin_line);
     ce_vim_add_key_bind(vim, 'y', &ce_vim_parse_verb_yank);
     ce_vim_add_key_bind(vim, '"', &ce_vim_parse_select_yank_register);
     ce_vim_add_key_bind(vim, 'P', &ce_vim_parse_verb_paste_before);
     ce_vim_add_key_bind(vim, 'p', &ce_vim_parse_verb_paste_after);
     ce_vim_add_key_bind(vim, 'u', &ce_vim_parse_verb_undo);
     ce_vim_add_key_bind(vim, KEY_REDO, &ce_vim_parse_verb_redo);
     ce_vim_add_key_bind(vim, 'O', &ce_vim_parse_verb_open_above);
     ce_vim_add_key_bind(vim, 'o', &ce_vim_parse_verb_open_below);
     ce_vim_add_key_bind(vim, '.', &ce_vim_parse_verb_last_action);
     ce_vim_add_key_bind(vim, 'z', &ce_vim_parse_verb_z_command);
     ce_vim_add_key_bind(vim, 'g', &ce_vim_parse_verb_g_command);
     ce_vim_add_key_bind(vim, '>', &ce_vim_parse_verb_indent);
     ce_vim_add_key_bind(vim, '<', &ce_vim_parse_verb_unindent);
     ce_vim_add_key_bind(vim, 'J', &ce_vim_parse_verb_join);
     ce_vim_add_key_bind(vim, 2, &ce_vim_parse_motion_page_up);
     ce_vim_add_key_bind(vim, 6, &ce_vim_parse_motion_page_down);
     ce_vim_add_key_bind(vim, 21, &ce_vim_parse_motion_half_page_up);
     ce_vim_add_key_bind(vim, 4, &ce_vim_parse_motion_half_page_down);

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

bool ce_vim_append_key(CeVim_t* vim, CeRune_t key){
     int64_t command_len = istrlen(vim->current_command);
     if(command_len < (CE_VIM_MAX_COMMAND_LEN - 1)){
          vim->current_command[command_len] = key;
          vim->current_command[command_len + 1] = 0;
          return true;
     }

     return false;
}

CeVimParseResult_t ce_vim_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, const CeConfigOptions_t* config_options){
     switch(vim->mode){
     default:
          return CE_VIM_PARSE_INVALID;
     case CE_VIM_MODE_INSERT:
          if(!vim->verb_last_action) ce_rune_node_insert(&vim->insert_rune_head, key);

          switch(key){
          default:
               if(isprint(key) || key == CE_NEWLINE || key == '\t'){
                    if(ce_buffer_insert_rune(view->buffer, key, view->cursor)){
                         const char str[2] = {key, 0};
                         CePoint_t new_cursor = ce_buffer_advance_point(view->buffer, view->cursor, 1);

                         // TODO: convenience function
                         CeBufferChange_t change = {};
                         change.chain = vim->chain_undo;
                         change.insertion = true;
                         change.remove_line_if_empty = (key == CE_NEWLINE);
                         change.string = strdup(str);
                         change.location = view->cursor;
                         change.cursor_before = view->cursor;
                         change.cursor_after = new_cursor;
                         ce_buffer_change(view->buffer, &change);

                         view->cursor = new_cursor;
                         vim->chain_undo = true;

                         // indent after newline
                         if(key == CE_NEWLINE){
                              // calc indentation
                              CePoint_t indentation_point = {0, view->cursor.y};
                              int64_t indentation = ce_vim_get_indentation(view->buffer, indentation_point, config_options->tab_width);
                              CePoint_t cursor_end = {indentation, indentation_point.y};

                              // build indentation string
                              char* insert_string = malloc(indentation + 1);
                              memset(insert_string, ' ', indentation);
                              insert_string[indentation] = 0;

                              // insert indentation
                              if(!ce_buffer_insert_string(view->buffer, insert_string, indentation_point)) return false;

                              // commit the change
                              change.chain = true;
                              change.insertion = true;
                              change.remove_line_if_empty = false;
                              change.string = insert_string;
                              change.location = indentation_point;
                              change.cursor_before = view->cursor;
                              change.cursor_after = cursor_end;
                              ce_buffer_change(view->buffer, &change);

                              view->cursor = cursor_end;
                         }
                    }
               }
               break;
          case KEY_BACKSPACE:
               if(!ce_points_equal(view->cursor, (CePoint_t){0, 0})){
                    CePoint_t remove_point;
                    char* removed_string;
                    CePoint_t end_cursor;
                    int64_t remove_len = 1;

                    if(view->cursor.x == 0){
                         int64_t line = view->cursor.y - 1;
                         end_cursor = (CePoint_t){ce_utf8_strlen(view->buffer->lines[line]), line};
                         ce_vim_join_next_line(view->buffer, view->cursor.y - 1, view->cursor, vim->chain_undo);
                    }else{
                         remove_point = ce_buffer_advance_point(view->buffer, view->cursor, -1);
                         end_cursor = remove_point;
                         removed_string = ce_buffer_dupe_string(view->buffer, remove_point, remove_len, false);

                         if(ce_buffer_remove_string(view->buffer, remove_point, remove_len, false)){
                              // TODO: convenience function
                              CeBufferChange_t change = {};
                              change.chain = vim->chain_undo;
                              change.insertion = false;
                              change.remove_line_if_empty = false;
                              change.string = removed_string;
                              change.location = remove_point;
                              change.cursor_before = view->cursor;
                              change.cursor_after = end_cursor;
                              ce_buffer_change(view->buffer, &change);

                         }
                    }

                    view->cursor = end_cursor;
                    vim->chain_undo = true;
               }
               break;
          case KEY_LEFT:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){-1, 0},
                                                   config_options->tab_width, CE_CLAMP_X_ON);
               ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off,
                                     config_options->tab_width);
               vim->chain_undo = false;
               break;
          case KEY_DOWN:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){0, 1},
                                                   config_options->tab_width, CE_CLAMP_X_ON);
               ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off,
                                     config_options->tab_width);
               vim->chain_undo = false;
               break;
          case KEY_UP:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){0, -1},
                                                   config_options->tab_width, CE_CLAMP_X_ON);
               ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off,
                                     config_options->tab_width);
               vim->chain_undo = false;
               break;
          case KEY_RIGHT:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){1, 0},
                                                   config_options->tab_width, CE_CLAMP_X_ON);
               ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off,
                                     config_options->tab_width);
               vim->chain_undo = false;
               break;
          case 27: // escape
               vim->mode = CE_VIM_MODE_NORMAL;
               vim->chain_undo = false;
               break;
          }
          break;
     case CE_VIM_MODE_NORMAL:
     case CE_VIM_MODE_VISUAL:
     case CE_VIM_MODE_VISUAL_LINE:
     {
          CeVimAction_t action = {};

          if(!ce_vim_append_key(vim, key)){
               vim->current_command[0] = 0;
               break;
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
     }
     if(action->verb.function){
          if(!action->verb.function(vim, action, motion_range, view, config_options)){
               return false;
          }
     }
     return true;
}

static bool is_little_word_character(int c){
     return isalnum(c) || c == '_';
}

typedef enum{
     WORD_INSIDE_WORD,
     WORD_INSIDE_SPACE,
     WORD_INSIDE_OTHER,
     WORD_NEW_LINE,
}WordState_t;

int64_t ce_vim_soft_begin_line(CeBuffer_t* buffer, int64_t line){
     if(line < 0 || line >= buffer->line_count) return -1;

     const char* itr = buffer->lines[line];
     int64_t index = 0;
     int64_t rune_len = 0;
     CeRune_t rune = ce_utf8_decode(itr, &rune_len);

     while(true){
          rune = ce_utf8_decode(itr, &rune_len);
          itr += rune_len;
          if(isspace(rune)) index++;
          else break;
     }

     return index;
}

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
               if(state == WORD_INSIDE_WORD) break;
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

CeVimMotionRange_t ce_vim_find_little_word_boundaries(CeBuffer_t* buffer, CePoint_t start){
     CeVimMotionRange_t range = {(CePoint_t){-1, -1}, (CePoint_t){-1, -1}};
     char* line_start = buffer->lines[start.y];
     char* itr = ce_utf8_find_index(line_start, start.x);
     char* save_start = itr;
     if(!is_little_word_character(*itr)) return range;

     int64_t end_x = start.x - 1;
     while(*itr){
          int64_t rune_len = 0;
          CeRune_t rune = ce_utf8_decode(itr, &rune_len);
          if(!is_little_word_character(rune)) break;
          end_x++;
          itr += rune_len;
     }

     int64_t start_x = start.x + 1;
     itr = save_start;
     while(itr > line_start){
          int64_t rune_len = 0;
          CeRune_t rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);
          if(!is_little_word_character(rune)) break;
          start_x--;
          itr -= rune_len;
     }

     if(start_x > end_x) return range;

     range.start.x = start_x;
     range.start.y = start.y;
     range.end.x = end_x;
     range.end.y = start.y;

     return range;
}

CeVimMotionRange_t ce_vim_find_big_word_boundaries(CeBuffer_t* buffer, CePoint_t start){
     CeVimMotionRange_t range = {(CePoint_t){-1, -1}, (CePoint_t){-1, -1}};
     char* line_start = buffer->lines[start.y];
     char* itr = ce_utf8_find_index(line_start, start.x);
     char* save_start = itr;
     if(!is_little_word_character(*itr)) return range;

     int64_t end_x = start.x - 1;
     while(*itr){
          int64_t rune_len = 0;
          CeRune_t rune = ce_utf8_decode(itr, &rune_len);
          if(isspace(rune)) break;
          end_x++;
          itr += rune_len;
     }

     int64_t start_x = start.x + 1;
     itr = save_start;
     while(itr > line_start){
          int64_t rune_len = 0;
          CeRune_t rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);
          if(isspace(rune)) break;
          start_x--;
          itr -= rune_len;
     }

     if(start_x > end_x) return range;

     range.start.x = start_x;
     range.start.y = start.y;
     range.end.x = end_x;
     range.end.y = start.y;

     return range;
}

CeVimMotionRange_t ce_vim_find_pair(CeBuffer_t* buffer, CePoint_t start, CeRune_t rune, bool inside){
     CeVimMotionRange_t range = {(CePoint_t){-1, -1}, (CePoint_t){-1, -1}};
     if(!ce_buffer_point_is_valid(buffer, start)) return range;
     CeRune_t left_match;
     CeRune_t right_match;

     switch(rune){
     default:
          return range;
     case ')':
     case '(':
          left_match = '(';
          right_match = ')';
          break;
     case '{':
     case '}':
          left_match = '{';
          right_match = '}';
          break;
     case '[':
     case ']':
          left_match = '[';
          right_match = ']';
          break;
     case 'w':
          return ce_vim_find_little_word_boundaries(buffer, start);
     case 'W':
          return ce_vim_find_big_word_boundaries(buffer, start);
     }

     int64_t match_count = 0;

     // find left match
     CePoint_t itr = start;
     CePoint_t prev = itr;
     CePoint_t new_start = range.start;
     while(true){
          CeRune_t buffer_rune = ce_buffer_get_rune(buffer, itr);
          if(buffer_rune == left_match){
               match_count--;
               if(match_count < 0){
                    new_start = inside ? prev : itr;
                    break;
               }
          }else if(buffer_rune == right_match){
               match_count++;
          }
          if(itr.x == 0 && itr.y == 0) return range;
          prev = itr;
          itr = ce_buffer_advance_point(buffer, itr, -1);
     }

     // find right match
     itr = start;
     prev = itr;
     match_count = 0;
     CePoint_t new_end = range.end;
     CePoint_t end_of_buffer = {ce_utf8_last_index(buffer->lines[buffer->line_count - 1]), buffer->line_count - 1};
     while(true){
          CeRune_t buffer_rune = ce_buffer_get_rune(buffer, itr);
          if(buffer_rune == right_match){
               match_count--;
               if(match_count < 0){
                    new_end = inside ? prev : itr;
                    break;
               }
          }else if(buffer_rune == left_match){
               match_count++;
          }
          if(itr.x == end_of_buffer.x && itr.y == end_of_buffer.y) break;
          prev = itr;
          itr = ce_buffer_advance_point(buffer, itr, 1);
     }

     range.start = new_start;
     range.end = new_end;
     return range;
}

int64_t ce_vim_get_indentation(CeBuffer_t* buffer, CePoint_t point, int64_t tab_length){
     CeVimMotionRange_t brace_range = ce_vim_find_pair(buffer, point, '{', false);
     CeVimMotionRange_t paren_range = ce_vim_find_pair(buffer, point, '(', false);
     if(brace_range.start.x < 0 && paren_range.start.x < 0) return 0;
     int64_t indent = 0;
     if(ce_point_after(paren_range.start, brace_range.start)){
          indent = paren_range.start.x + 1;
     }else{
          indent = ce_vim_soft_begin_line(buffer, brace_range.start.y) + tab_length;
     }
     return indent;
}

bool ce_vim_join_next_line(CeBuffer_t* buffer, int64_t line, CePoint_t cursor, bool chain_undo){
     CePoint_t point = {ce_utf8_strlen(buffer->lines[line]), line};
     if(!ce_buffer_remove_string(buffer, point, 0, false)) return false;
     CePoint_t after_point = ce_buffer_advance_point(buffer, point, 1);

     CeBufferChange_t change = {};
     change.chain = chain_undo;
     change.insertion = false;
     change.remove_line_if_empty = false;
     change.string = strdup("\n");
     change.location = point;
     change.cursor_before = cursor;
     change.cursor_after = after_point;
     ce_buffer_change(buffer, &change);

     return true;
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

CeVimParseResult_t ce_vim_parse_verb_append(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_append;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_append_at_end_of_line(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_append_at_end_of_line;
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
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     action->motion.function = ce_vim_motion_up;

     if(action->verb.function){
          action->yank_line = true;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = ce_vim_verb_motion;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_motion_down(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     action->motion.function = ce_vim_motion_down;

     if(action->verb.function){
          action->yank_line = true;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = ce_vim_verb_motion;
     return CE_VIM_PARSE_COMPLETE;
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

CeVimParseResult_t ce_vim_parse_motion_inside(CeVimAction_t* action, CeRune_t key){
     if(!action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     if(action->motion.function == NULL){
          action->motion.function = &ce_vim_motion_inside;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->motion.function == &ce_vim_motion_inside){
          action->motion.integer = key;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_motion_around(CeVimAction_t* action, CeRune_t key){
     // around requires a verb
     if(!action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     if(action->motion.function == NULL){
          action->motion.function = &ce_vim_motion_around;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->motion.function == &ce_vim_motion_around){
          action->motion.integer = key;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
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

CeVimParseResult_t ce_vim_parse_motion_end_of_file(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_end_of_file);
}

CeVimParseResult_t ce_vim_parse_motion_search_next(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_search_next);
}

CeVimParseResult_t ce_vim_parse_motion_search_prev(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_search_prev);
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

CeVimParseResult_t ce_vim_parse_verb_delete_to_end_of_line(CeVimAction_t* action, CeRune_t key){
     action->motion.function = &ce_vim_motion_end_line;
     action->verb.function = &ce_vim_verb_delete;
     action->chain_undo = true;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_change(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == ce_vim_verb_change){
          action->verb.function = &ce_vim_verb_substitute_soft_begin_line;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = &ce_vim_verb_change;
     action->chain_undo = true;
     return CE_VIM_PARSE_IN_PROGRESS;
}

CeVimParseResult_t ce_vim_parse_verb_change_to_end_of_line(CeVimAction_t* action, CeRune_t key){
     action->motion.function = &ce_vim_motion_end_line;
     action->verb.function = &ce_vim_verb_change;
     action->chain_undo = true;
     return CE_VIM_PARSE_COMPLETE;
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

CeVimParseResult_t ce_vim_parse_verb_delete_character(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_delete_character;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_substitute_character(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_substitute_character;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_substitute_soft_begin_line(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_substitute_soft_begin_line;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_yank(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == &ce_vim_verb_yank){
          action->motion.function = &ce_vim_motion_entire_line;
          action->yank_line = true;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = &ce_vim_verb_yank;
     if(action->verb.integer == 0) action->verb.integer = '"';
     return CE_VIM_PARSE_IN_PROGRESS;
}

CeVimParseResult_t ce_vim_parse_verb_paste_before(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_paste_before;
     if(action->verb.integer == 0) action->verb.integer = '"';
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_paste_after(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_paste_after;
     if(action->verb.integer == 0) action->verb.integer = '"';
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

CeVimParseResult_t ce_vim_parse_verb_join(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_join;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_select_yank_register(CeVimAction_t* action, CeRune_t key){
     if(action->verb.integer == 0){
          action->verb.integer = '"';
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->verb.integer == '"'){
          action->verb.integer = key;
          return CE_VIM_PARSE_CONTINUE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_last_action(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_last_action;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_z_command(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == NULL){
          action->verb.function = ce_vim_verb_z_command;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->verb.function == ce_vim_verb_z_command){
          action->verb.integer = key;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_g_command(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == NULL){
          action->verb.function = ce_vim_verb_g_command;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->verb.function == ce_vim_verb_g_command){
          action->verb.integer = key;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_indent(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == NULL){
          action->verb.function = ce_vim_verb_indent;
          return CE_VIM_PARSE_IN_PROGRESS;
     }else if(action->verb.function == ce_vim_verb_indent){
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_unindent(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == NULL){
          action->verb.function = ce_vim_verb_unindent;
          return CE_VIM_PARSE_IN_PROGRESS;
     }else if(action->verb.function == ce_vim_verb_unindent){
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_motion_search_word_forward(CeVimAction_t* action, CeRune_t key){
     action->motion.function = ce_vim_motion_search_word_forward;
     action->verb.function = ce_vim_verb_motion;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_motion_search_word_backward(CeVimAction_t* action, CeRune_t key){
     action->motion.function = ce_vim_motion_search_word_backward;
     action->verb.function = ce_vim_verb_motion;
     return CE_VIM_PARSE_COMPLETE;
}

static bool motion_direction(const CeView_t* view, CePoint_t delta, const CeConfigOptions_t* config_options,
                             CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_buffer_move_point(view->buffer, motion_range->end, delta, config_options->tab_width, CE_CLAMP_X_NONE);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_range_sort(CeVimMotionRange_t* motion_range){
     if(ce_point_after(motion_range->start, motion_range->end)){
          CePoint_t tmp = motion_range->start;
          motion_range->start = motion_range->end;
          motion_range->end = tmp;
          return true;
     }

     return false;
}

bool ce_vim_motion_left(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                        const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return motion_direction(view, (CePoint_t){-1, 0}, config_options, motion_range);
}

bool ce_vim_motion_right(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                         const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return motion_direction(view, (CePoint_t){1, 0}, config_options, motion_range);
}

bool ce_vim_motion_up(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                      const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     if(action->verb.function != ce_vim_verb_motion){
          if(motion_range->start.y > 0){
               // we use start instead of end so that we can sort them consistently through a motion multiplier
               motion_range->start.y--;
               motion_range->start.x = 0;
               motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
               ce_vim_motion_range_sort(motion_range);
          }

          return true;
     }

     return motion_direction(view, (CePoint_t){0, -1}, config_options, motion_range);
}

bool ce_vim_motion_down(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                        const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     if(action->verb.function != ce_vim_verb_motion){
          if(motion_range->end.y < view->buffer->line_count - 1){
               motion_range->end.y++;
               motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
               motion_range->start.x = 0;
               ce_vim_motion_range_sort(motion_range);
          }
          return true;
     }

     return motion_direction(view, (CePoint_t){0, 1}, config_options, motion_range);
}

bool ce_vim_motion_little_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_big_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                            const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_end_little_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                   const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_end_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_end_big_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_end_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_begin_little_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                     const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_begin_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_begin_big_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                     const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t destination = ce_vim_move_begin_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_soft_begin_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                   const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     int64_t result = ce_vim_soft_begin_line(view->buffer, motion_range->end.y);
     if(result < 0) return false;
     motion_range->end.x = result;
     return true;
}

bool ce_vim_motion_hard_begin_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                   const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end = (CePoint_t){0, motion_range->end.y};
     return true;
}

bool ce_vim_motion_end_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                            const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
     return true;
}

bool ce_vim_motion_entire_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     int64_t last_index = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
     motion_range->start = (CePoint_t){0, motion_range->end.y};
     motion_range->end = (CePoint_t){last_index, motion_range->end.y};
     return true;
}

bool ce_vim_motion_page_up(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                           const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.y -= (view->rect.bottom - view->rect.top);
     return true;
}

bool ce_vim_motion_page_down(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                             const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.y += (view->rect.bottom - view->rect.top);
     return true;
}

bool ce_vim_motion_half_page_up(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.y -= (view->rect.bottom - view->rect.top) / 2;
     return true;
}

bool ce_vim_motion_half_page_down(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                  const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.y += (view->rect.bottom - view->rect.top) / 2;
     return true;
}

bool ce_vim_motion_visual(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                          const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     if(vim->mode == CE_VIM_MODE_VISUAL || vim->mode == CE_VIM_MODE_VISUAL_LINE){
          motion_range->start = view->cursor;
          motion_range->end = vim->visual;
          ce_vim_motion_range_sort(motion_range);
          action->motion.integer = ce_buffer_range_len(view->buffer, motion_range->start, motion_range->end);

          if(vim->mode == CE_VIM_MODE_VISUAL_LINE){
               motion_range->start.x = 0;;
               motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
               action->yank_line = true;
               action->motion.integer = motion_range->end.y - motion_range->start.y;
          }
     }else if(vim->verb_last_action){
          if(action->yank_line){
               motion_range->start = (CePoint_t){0, view->cursor.y};
               motion_range->end.y = view->cursor.y + action->motion.integer;
               motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
          }else{
               motion_range->start = view->cursor;
               motion_range->end = ce_buffer_advance_point(view->buffer, view->cursor, action->motion.integer);
          }
     }

     return true;
}

bool ce_vim_motion_find_forward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, action->motion.integer, false);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

bool ce_vim_motion_find_backward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                 const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, action->motion.integer, false);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

bool ce_vim_motion_until_forward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, action->motion.integer, true);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

bool ce_vim_motion_until_backward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                 const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, action->motion.integer, true);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

static bool vim_motion_find(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                            const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range, bool inside){
     CeVimMotionRange_t new_range = ce_vim_find_pair(view->buffer, motion_range->end, action->motion.integer, inside);
     if(new_range.start.x < 0) return false;
     *motion_range = new_range;
     return true;
}

bool ce_vim_motion_inside(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                          const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return vim_motion_find(vim, action, view, config_options, motion_range, true);
}

bool ce_vim_motion_around(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                          const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     return vim_motion_find(vim, action, view, config_options, motion_range, false);
}

bool ce_vim_motion_end_of_file(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     motion_range->end.x = 0;
     motion_range->end.y = view->buffer->line_count;
     if(motion_range->end.y){
          motion_range->end.y--;
          motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
     }
     return true;
}

bool ce_vim_motion_search_next(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     const CeVimYank_t* yank = vim->yanks + ce_vim_yank_register_index('/');
     if(!yank->text) return false;
     CePoint_t result = {-1, -1};
     switch(vim->search_mode){
     default:
          return false;
     case CE_VIM_SEARCH_MODE_FORWARD:
     {
          CePoint_t start = ce_buffer_advance_point(view->buffer, motion_range->end, 1);
          result = ce_buffer_search_forward(view->buffer, start, yank->text);
     } break;
     case CE_VIM_SEARCH_MODE_BACKWARD:
     {
          CePoint_t start = ce_buffer_advance_point(view->buffer, motion_range->end, -1);
          result = ce_buffer_search_backward(view->buffer, start, yank->text);
     } break;
     case CE_VIM_SEARCH_MODE_REGEX_FORWARD:
     {
          CePoint_t start = ce_buffer_advance_point(view->buffer, motion_range->end, 1);
          regex_t regex = {};
          int rc = regcomp(&regex, yank->text, REG_EXTENDED);
          if(rc != 0){
               char error_buffer[BUFSIZ];
               regerror(rc, &regex, error_buffer, BUFSIZ);
               ce_log("regcomp() failed: '%s'", error_buffer);
          }else{
               CeRegexSearchResult_t regex_result = ce_buffer_regex_search_forward(view->buffer, start, &regex);
               result = regex_result.point;
          }
     } break;
     case CE_VIM_SEARCH_MODE_REGEX_BACKWARD:
     {
          CePoint_t start = ce_buffer_advance_point(view->buffer, motion_range->end, -1);
          regex_t regex = {};
          int rc = regcomp(&regex, yank->text, REG_EXTENDED);
          if(rc != 0){
               char error_buffer[BUFSIZ];
               regerror(rc, &regex, error_buffer, BUFSIZ);
               ce_log("regcomp() failed: '%s'", error_buffer);
          }else{
               CeRegexSearchResult_t regex_result = ce_buffer_regex_search_backward(view->buffer, start, &regex);
               result = regex_result.point;
          }
     } break;
     }
     if(result.x < 0) return false;
     motion_range->end = result;
     return true;
}

bool ce_vim_motion_search_prev(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                               const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     const CeVimYank_t* yank = vim->yanks + ce_vim_yank_register_index('/');
     if(!yank->text) return false;
     CePoint_t result = {-1, -1};
     switch(vim->search_mode){
     default:
          return false;
     case CE_VIM_SEARCH_MODE_FORWARD:
     {
          CePoint_t start = ce_buffer_advance_point(view->buffer, motion_range->end, -1);
          result = ce_buffer_search_backward(view->buffer, start, yank->text);
     } break;
     case CE_VIM_SEARCH_MODE_BACKWARD:
     {
          CePoint_t start = ce_buffer_advance_point(view->buffer, motion_range->end, 1);
          result = ce_buffer_search_forward(view->buffer, start, yank->text);
     } break;
     case CE_VIM_SEARCH_MODE_REGEX_FORWARD:
     {
          CePoint_t start = ce_buffer_advance_point(view->buffer, motion_range->end, -1);
          regex_t regex = {};
          int rc = regcomp(&regex, yank->text, REG_EXTENDED);
          if(rc != 0){
               char error_buffer[BUFSIZ];
               regerror(rc, &regex, error_buffer, BUFSIZ);
               ce_log("regcomp() failed: '%s'", error_buffer);
          }else{
               CeRegexSearchResult_t regex_result = ce_buffer_regex_search_backward(view->buffer, start, &regex);
               result = regex_result.point;
          }
     } break;
     case CE_VIM_SEARCH_MODE_REGEX_BACKWARD:
     {
          CePoint_t start = ce_buffer_advance_point(view->buffer, motion_range->end, 1);
          regex_t regex = {};
          int rc = regcomp(&regex, yank->text, REG_EXTENDED);
          if(rc != 0){
               char error_buffer[BUFSIZ];
               regerror(rc, &regex, error_buffer, BUFSIZ);
               ce_log("regcomp() failed: '%s'", error_buffer);
          }else{
               CeRegexSearchResult_t regex_result = ce_buffer_regex_search_forward(view->buffer, start, &regex);
               result = regex_result.point;
          }
     } break;
     }
     if(result.x < 0) return false;
     motion_range->end = result;
     return true;
}

static void search_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, CeVimMotionRange_t* motion_range){
     CeVimYank_t* yank = vim->yanks + ce_vim_yank_register_index('/');
     free(yank->text);
     *motion_range = ce_vim_find_pair(view->buffer, view->cursor, 'w', true);
     int64_t word_len = ce_buffer_range_len(view->buffer, motion_range->start, motion_range->end);
     char* word = ce_buffer_dupe_string(view->buffer, motion_range->start, word_len, false);
     int64_t search_len = word_len + 4;
     yank->text = malloc(search_len + 1);
     snprintf(yank->text, search_len + 1, "\\b%s\\b", word); // TODO: this doesn't work on other platforms like macos
     yank->text[search_len] = 0;
     free(word);
     yank->line = false;
     vim->search_mode = CE_VIM_SEARCH_MODE_REGEX_FORWARD;
}

bool ce_vim_motion_search_word_forward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                       const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     search_word(vim, action, view, motion_range);
     return ce_vim_motion_search_next(vim, action, view, config_options, motion_range);
}

bool ce_vim_motion_search_word_backward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view,
                                       const CeConfigOptions_t* config_options, CeVimMotionRange_t* motion_range){
     search_word(vim, action, view, motion_range);
     motion_range->end = motion_range->start;
     return ce_vim_motion_search_prev(vim, action, view, config_options, motion_range);
}

int64_t ce_vim_yank_register_index(CeRune_t rune){
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
     CePoint_t before_follow = view->scroll;
     ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off,
                           config_options->tab_width);

     // if we are searching, and our cursor goes off the screen, center it
     if(!ce_points_equal(before_follow, view->scroll) &&
        (action->motion.function == ce_vim_motion_search_next ||
         action->motion.function == ce_vim_motion_search_prev)){
          int64_t view_height = view->rect.bottom - view->rect.top;
          ce_view_scroll_to(view, (CePoint_t){0, view->cursor.y - (view_height / 2)});
     }

     return true;
}

bool ce_vim_verb_delete(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                        const CeConfigOptions_t* config_options){
     bool do_not_include_end = ce_vim_motion_range_sort(&motion_range);

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
     vim->mode = CE_VIM_MODE_NORMAL;

     CeVimYank_t* yank = vim->yanks + ce_vim_yank_register_index('"');
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
     ce_vim_motion_range_sort(&motion_range);

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

bool ce_vim_verb_delete_character(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                                  const CeConfigOptions_t* config_options){
     ce_vim_motion_range_sort(&motion_range);

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

          motion_range.start = ce_buffer_advance_point(view->buffer, motion_range.start, 1);
     }

     return true;
}

bool ce_vim_verb_substitute_character(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                                      const CeConfigOptions_t* config_options){
     bool success = ce_vim_verb_delete_character(vim, action, motion_range, view, config_options);
     if(success){
          vim->chain_undo = true;
          vim->mode = CE_VIM_MODE_INSERT;
     }
     return success;
}

bool ce_vim_verb_substitute_soft_begin_line(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                                            const CeConfigOptions_t* config_options){
     int64_t soft_begin_index = ce_vim_soft_begin_line(view->buffer, view->cursor.y);
     if(soft_begin_index < 0) return false;

     view->cursor.x = soft_begin_index;
     motion_range.start = view->cursor;
     motion_range.end.x = ce_utf8_last_index(view->buffer->lines[motion_range.end.y]);
     bool success = ce_vim_verb_change(vim, action, motion_range, view, config_options);
     if(success){
          vim->chain_undo = true;
          vim->mode = CE_VIM_MODE_INSERT;
     }
     return success;
}

bool ce_vim_verb_yank(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                      const CeConfigOptions_t* config_options){
     CeVimYank_t* yank = vim->yanks + ce_vim_yank_register_index(action->verb.integer);
     if(yank->text) free(yank->text);
     int64_t yank_len = ce_buffer_range_len(view->buffer, motion_range.start, motion_range.end);
     yank->text = ce_buffer_dupe_string(view->buffer, motion_range.start, yank_len, action->yank_line);
     yank->line = action->yank_line;
     vim->mode = CE_VIM_MODE_NORMAL;
     return true;
}

static bool paste_text(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                       const CeConfigOptions_t* config_options, bool after){
     CeVimYank_t* yank = vim->yanks + ce_vim_yank_register_index(action->verb.integer);
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
     change.remove_line_if_empty = yank->line;
     change.string = strdup(yank->text);
     change.location = insertion_point;
     change.cursor_before = view->cursor;
     change.cursor_after = cursor_end;
     ce_buffer_change(view->buffer, &change);

     view->cursor = cursor_end;
     vim->mode = CE_VIM_MODE_NORMAL;

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
     // insert newline on next line
     char* insert_string = strdup("\n");
     motion_range.start.x = 0;
     if(!ce_buffer_insert_string(view->buffer, insert_string, motion_range.start)) return false;

     // commit the change
     CeBufferChange_t change = {};
     change.chain = action->chain_undo;
     change.insertion = true;
     change.remove_line_if_empty = true;
     change.string = insert_string;
     change.location = motion_range.start;
     change.cursor_before = view->cursor;
     change.cursor_after = view->cursor;
     ce_buffer_change(view->buffer, &change);

     // TODO: compress what we can with open_below
     // calc indentation
     CePoint_t indentation_point = {0, motion_range.start.y};
     CePoint_t cursor_end = {0, indentation_point.y};
     int64_t indentation = ce_vim_get_indentation(view->buffer, indentation_point, config_options->tab_width);
     if(indentation > 0){
          cursor_end.x = indentation;

          // build indentation string
          insert_string = malloc(indentation + 1);
          memset(insert_string, ' ', indentation);
          insert_string[indentation] = 0;

          // insert indentation
          if(!ce_buffer_insert_string(view->buffer, insert_string, indentation_point)) return false;

          // commit the change
          change.chain = true;
          change.insertion = true;
          change.remove_line_if_empty = false;
          change.string = insert_string;
          change.location = indentation_point;
          change.cursor_before = view->cursor;
          change.cursor_after = cursor_end;
          ce_buffer_change(view->buffer, &change);
     }

     view->cursor = cursor_end;
     vim->chain_undo = true;
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_open_below(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                            const CeConfigOptions_t* config_options){
     // insert newline at the end of the current line
     char* insert_string = strdup("\n");
     motion_range.start.x = ce_utf8_strlen(view->buffer->lines[motion_range.start.y]);
     if(!ce_buffer_insert_string(view->buffer, insert_string, motion_range.start)) return false;

     // commit the change
     CeBufferChange_t change = {};
     change.chain = false;
     change.insertion = true;
     change.remove_line_if_empty = false;
     change.string = insert_string;
     change.location = motion_range.start;
     change.cursor_before = view->cursor;
     change.cursor_after = view->cursor;
     ce_buffer_change(view->buffer, &change);

     // calc indentation
     CePoint_t indentation_point = {0, motion_range.start.y + 1};
     int64_t indentation = ce_vim_get_indentation(view->buffer, indentation_point, config_options->tab_width);
     CePoint_t cursor_end = {0, indentation_point.y};
     if(indentation > 0){
          cursor_end.x = indentation;

          // build indentation string
          insert_string = malloc(indentation + 1);
          memset(insert_string, ' ', indentation);
          insert_string[indentation] = 0;

          // insert indentation
          if(!ce_buffer_insert_string(view->buffer, insert_string, indentation_point)) return false;

          // commit the change
          change.chain = true;
          change.insertion = true;
          change.remove_line_if_empty = false;
          change.string = insert_string;
          change.location = indentation_point;
          change.cursor_before = view->cursor;
          change.cursor_after = cursor_end;
          ce_buffer_change(view->buffer, &change);
     }

     view->cursor = cursor_end;
     vim->chain_undo = true;
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

bool ce_vim_verb_append(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                        const CeConfigOptions_t* config_options){
     view->cursor.x++;
     if(ce_utf8_strlen(view->buffer->lines[view->cursor.y]) == 0) view->cursor.x = 0;
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_append_at_end_of_line(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                                       const CeConfigOptions_t* config_options){
     view->cursor.x = ce_utf8_strlen(view->buffer->lines[view->cursor.y]);
     insert_mode(vim);
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

bool ce_vim_verb_z_command(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                           const CeConfigOptions_t* config_options){
     switch(action->verb.integer){
     default:
          break;
     case 'z':
     {
          int64_t view_height = view->rect.bottom - view->rect.top;
          ce_view_scroll_to(view, (CePoint_t){0, view->cursor.y - (view_height / 2)});
     } break;
     case 'b':
     {
          ce_view_scroll_to(view, (CePoint_t){0, view->cursor.y - view->rect.bottom + config_options->vertical_scroll_off});
     } break;
     case 't':
     {
          ce_view_scroll_to(view, (CePoint_t){0, view->cursor.y - config_options->vertical_scroll_off});
     } break;
     }

     return false;
}

bool ce_vim_verb_g_command(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                           const CeConfigOptions_t* config_options){
     switch(action->verb.integer){
     default:
          break;
     case 'g':
          motion_range.end = (CePoint_t){0, 0};
          return ce_vim_verb_motion(vim, action, motion_range, view, config_options);
     }

     return false;
}

bool ce_vim_verb_indent(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                        const CeConfigOptions_t* config_options){
     ce_vim_motion_range_sort(&motion_range);
     for(int64_t i = motion_range.start.y; i <= motion_range.end.y; i++){
          // calc indentation
          CePoint_t indentation_point = {0, i};

          // build indentation string
          char* insert_string = malloc(config_options->tab_width + 1);
          memset(insert_string, ' ', config_options->tab_width);
          insert_string[config_options->tab_width] = 0;

          // insert indentation
          if(!ce_buffer_insert_string(view->buffer, insert_string, indentation_point)) return false;

          // commit the change
          CeBufferChange_t change = {};
          change.chain = true;
          change.insertion = true;
          change.remove_line_if_empty = false;
          change.string = insert_string;
          change.location = indentation_point;
          change.cursor_before = view->cursor;
          change.cursor_after = view->cursor;
          ce_buffer_change(view->buffer, &change);
     }

     return true;
}

bool ce_vim_verb_unindent(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                          const CeConfigOptions_t* config_options){
     bool chain = false;
     ce_vim_motion_range_sort(&motion_range);
     for(int64_t i = motion_range.start.y; i <= motion_range.end.y; i++){
          // calc indentation
          CePoint_t indentation_point = {0, i};

          // build indentation string
          char* remove_string = malloc(config_options->tab_width + 1);
          memset(remove_string, ' ', config_options->tab_width);
          remove_string[config_options->tab_width] = 0;

          if(strncmp(view->buffer->lines[i], remove_string, config_options->tab_width) == 0){
               // insert indentation
               if(!ce_buffer_remove_string(view->buffer, indentation_point, config_options->tab_width, false)) return false;

               // commit the change
               CeBufferChange_t change = {};
               change.chain = chain;
               change.insertion = false;
               change.remove_line_if_empty = false;
               change.string = remove_string;
               change.location = indentation_point;
               change.cursor_before = view->cursor;
               change.cursor_after = view->cursor;
               ce_buffer_change(view->buffer, &change);
               chain = true;
          }else{
               free(remove_string);
          }
     }

     return true;
}

bool ce_vim_verb_join(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view,
                      const CeConfigOptions_t* config_options){
     if(view->cursor.y < 0) return false;
     if(view->cursor.y >= (view->buffer->line_count - 1)) return false;

     CeBufferChange_t change = {};

     // delete up to the soft beginning of the line
     CePoint_t beginning_of_next_line = {0, view->cursor.y + 1};
     int64_t whitespace_len = ce_vim_soft_begin_line(view->buffer, beginning_of_next_line.y);
     if(whitespace_len > 0){
          char* whitespace_str = ce_buffer_dupe_string(view->buffer, beginning_of_next_line, whitespace_len, false);
          if(!ce_buffer_remove_string(view->buffer, beginning_of_next_line, whitespace_len, false)) return false;

          change.chain = false;
          change.insertion = false;
          change.remove_line_if_empty = false;
          change.string = whitespace_str;
          change.location = beginning_of_next_line;
          change.cursor_before = view->cursor;
          change.cursor_after = view->cursor;
          ce_buffer_change(view->buffer, &change);
     }

     CePoint_t point = {ce_utf8_strlen(view->buffer->lines[view->cursor.y]), view->cursor.y};
     ce_vim_join_next_line(view->buffer, view->cursor.y, view->cursor, true);

     if(whitespace_len > 0){
          char* space_str = strdup(" ");
          if(!ce_buffer_insert_string(view->buffer, space_str, point)) return false;
          CePoint_t end_cursor = ce_buffer_advance_point(view->buffer, point, 1);

          change.chain = true;
          change.insertion = true;
          change.remove_line_if_empty = false;
          change.string = space_str;
          change.location = point;
          change.cursor_before = view->cursor;
          change.cursor_after = end_cursor;
          ce_buffer_change(view->buffer, &change);

          view->cursor = end_cursor;
     }else{
          view->cursor = point;
     }

     return true;
}
