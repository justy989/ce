#include "ce_vim.h"
#include "ce_app.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>
#include <assert.h>

static bool string_is_whitespace(const char* string){
     if(*string == 0) return false;

     bool all_whitespace = true;
     while(*string){
          if(!isspace(*string)){
               all_whitespace = false;
               break;
          }
          string++;
     }

     return all_whitespace;
}

void ce_vim_add_key_bind(CeVimKeyBind_t* key_binds, int64_t* key_bind_count, CeRune_t key, CeVimParseFunc_t* function){
     assert(*key_bind_count < CE_VIM_MAX_KEY_BINDS);
     key_binds[*key_bind_count].key = key;
     key_binds[*key_bind_count].function = function;
     (*key_bind_count)++;
}

static void insert_mode(CeVim_t* vim){
     vim->mode = CE_VIM_MODE_INSERT;
     if(!vim->verb_last_action) ce_rune_node_free(&vim->insert_rune_head);
}

bool vim_mode_is_visual(CeVimMode_t vim_mode){
     return vim_mode == CE_VIM_MODE_VISUAL || vim_mode == CE_VIM_MODE_VISUAL_LINE || vim_mode == CE_VIM_MODE_VISUAL_BLOCK;
}

bool ce_vim_init(CeVim_t* vim){
     vim->chain_undo = false;

     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'w', &ce_vim_parse_motion_little_word);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'W', &ce_vim_parse_motion_big_word);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'e', &ce_vim_parse_motion_end_little_word);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'E', &ce_vim_parse_motion_end_big_word);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'b', &ce_vim_parse_motion_begin_little_word);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'B', &ce_vim_parse_motion_begin_big_word);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'h', &ce_vim_parse_motion_left);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'l', &ce_vim_parse_motion_right);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'k', &ce_vim_parse_motion_up);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'j', &ce_vim_parse_motion_down);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, KEY_LEFT, &ce_vim_parse_motion_left);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, KEY_RIGHT, &ce_vim_parse_motion_right);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, KEY_UP, &ce_vim_parse_motion_up);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, KEY_DOWN, &ce_vim_parse_motion_down);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '^', &ce_vim_parse_motion_soft_begin_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '0', &ce_vim_parse_motion_hard_begin_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, KEY_HOME, &ce_vim_parse_motion_hard_begin_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '$', &ce_vim_parse_motion_end_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'f', &ce_vim_parse_motion_find_forward);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'F', &ce_vim_parse_motion_find_backward);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 't', &ce_vim_parse_motion_until_forward);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'T', &ce_vim_parse_motion_until_backward);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, ';', &ce_vim_parse_motion_next_find_char);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, ',', &ce_vim_parse_motion_prev_find_char);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'i', &ce_vim_parse_motion_inside);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'a', &ce_vim_parse_motion_around);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'G', &ce_vim_parse_motion_end_of_file);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'n', &ce_vim_parse_motion_search_next);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'N', &ce_vim_parse_motion_search_prev);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '%', &ce_vim_parse_motion_match_pair);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '*', &ce_vim_parse_motion_search_word_forward);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '#', &ce_vim_parse_motion_search_word_backward);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'H', &ce_vim_parse_motion_top_of_view);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'M', &ce_vim_parse_motion_middle_of_view);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'L', &ce_vim_parse_motion_bottom_of_view);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '}', &ce_vim_parse_motion_next_blank_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '{', &ce_vim_parse_motion_previous_blank_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, ']', &ce_vim_parse_motion_next_zero_indentation_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '[', &ce_vim_parse_motion_previous_zero_indentation_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, ce_ctrl_key('b'), &ce_vim_parse_motion_page_up);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, ce_ctrl_key('f'), &ce_vim_parse_motion_page_down);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, KEY_PPAGE, &ce_vim_parse_motion_page_up);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, KEY_NPAGE, &ce_vim_parse_motion_page_down);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, ce_ctrl_key('u'), &ce_vim_parse_motion_half_page_up);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, ce_ctrl_key('d'), &ce_vim_parse_motion_half_page_down);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '`', &ce_vim_parse_motion_mark);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '\'', &ce_vim_parse_motion_mark_soft_begin_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'i', &ce_vim_parse_verb_insert_mode);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'R', &ce_vim_parse_verb_replace_mode);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'v', &ce_vim_parse_verb_visual_mode);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'V', &ce_vim_parse_verb_visual_line_mode);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 27, &ce_vim_parse_verb_normal_mode);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'a', &ce_vim_parse_verb_append);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'A', &ce_vim_parse_verb_append_at_end_of_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'I', &ce_vim_parse_verb_insert_at_soft_begin_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'd', &ce_vim_parse_verb_delete);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'D', &ce_vim_parse_verb_delete_to_end_of_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'c', &ce_vim_parse_verb_change);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'C', &ce_vim_parse_verb_change_to_end_of_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'r', &ce_vim_parse_verb_set_character);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'x', &ce_vim_parse_verb_delete_character);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 's', &ce_vim_parse_verb_substitute_character);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'S', &ce_vim_parse_verb_substitute_soft_begin_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'y', &ce_vim_parse_verb_yank);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'Y', &ce_vim_parse_verb_yank_line);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '"', &ce_vim_parse_select_yank_register);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'P', &ce_vim_parse_verb_paste_before);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'p', &ce_vim_parse_verb_paste_after);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'u', &ce_vim_parse_verb_undo);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, KEY_REDO, &ce_vim_parse_verb_redo);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'O', &ce_vim_parse_verb_open_above);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'o', &ce_vim_parse_verb_open_below);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '.', &ce_vim_parse_verb_last_action);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'z', &ce_vim_parse_verb_z_command);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'g', &ce_vim_parse_verb_g_command);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '>', &ce_vim_parse_verb_indent);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '<', &ce_vim_parse_verb_unindent);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'J', &ce_vim_parse_verb_join);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, '~', &ce_vim_parse_verb_flip_case);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, 'm', &ce_vim_parse_verb_set_mark);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, ce_ctrl_key('a'), &ce_vim_parse_verb_increment_number);
     ce_vim_add_key_bind(vim->key_binds, &vim->key_bind_count, ce_ctrl_key('x'), &ce_vim_parse_verb_decrement_number);

     return true;
}

bool ce_vim_free(CeVim_t* vim){
     for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
          ce_vim_yank_free(vim->yanks + i);
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

CeVimParseResult_t insert_mode_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, const CeConfigOptions_t* config_options){
     switch(key){
     default:
          if(isprint(key) || key == CE_NEWLINE){
               if(ce_buffer_insert_rune(view->buffer, key, view->cursor)){
                    const char str[2] = {key, 0};
                    CePoint_t save_cursor = view->cursor;
                    CePoint_t new_cursor = ce_buffer_advance_point(view->buffer, view->cursor, 1);

                    // TODO: convenience function
                    CeBufferChange_t change = {};
                    change.chain = vim->chain_undo;
                    change.insertion = true;
                    change.string = strdup(str);
                    change.location = view->cursor;
                    change.cursor_before = view->cursor;
                    change.cursor_after = new_cursor;
                    ce_buffer_change(view->buffer, &change);

                    view->cursor = new_cursor;
                    vim->chain_undo = true;

                    // indent after newline
                    if(key == CE_NEWLINE && !vim->pasting){
                         // calc indentation
                         CePoint_t indentation_point = {0, view->cursor.y};
                         int64_t indentation = ce_vim_get_indentation(view->buffer, save_cursor, config_options->tab_width);
                         CePoint_t cursor_end = {indentation, indentation_point.y};

                         if(indentation > 0){
                              // build indentation string
                              char* insert_string = malloc(indentation + 1);
                              memset(insert_string, ' ', indentation);
                              insert_string[indentation] = 0;

                              if(!ce_buffer_insert_string_change(view->buffer, insert_string, indentation_point,
                                                                 &view->cursor, cursor_end, true)){
                                   return false;
                              }
                         }

                         view->cursor = cursor_end;

                         // check if previous line was all whitespace, if so, remove it
                         CePoint_t remove_loc = {0, save_cursor.y};
                         if(string_is_whitespace(view->buffer->lines[save_cursor.y])){
                              int64_t remove_len = strlen(view->buffer->lines[save_cursor.y]);
                              ce_buffer_remove_string_change(view->buffer, remove_loc, remove_len, &view->cursor,
                                                             view->cursor, true);
                         }
                    }
               }
          }else if(key == CE_TAB){
               // build a string to insert
               int64_t insert_len = 1;
               if(config_options->insert_spaces_on_tab){
                    insert_len = config_options->tab_width;
               }
               char* insert_string = malloc(insert_len + 1);
               if(config_options->insert_spaces_on_tab){
                    memset(insert_string, ' ', insert_len);
               }else{
                    *insert_string = key;
               }
               insert_string[insert_len] = 0;

               if(!ce_buffer_insert_string_change_at_cursor(view->buffer, insert_string, &view->cursor, vim->chain_undo)){
                    break;
               }

               vim->chain_undo = true;
          }else{
               return CE_VIM_PARSE_KEY_NOT_HANDLED;
          }
          break;
     case '}':
     {
          if(!vim->pasting && string_is_whitespace(view->buffer->lines[view->cursor.y])){
               int64_t remove_len = strlen(view->buffer->lines[view->cursor.y]);
               CePoint_t remove_loc = {0, view->cursor.y};
               ce_buffer_remove_string_change(view->buffer, remove_loc, remove_len, &view->cursor,
                                              remove_loc, true);

               // calc indentation
               CePoint_t indentation_point = {0, view->cursor.y};
               int64_t indentation = ce_vim_get_indentation(view->buffer, view->cursor, config_options->tab_width);
               if(indentation > 0) indentation -= config_options->tab_width;
               if(indentation < 0) indentation = 0;
               CePoint_t cursor_end = {indentation, indentation_point.y};

               if(indentation > 0){
                   // build indentation string
                    char* insert_string = malloc(indentation + 1);
                    memset(insert_string, ' ', indentation);
                    insert_string[indentation] = 0;

                    if(!ce_buffer_insert_string_change(view->buffer, insert_string, indentation_point,
                                                       &view->cursor, cursor_end, true)){
                         break;
                    }
               }
          }

          if(!ce_buffer_insert_rune(view->buffer, key, view->cursor)) break;

          const char str[2] = {key, 0};
          CePoint_t new_cursor = ce_buffer_advance_point(view->buffer, view->cursor, 1);

          // TODO: convenience function
          CeBufferChange_t change = {};
          change.chain = vim->chain_undo;
          change.insertion = true;
          change.string = strdup(str);
          change.location = view->cursor;
          change.cursor_before = view->cursor;
          change.cursor_after = new_cursor;
          ce_buffer_change(view->buffer, &change);

          view->cursor = new_cursor;
          vim->chain_undo = true;

     } break;
     case KEY_BACKSPACE:
          if(!ce_points_equal(view->cursor, (CePoint_t){0, 0})){
               CePoint_t remove_point;
               CePoint_t end_cursor;
               int64_t remove_len = 1;

               if(view->cursor.x == 0){
                    int64_t line = view->cursor.y - 1;
                    end_cursor = (CePoint_t){ce_utf8_strlen(view->buffer->lines[line]), line};
                    ce_vim_join_next_line(view->buffer, view->cursor.y - 1, view->cursor, vim->chain_undo);
               }else{
                    remove_point = ce_buffer_advance_point(view->buffer, view->cursor, -1);
                    end_cursor = remove_point;
                    ce_buffer_remove_string_change(view->buffer, remove_point, remove_len, &view->cursor,
                                                   end_cursor, vim->chain_undo);
               }

               view->cursor = end_cursor;
               vim->chain_undo = true;
          }
          break;
     case KEY_DC:
          ce_buffer_remove_string_change(view->buffer, view->cursor, 1, &view->cursor,
                                         view->cursor, vim->chain_undo);
          vim->chain_undo = true;
          break;
     case KEY_LEFT:
          view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){-1, 0},
                                              config_options->tab_width, CE_CLAMP_X_ON);
          vim->chain_undo = false;
          break;
     case KEY_DOWN:
          view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){0, 1},
                                              config_options->tab_width, CE_CLAMP_X_ON);
          vim->chain_undo = false;
          break;
     case KEY_UP:
          view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){0, -1},
                                              config_options->tab_width, CE_CLAMP_X_ON);
          vim->chain_undo = false;
          break;
     case KEY_RIGHT:
          view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){1, 0},
                                              config_options->tab_width, CE_CLAMP_X_ON);
          vim->chain_undo = false;
          break;
     case 27: // escape
     {
          // check if previous line was all whitespace, if so, remove it
          CePoint_t remove_loc = {0, view->cursor.y};
          if(string_is_whitespace(view->buffer->lines[view->cursor.y])){
               int64_t remove_len = strlen(view->buffer->lines[view->cursor.y]);
               ce_buffer_remove_string_change(view->buffer, remove_loc, remove_len, &view->cursor,
                                              remove_loc, true);
          }

          if(!ce_points_equal(vim->visual_block_top_left, vim->visual_block_bottom_right)){
               CePoint_t visual_top_left = vim->visual_block_top_left;
               CePoint_t visual_bottom_right = vim->visual_block_bottom_right;

               // adjust range because we've already inserted text the line the cursor is on
               CePoint_t cursor_end = view->cursor;
               if(visual_top_left.y == view->cursor.y){
                    visual_top_left.y++;
               }else{
                    visual_bottom_right.y--;
               }

               vim->visual_block_top_left = (CePoint_t){0, 0};
               vim->visual_block_bottom_right = (CePoint_t){0, 0};

               CeRune_t* rune_string = ce_rune_node_string(vim->insert_rune_head);

               if(view->buffer->change_node) view->buffer->change_node->change.chain = true;
               vim->chain_undo = true;

               for(int64_t i = visual_top_left.y; i <= visual_bottom_right.y; i++){
                    view->cursor = (CePoint_t){visual_top_left.x, i};
                    CeRune_t* itr = rune_string;
                    while(*itr){
                         insert_mode_handle_key(vim, view, *itr, config_options);
                         itr++;
                    }
               }

               free(rune_string);
               view->cursor = cursor_end;
               if(view->buffer->change_node) view->buffer->change_node->change.cursor_after = view->cursor;
          }

          vim->mode = CE_VIM_MODE_NORMAL;
          vim->chain_undo = false;
          if(view->cursor.x > 0){
               view->cursor = ce_buffer_advance_point(view->buffer, view->cursor, -1);
          }
     } break;
     case KEY_REDO:
     {
          CeVimYank_t* yank = vim->yanks + ce_vim_register_index('"');
          if(!yank) break;
          if(yank->type == CE_VIM_YANK_TYPE_BLOCK) break;

          if(!ce_buffer_insert_string_change_at_cursor(view->buffer, strdup(yank->text), &view->cursor, vim->chain_undo)){
               break;
          }

          vim->chain_undo = true;
     } break;
     }

     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, CeVimBufferData_t* buffer_data,
                                     const CeConfigOptions_t* config_options){
     switch(vim->mode){
     default:
          return CE_VIM_PARSE_INVALID;
     case CE_VIM_MODE_INSERT:
          if(!vim->verb_last_action && key != 27) ce_rune_node_insert(&vim->insert_rune_head, key);
          memset(&vim->current_action, 0, sizeof(vim->current_action));
          return insert_mode_handle_key(vim, view, key, config_options);
     case CE_VIM_MODE_REPLACE:
          if(key != CE_NEWLINE && key != 27){ // escape
               int64_t last_index = ce_utf8_last_index(view->buffer->lines[view->cursor.y]);
               if(view->cursor.x < last_index){
                    ce_buffer_remove_string_change(view->buffer, view->cursor, 1, &view->cursor,
                                                   view->cursor, vim->chain_undo);
                    vim->chain_undo = true;
              }
          }

          memset(&vim->current_action, 0, sizeof(vim->current_action));
          return insert_mode_handle_key(vim, view, key, config_options);
     case CE_VIM_MODE_NORMAL:
     case CE_VIM_MODE_VISUAL:
     case CE_VIM_MODE_VISUAL_LINE:
     case CE_VIM_MODE_VISUAL_BLOCK:
     {
          CeVimAction_t action = {};

          if(!ce_vim_append_key(vim, key)){
               vim->current_command[0] = 0;
               break;
          }

          CeVimParseResult_t result = ce_vim_parse_action(&action, vim->current_command, vim->key_binds,
                                                          vim->key_bind_count, vim->mode);

          if(result == CE_VIM_PARSE_COMPLETE){
               ce_vim_apply_action(vim, &action, view, buffer_data, config_options);
               vim->current_command[0] = 0;

               if(!vim->verb_last_action && action.repeatable){
                    vim->last_action = action;
                    ce_rune_node_free(&vim->insert_rune_head);
               }
          }else if(result == CE_VIM_PARSE_INVALID ||
                   result == CE_VIM_PARSE_KEY_NOT_HANDLED){
               vim->current_command[0] = 0;
          }

          vim->current_action = action;
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

                    int64_t loops = 0;
                    while(result == CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY){
                         if(*keys == 0) return result;
                         result = key_bind->function(&build_action, *keys);
                         keys++;
                         if(result == CE_VIM_PARSE_CONTINUE) goto VIM_PARSE_CONTINUE;
                         loops++;
                         if(loops >= 10) return CE_VIM_PARSE_INVALID;
                    }

                    break;
               }
          }
     }

     // parse multiplier
     if(result != CE_VIM_PARSE_COMPLETE){
          if(vim_mode_is_visual(vim_mode) && multiplier == 0){
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

                              int64_t loops = 0;
                              while(result == CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY){
                                   if(*keys == 0) return result;
                                   result = key_bind->function(&build_action, *keys);
                                   keys++;
                                   if(result == CE_VIM_PARSE_CONTINUE) goto VIM_PARSE_CONTINUE;
                                   loops++;
                                   if(loops >= 10) return CE_VIM_PARSE_INVALID;
                              }

                              break;
                         }
                    }
               }
          }
     }else if(vim_mode_is_visual(vim_mode) && !build_action.motion.function){
          build_action.motion.function = &ce_vim_motion_visual;
     }

     if(result == CE_VIM_PARSE_COMPLETE) *action = build_action;
     return result;
}

bool ce_vim_apply_action(CeVim_t* vim, CeVimAction_t* action, CeView_t* view, CeVimBufferData_t* buffer_data,
                         const CeConfigOptions_t* config_options){
     if(vim->mode == CE_VIM_MODE_VISUAL_BLOCK && action->verb.function != ce_vim_verb_motion){
          // sort y
          if(vim->visual.y < view->cursor.y){
               vim->visual_block_top_left.y = vim->visual.y;
               vim->visual_block_bottom_right.y = view->cursor.y;
          }else{
               vim->visual_block_top_left.y = view->cursor.y;
               vim->visual_block_bottom_right.y = vim->visual.y;
          }

          // sort x
          if(vim->visual.x < view->cursor.x){
               vim->visual_block_top_left.x = vim->visual.x;
               vim->visual_block_bottom_right.x = view->cursor.x;
          }else{
               vim->visual_block_top_left.x = view->cursor.x;
               vim->visual_block_bottom_right.x = vim->visual.x;
          }

          bool success = true;
          if(action->verb.function == ce_vim_verb_yank ||
             action->verb.function == ce_vim_verb_delete ||
             action->verb.function == ce_vim_verb_change){
               CeVimYank_t* yank = vim->yanks + ce_vim_register_index(action->verb.integer);
               if(action->verb.function == ce_vim_verb_delete || action->verb.function == ce_vim_verb_change){
                    yank = vim->yanks + ce_vim_register_index('"');
               }
               ce_vim_yank_free(yank);
               yank->type = CE_VIM_YANK_TYPE_BLOCK;
               yank->block_line_count = (vim->visual_block_bottom_right.y - vim->visual_block_top_left.y) + 1;
               yank->block = malloc(yank->block_line_count * sizeof(*yank->block));

               // copy each line into yank
               for(int64_t i = vim->visual_block_top_left.y; i <= vim->visual_block_bottom_right.y; i++){
                    CeRange_t motion_range = {(CePoint_t){vim->visual_block_top_left.x, i},
                                              (CePoint_t){vim->visual_block_bottom_right.x, i}};
                    int64_t yank_string_index = i - vim->visual_block_top_left.y;
                    int64_t line_last_index = ce_utf8_last_index(view->buffer->lines[i]);

                    // clamp the range to the line length
                    if(motion_range.start.x > line_last_index) motion_range.start.x = line_last_index;
                    if(motion_range.end.x > line_last_index) motion_range.end.x = line_last_index;
                    if(motion_range.end.x == 0 && line_last_index == 0){
                         yank->block[yank_string_index] = NULL;
                         continue;
                    }

                    int64_t motion_range_len = (motion_range.end.x - motion_range.start.x) + 1;
                    yank->block[yank_string_index] = ce_buffer_dupe_string(view->buffer, motion_range.start, motion_range_len);
               }

               action->do_not_yank = true;

               if(action->verb.function == ce_vim_verb_yank){
                    vim->visual_block_top_left = (CePoint_t){0, 0};
                    vim->visual_block_bottom_right = (CePoint_t){0, 0};
                    vim->mode = CE_VIM_MODE_NORMAL;
                    return success;
               }
          }

          if(action->verb.function == ce_vim_verb_insert_mode ||
             action->verb.function == ce_vim_verb_delete ||
             action->verb.function == ce_vim_verb_change){
               // run verb for each line in range
               for(int64_t i = vim->visual_block_top_left.y; i <= vim->visual_block_bottom_right.y; i++){
                    CeRange_t motion_range = {(CePoint_t){vim->visual_block_top_left.x, i},
                                              (CePoint_t){vim->visual_block_bottom_right.x, i}};
                    int64_t line_last_index = ce_utf8_last_index(view->buffer->lines[i]);

                    // clamp the range to the line length
                    if(motion_range.start.x > line_last_index) motion_range.start.x = line_last_index;
                    if(motion_range.end.x > line_last_index) motion_range.end.x = line_last_index;
                    if(motion_range.end.x == 0 && line_last_index == 0) continue;

                    if(!action->verb.function(vim, action, motion_range, view, buffer_data, config_options)){
                         success = false;
                    }else if(i != vim->visual_block_top_left.y){
                         if(view->buffer->change_node) view->buffer->change_node->change.chain = true;
                    }
               }

               if(vim->mode != CE_VIM_MODE_INSERT){
                    vim->visual_block_top_left = (CePoint_t){0, 0};
                    vim->visual_block_bottom_right = (CePoint_t){0, 0};
               }
               return success;
          }
     }

     CeRange_t motion_range = {view->cursor, view->cursor};
     if(action->motion.function){
          int64_t total_multiplier = action->multiplier * action->motion.multiplier;
          for(int64_t i = 0; i < total_multiplier; ++i){
               if(!action->motion.function(vim, action, view, config_options, buffer_data, &motion_range)){
                    return false;
               }
          }
     }
     if(action->verb.function && motion_range.start.x >= 0){
          if(!action->verb.function(vim, action, motion_range, view, buffer_data, config_options)){
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

     char* itr = ce_utf8_iterate_to(buffer->lines[start.y], start.x);

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

          if(*itr == 0){
               if(start.y >= buffer->line_count - 1) break;
               start.x = 0;
               start.y++;
               itr = buffer->lines[start.y];
               state = WORD_NEW_LINE;
          }else{
               itr += rune_len;
               start.x++;
          }
     }

END_LOOP:

     return start;
}

CePoint_t ce_vim_move_big_word(CeBuffer_t* buffer, CePoint_t start){
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};

     char* itr = ce_utf8_iterate_to(buffer->lines[start.y], start.x);

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

          if(*itr == 0){
               if(start.y >= buffer->line_count - 1) break;
               start.x = 0;
               start.y++;
               itr = buffer->lines[start.y];
               state = WORD_NEW_LINE;
          }else{
               itr += rune_len;
               start.x++;
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

     char* itr = ce_utf8_iterate_to(buffer->lines[start.y], start.x);
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

     char* itr = ce_utf8_iterate_to(buffer->lines[start.y], start.x);

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
     char* itr = ce_utf8_iterate_to(line_start, start.x); // start one character back

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
               else if(!isspace(rune)) state = WORD_INSIDE_OTHER;;
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

          if(itr < line_start){
               if(state == WORD_INSIDE_WORD || state == WORD_INSIDE_OTHER) break;

               start.y--;
               if(start.y < 0) return (CePoint_t){0, 0};
               start.x = ce_utf8_strlen(buffer->lines[start.y]);
               if(start.x > 0) start.x--;
               else break;
               line_start = buffer->lines[start.y];
               itr = ce_utf8_iterate_to(line_start, start.x); // start one character back
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
     char* itr = ce_utf8_iterate_to(line_start, start.x); // start one character back

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

          if(itr < line_start){
               if(state == WORD_INSIDE_WORD) break;

               start.y--;
               if(start.y < 0) return (CePoint_t){0, 0};
               start.x = ce_utf8_strlen(buffer->lines[start.y]);
               if(start.x == 0) break;
               line_start = buffer->lines[start.y];
               itr = ce_utf8_iterate_to(line_start, start.x);
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
     char* str = ce_utf8_iterate_to(buffer->lines[start.y], match_x);
     if(!str) return (CePoint_t){-1, -1};

     while(*str){
          int64_t rune_len = 0;
          CeRune_t rune = ce_utf8_decode(str, &rune_len);
          if(rune == match_rune) return (CePoint_t){until ? match_x - 1 : match_x, start.y};
          str += rune_len;
          match_x++;
     }

     return (CePoint_t){-1, -1};
}

CePoint_t ce_vim_move_find_rune_backward(CeBuffer_t* buffer, CePoint_t start, CeRune_t match_rune, bool until){
     if(!ce_buffer_point_is_valid(buffer, start)) return (CePoint_t){-1, -1};
     if(start.x == 0) return (CePoint_t){-1, -1};

     char* start_of_line = buffer->lines[start.y];
     char* str = ce_utf8_iterate_to(start_of_line, start.x);
     if(!str) return (CePoint_t){-1, -1};
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

     return (CePoint_t){-1, -1};
}

CeRange_t ce_vim_find_little_word_boundaries(CeBuffer_t* buffer, CePoint_t start){
     CeRange_t range = {(CePoint_t){-1, -1}, (CePoint_t){-1, -1}};
     char* line_start = buffer->lines[start.y];
     char* itr = ce_utf8_iterate_to(line_start, start.x);
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
     while(itr >= line_start){
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

CeRange_t ce_vim_find_big_word_boundaries(CeBuffer_t* buffer, CePoint_t start){
     CeRange_t range = {(CePoint_t){-1, -1}, (CePoint_t){-1, -1}};
     char* line_start = buffer->lines[start.y];
     char* itr = ce_utf8_iterate_to(line_start, start.x);
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
     while(itr >= line_start){
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

CeRange_t ce_vim_find_string_boundaries(CeBuffer_t* buffer, CePoint_t start, char string_char){
     CeRange_t range = {(CePoint_t){-1, -1}, (CePoint_t){-1, -1}};
     char* line_start = buffer->lines[start.y];
     char* itr = ce_utf8_iterate_to(line_start, start.x);
     char* save_start = itr;
     int64_t rune_len = 0;

     CeRune_t previous_rune = CE_UTF8_INVALID;
     int64_t end_x = start.x;
     while(*itr){
          CeRune_t rune = ce_utf8_decode(itr, &rune_len);
          if(rune == string_char && previous_rune != '\\') break;
          previous_rune = rune;
          end_x++;
          itr += rune_len;
     }

     int64_t start_x = start.x + 1;
     itr = save_start;
     previous_rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);
     while(itr > line_start){
          CeRune_t rune = previous_rune;
          previous_rune = ce_utf8_decode_reverse(itr, line_start, &rune_len);
          if(rune == string_char && previous_rune != '\\' && start_x < start.x) break;
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

// TODO: handle multiline comments
static bool point_in_string_or_comment(CeBuffer_t* buffer, CePoint_t point){
     if(!ce_buffer_contains_point(buffer, point)) return false;
     CeRune_t in_string = 0;
     CeRune_t in_comment = 0;
     CeRune_t prev_rune = 0;
     CeRune_t prev_prev_rune = 0;
     char* str = buffer->lines[point.y];
     int64_t rune_len;
     for(int64_t i = 0; i <= point.x; i++){
          CeRune_t rune = ce_utf8_decode(str, &rune_len);
          switch(rune){
          default:
               break;
          case '/':
               if(prev_rune == rune && !in_string){
                    in_comment = true;
               }else if(in_comment && prev_rune == '/'){
                    in_comment = false;
               }
               break;
          case '*':
               if(prev_rune == '/' && !in_string){
                    in_comment = true;
               }
               break;
          case '"':
          case '\'':
               if(in_string == rune){
                    if(prev_rune != '\\'){
                         in_string = 0;
                    // handle case where backslash getting backslashed
                    }else if(prev_prev_rune == '\\'){
                         in_string = 0;
                    }
               }else if(in_string == 0){
                    in_string = rune;
               }
               break;
          }
          prev_prev_rune = prev_rune;
          prev_rune = rune;
          str += rune_len;
     }

     return in_string || in_comment;
}

CeRange_t ce_vim_find_pair(CeBuffer_t* buffer, CePoint_t start, CeRune_t rune, bool inside){
     CeRange_t range = {(CePoint_t){-1, -1}, (CePoint_t){-1, -1}};
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
     case '"':
     case '\'':
     {
          CeRange_t string_range = ce_vim_find_string_boundaries(buffer, start, rune);
          if(inside && string_range.start.x >= 0){
               string_range.start = ce_buffer_advance_point(buffer, string_range.start, 1);
               string_range.end = ce_buffer_advance_point(buffer, string_range.end, -1);
               if(ce_point_after(string_range.start, string_range.end)) return range; // empty in between
          }
          return string_range;
     }
     case 'w':
          return ce_vim_find_little_word_boundaries(buffer, start);
     case 'W':
          return ce_vim_find_big_word_boundaries(buffer, start);
     }

     int64_t match_count = 0;

     // find left match
     CePoint_t itr = start;
     CeRune_t buffer_rune = ce_buffer_get_rune(buffer, itr);
     // if we start on a right match, back up the starting point
     if(buffer_rune == right_match){
          itr = ce_buffer_advance_point(buffer, start, -1);
     }else if(buffer_rune == left_match){
          itr = ce_buffer_advance_point(buffer, start, 1);
     }
     CePoint_t prev = itr;
     CePoint_t new_start = range.start;
     while(true){
          buffer_rune = ce_buffer_get_rune(buffer, itr); // NOTE: decoding would be faster
          if(buffer_rune == left_match && !point_in_string_or_comment(buffer, itr)){
               match_count--;
               if(match_count < 0){
                    new_start = inside ? prev : itr;
                    break;
               }
          }else if(buffer_rune == right_match && !point_in_string_or_comment(buffer, itr)){
               match_count++;
          }
          if(itr.x == 0 && itr.y == 0) return range;
          prev = itr;
          itr = ce_buffer_advance_point(buffer, itr, -1);
     }

     // find right match
     if(ce_points_equal(itr, start)){
          itr = ce_buffer_advance_point(buffer, start, 1);
     }else{
          itr = start;
     }

     prev = itr;
     match_count = 0;
     CePoint_t new_end = range.end;
     CePoint_t end_of_buffer = {ce_utf8_last_index(buffer->lines[buffer->line_count - 1]), buffer->line_count - 1};
     while(true){
          buffer_rune = ce_buffer_get_rune(buffer, itr);
          if(buffer_rune == right_match && !point_in_string_or_comment(buffer, itr)){
               match_count--;
               if(match_count < 0){
                    new_end = inside ? prev : itr;
                    break;
               }
          }else if(buffer_rune == left_match && !point_in_string_or_comment(buffer, itr)){
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
     CeAppBufferData_t* buffer_data = buffer->app_data;

     if(buffer_data->syntax_function == ce_syntax_highlight_c ||
        buffer_data->syntax_function == ce_syntax_highlight_cpp ||
        buffer_data->syntax_function == ce_syntax_highlight_java ||
        buffer_data->syntax_function == ce_syntax_highlight_config){
          CeRange_t brace_range = ce_vim_find_pair(buffer, point, '{', false);
          CeRange_t paren_range = ce_vim_find_pair(buffer, point, '(', false);
          if(brace_range.start.x < 0 && paren_range.start.x < 0) return 0;
          int64_t indent = 0;
          if(ce_point_after(paren_range.start, brace_range.start)){
               indent = paren_range.start.x + 1;
          }else{
               indent = ce_vim_soft_begin_line(buffer, brace_range.start.y);
               // if in our indent, we are inside parens, get the indentation of where the parens started
               paren_range = ce_vim_find_pair(buffer, (CePoint_t){indent, brace_range.start.y}, '(', false);
               if(paren_range.start.x >= 0){
                    indent = ce_vim_soft_begin_line(buffer, paren_range.start.y);
               }
               indent += tab_length;
          }
          return indent;
     }else if(buffer_data->syntax_function == ce_syntax_highlight_python){
          for(int64_t y = point.y; y >= 0; --y){
               const char* itr = buffer->lines[y];

               // find previous line that isn't blank
               bool blank = true;

               while(*itr){
                    if(!isblank(*itr)){
                         blank = false;
                         break;
                    }

                    itr++;
               }

               if(blank) continue;

               // use it as indentation unless it ends in a ':'
               int indentation = itr - buffer->lines[y];

               while(*itr) itr++;
               itr--;

               if(*itr == ':') indentation += 5;
               return indentation;
          }
     }

     return 0;
}

bool ce_vim_join_next_line(CeBuffer_t* buffer, int64_t line, CePoint_t cursor, bool chain_undo){
     CePoint_t point = {ce_utf8_strlen(buffer->lines[line]), line};
     CePoint_t after_point = {0, point.y + 1};
     ce_buffer_remove_string_change(buffer, point, 1, &cursor, after_point, chain_undo);
     return true;
}

CeVimParseResult_t ce_vim_parse_verb_insert_mode(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_insert_mode;
     action->repeatable = true;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_replace_mode(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_replace_mode;
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
     action->repeatable = true;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_append_at_end_of_line(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_append_at_end_of_line;
     action->repeatable = true;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_insert_at_soft_begin_line(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;

     action->verb.function = &ce_vim_verb_insert_at_soft_begin_line;
     action->repeatable = true;
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
          action->yank_type = CE_VIM_YANK_TYPE_LINE;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = ce_vim_verb_motion;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_motion_down(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_KEY_NOT_HANDLED;
     action->motion.function = ce_vim_motion_down;

     if(action->verb.function){
          action->yank_type = CE_VIM_YANK_TYPE_LINE;
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
     action->exclude_end = true;
     return parse_motion_direction(action, ce_vim_motion_soft_begin_line);
}

CeVimParseResult_t ce_vim_parse_motion_hard_begin_line(CeVimAction_t* action, CeRune_t key){
     action->exclude_end = true;
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
     action->exclude_end = true;
     return vim_parse_motion_find(action, key, ce_vim_motion_find_backward);
}

CeVimParseResult_t ce_vim_parse_motion_until_forward(CeVimAction_t* action, CeRune_t key){
     return vim_parse_motion_find(action, key, ce_vim_motion_until_forward);
}

CeVimParseResult_t ce_vim_parse_motion_until_backward(CeVimAction_t* action, CeRune_t key){
     action->exclude_end = true;
     return vim_parse_motion_find(action, key, ce_vim_motion_until_backward);
}

CeVimParseResult_t ce_vim_parse_motion_next_find_char(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_next_find_char);
}

CeVimParseResult_t ce_vim_parse_motion_prev_find_char(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_prev_find_char);
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

CeVimParseResult_t ce_vim_parse_motion_match_pair(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_match_pair);
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

CeVimParseResult_t ce_vim_parse_motion_mark(CeVimAction_t* action, CeRune_t key){
     if(!action->motion.function){
          action->motion.function = ce_vim_motion_mark;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->motion.function == ce_vim_motion_mark){
          action->motion.integer = key;
          if(!action->verb.function) action->verb.function = ce_vim_verb_motion;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_motion_mark_soft_begin_line(CeVimAction_t* action, CeRune_t key){
     if(!action->motion.function){
          action->motion.function = ce_vim_motion_mark_soft_begin_line;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->motion.function == ce_vim_motion_mark_soft_begin_line){
          action->motion.integer = key;
          if(!action->verb.function) action->verb.function = ce_vim_verb_motion;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_motion_top_of_view(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_top_of_view);
}

CeVimParseResult_t ce_vim_parse_motion_middle_of_view(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_middle_of_view);
}

CeVimParseResult_t ce_vim_parse_motion_bottom_of_view(CeVimAction_t* action, CeRune_t key){
     return parse_motion_direction(action, ce_vim_motion_bottom_of_view);
}

CeVimParseResult_t ce_vim_parse_motion_next_blank_line(CeVimAction_t* action, CeRune_t key){
     action->exclude_end = true;
     return parse_motion_direction(action, ce_vim_motion_next_blank_line);
}

CeVimParseResult_t ce_vim_parse_motion_previous_blank_line(CeVimAction_t* action, CeRune_t key){
     action->exclude_end = true;
     return parse_motion_direction(action, ce_vim_motion_previous_blank_line);
}

CeVimParseResult_t ce_vim_parse_motion_next_zero_indentation_line(CeVimAction_t* action, CeRune_t key){
     action->exclude_end = true;
     return parse_motion_direction(action, ce_vim_motion_next_zero_indentation_line);
}

CeVimParseResult_t ce_vim_parse_motion_previous_zero_indentation_line(CeVimAction_t* action, CeRune_t key){
     action->exclude_end = true;
     return parse_motion_direction(action, ce_vim_motion_previous_zero_indentation_line);
}

CeVimParseResult_t ce_vim_parse_verb_delete(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     if(action->verb.function == ce_vim_verb_delete){
          action->motion.function = &ce_vim_motion_entire_line;
          action->yank_type = CE_VIM_YANK_TYPE_LINE;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = &ce_vim_verb_delete;
     action->clamp_x = CE_CLAMP_X_INSIDE;
     return CE_VIM_PARSE_IN_PROGRESS;
}

CeVimParseResult_t ce_vim_parse_verb_delete_to_end_of_line(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     action->motion.function = &ce_vim_motion_end_line;
     action->verb.function = &ce_vim_verb_delete;
     action->clamp_x = CE_CLAMP_X_INSIDE;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_change(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     if(action->verb.function == ce_vim_verb_change){
          action->verb.function = &ce_vim_verb_substitute_soft_begin_line;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = &ce_vim_verb_change;
     action->clamp_x = CE_CLAMP_X_ON;
     return CE_VIM_PARSE_IN_PROGRESS;
}

CeVimParseResult_t ce_vim_parse_verb_change_to_end_of_line(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     action->motion.function = &ce_vim_motion_end_line;
     action->verb.function = &ce_vim_verb_change;
     action->clamp_x = CE_CLAMP_X_ON;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_set_character(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     if(action->verb.function == NULL){
          action->verb.function = &ce_vim_verb_set_character;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else{
          if(!isprint(key)) return CE_VIM_PARSE_INVALID;
          action->verb.integer = key;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_delete_character(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     action->verb.function = &ce_vim_verb_delete_character;
     action->clamp_x = CE_CLAMP_X_INSIDE;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_substitute_character(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     action->verb.function = &ce_vim_verb_substitute_character;
     action->clamp_x = CE_CLAMP_X_ON;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_substitute_soft_begin_line(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     action->verb.function = &ce_vim_verb_substitute_soft_begin_line;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_yank(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == &ce_vim_verb_yank){
          action->motion.function = &ce_vim_motion_entire_line;
          action->yank_type = CE_VIM_YANK_TYPE_LINE;
          return CE_VIM_PARSE_COMPLETE;
     }

     action->verb.function = &ce_vim_verb_yank;
     if(action->verb.integer == 0) action->verb.integer = '"';

     return CE_VIM_PARSE_IN_PROGRESS;
}

CeVimParseResult_t ce_vim_parse_verb_yank_line(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &ce_vim_verb_yank;
     action->motion.function = &ce_vim_motion_entire_line;
     if(action->verb.integer == 0) action->verb.integer = '"';
     action->yank_type = CE_VIM_YANK_TYPE_LINE;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_paste_before(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     action->verb.function = &ce_vim_verb_paste_before;
     if(action->verb.integer == 0) action->verb.integer = '"';
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_paste_after(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     action->verb.function = &ce_vim_verb_paste_after;
     if(action->verb.integer == 0) action->verb.integer = '"';
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_open_above(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     action->verb.function = &ce_vim_verb_open_above;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_open_below(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
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
     action->repeatable = true;
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
          if(action->multiplier > 1) return CE_VIM_PARSE_COMPLETE;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->verb.function == ce_vim_verb_g_command){
          action->verb.integer = key;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_indent(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     if(action->verb.function == NULL){
          action->verb.function = ce_vim_verb_indent;
          return CE_VIM_PARSE_IN_PROGRESS;
     }else if(action->verb.function == ce_vim_verb_indent){
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_unindent(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     if(action->verb.function == NULL){
          action->verb.function = ce_vim_verb_unindent;
          return CE_VIM_PARSE_IN_PROGRESS;
     }else if(action->verb.function == ce_vim_verb_unindent){
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_flip_case(CeVimAction_t* action, CeRune_t key){
     action->repeatable = true;
     action->motion.function = ce_vim_motion_right;
     action->verb.function = ce_vim_verb_flip_case;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_set_mark(CeVimAction_t* action, CeRune_t key){
     if(action->verb.function == NULL){
          action->verb.function = ce_vim_verb_set_mark;
          return CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY;
     }else if(action->verb.function == ce_vim_verb_set_mark){
          action->verb.integer = key;
          return CE_VIM_PARSE_COMPLETE;
     }

     return CE_VIM_PARSE_INVALID;
}

CeVimParseResult_t ce_vim_parse_verb_increment_number(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_INVALID;
     action->verb.function = ce_vim_verb_increment_number;
     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_verb_decrement_number(CeVimAction_t* action, CeRune_t key){
     if(action->motion.function) return CE_VIM_PARSE_INVALID;
     action->verb.function = ce_vim_verb_decrement_number;
     return CE_VIM_PARSE_COMPLETE;
}

static bool motion_direction(const CeView_t* view, CePoint_t delta, const CeConfigOptions_t* config_options,
                             CeRange_t* motion_range){
     CePoint_t destination = ce_buffer_move_point(view->buffer, motion_range->end, delta, config_options->tab_width, CE_CLAMP_X_INSIDE);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_left(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                        CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     return motion_direction(view, (CePoint_t){-1, 0}, config_options, motion_range);
}

bool ce_vim_motion_right(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                         CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     return motion_direction(view, (CePoint_t){1, 0}, config_options, motion_range);
}

bool ce_vim_motion_up(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                      CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     if(action->verb.function != ce_vim_verb_motion){
          if(motion_range->start.y > 0){
               // we use start instead of end so that we can sort them consistently through a motion multiplier
               motion_range->start.y--;
               motion_range->start.x = 0;
               motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
               ce_range_sort(motion_range);
          }

          return true;
     }

     return motion_direction(view, (CePoint_t){0, -1}, config_options, motion_range);
}

bool ce_vim_motion_down(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                        CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     if(action->verb.function != ce_vim_verb_motion){
          if(motion_range->end.y < view->buffer->line_count - 1){
               motion_range->end.y++;
               motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
               motion_range->start.x = 0;
               ce_range_sort(motion_range);
          }
          return true;
     }

     return motion_direction(view, (CePoint_t){0, 1}, config_options, motion_range);
}

bool ce_vim_motion_little_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                               CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t destination = ce_vim_move_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     action->exclude_end = true;
     return true;
}

bool ce_vim_motion_big_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                            CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t destination = ce_vim_move_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     action->exclude_end = true;
     return true;
}

bool ce_vim_motion_end_little_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                   CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t destination = ce_vim_move_end_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_end_big_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t destination = ce_vim_move_end_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     return true;
}

bool ce_vim_motion_begin_little_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                     CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t destination = ce_vim_move_begin_little_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     action->exclude_end = true;
     return true;
}

bool ce_vim_motion_begin_big_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                  CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t destination = ce_vim_move_begin_big_word(view->buffer, motion_range->end);
     if(destination.x < 0) return false;
     motion_range->end = destination;
     action->exclude_end = true;
     return true;
}

bool ce_vim_motion_soft_begin_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                   CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     int64_t result = ce_vim_soft_begin_line(view->buffer, motion_range->end.y);
     if(result < 0) return false;
     motion_range->end.x = result;
     return true;
}

bool ce_vim_motion_hard_begin_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                   CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     motion_range->end = (CePoint_t){0, motion_range->end.y};
     return true;
}

bool ce_vim_motion_end_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                            CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     motion_range->end.x = ce_utf8_last_index(view->buffer->lines[motion_range->end.y]);
     return true;
}

bool ce_vim_motion_entire_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                               CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     int64_t line_length = ce_utf8_strlen(view->buffer->lines[motion_range->end.y]);
     motion_range->start = (CePoint_t){0, motion_range->end.y};
     motion_range->end = (CePoint_t){line_length, motion_range->end.y};
     return true;
}

bool ce_vim_motion_page_up(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                           CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     motion_range->end.y -= (view->rect.bottom - view->rect.top);
     return true;
}

bool ce_vim_motion_page_down(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                             CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     motion_range->end.y += (view->rect.bottom - view->rect.top);
     return true;
}

bool ce_vim_motion_half_page_up(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     motion_range->end.y -= (view->rect.bottom - view->rect.top) / 2;
     return true;
}

bool ce_vim_motion_half_page_down(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                  CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     motion_range->end.y += (view->rect.bottom - view->rect.top) / 2;
     return true;
}

bool ce_vim_motion_visual(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                          CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     if(vim->mode == CE_VIM_MODE_VISUAL || vim->mode == CE_VIM_MODE_VISUAL_LINE){
          motion_range->start = view->cursor;
          motion_range->end = vim->visual;
          ce_range_sort(motion_range);
          action->motion.integer = ce_buffer_range_len(view->buffer, motion_range->start, motion_range->end) - 1;

          if(vim->mode == CE_VIM_MODE_VISUAL_LINE){
               motion_range->start.x = 0;
               motion_range->end.x = ce_utf8_strlen(view->buffer->lines[motion_range->end.y]);
               action->yank_type = CE_VIM_YANK_TYPE_LINE;
               action->motion.integer = motion_range->end.y - motion_range->start.y;
          }
     }else if(vim->verb_last_action){
          switch(action->yank_type){
          default:
               break;
          case CE_VIM_YANK_TYPE_LINE:
               motion_range->start = (CePoint_t){0, view->cursor.y};
               motion_range->end.y = view->cursor.y + action->motion.integer;
               motion_range->end.x = ce_utf8_strlen(view->buffer->lines[motion_range->end.y]);
               break;
          case CE_VIM_YANK_TYPE_STRING:
               motion_range->start = view->cursor;
               motion_range->end = ce_buffer_advance_point(view->buffer, view->cursor, action->motion.integer);
               break;
          }
     }

     return true;
}

bool ce_vim_motion_find_forward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, action->motion.integer, false);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     vim->find_char.rune = action->motion.integer;
     vim->find_char.state = CE_VIM_FIND_CHAR_STATE_FIND_FORWARD;
     return true;
}

bool ce_vim_motion_find_backward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                 CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, action->motion.integer, false);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     vim->find_char.rune = action->motion.integer;
     vim->find_char.state = CE_VIM_FIND_CHAR_STATE_FIND_BACKWARD;
     return true;
}

bool ce_vim_motion_until_forward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                 CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, action->motion.integer, true);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     vim->find_char.rune = action->motion.integer;
     vim->find_char.state = CE_VIM_FIND_CHAR_STATE_UNTIL_FORWARD;
     return true;
}

bool ce_vim_motion_until_backward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                  CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, action->motion.integer, true);
     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     vim->find_char.rune = action->motion.integer;
     vim->find_char.state = CE_VIM_FIND_CHAR_STATE_UNTIL_BACKWARD;
     return true;
}

bool ce_vim_motion_next_find_char(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                  CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t new_position = motion_range->end;

     switch(vim->find_char.state){
     default:
          break;
     case CE_VIM_FIND_CHAR_STATE_FIND_FORWARD:
          new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, vim->find_char.rune, false);
          break;
     case CE_VIM_FIND_CHAR_STATE_FIND_BACKWARD:
          new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, vim->find_char.rune, false);
          break;
     case CE_VIM_FIND_CHAR_STATE_UNTIL_FORWARD:
          new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, vim->find_char.rune, true);
          break;
     case CE_VIM_FIND_CHAR_STATE_UNTIL_BACKWARD:
          new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, vim->find_char.rune, true);
          break;
     }

     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

bool ce_vim_motion_prev_find_char(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                  CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t new_position = motion_range->end;

     switch(vim->find_char.state){
     default:
          break;
     case CE_VIM_FIND_CHAR_STATE_FIND_FORWARD:
          new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, vim->find_char.rune, false);
          break;
     case CE_VIM_FIND_CHAR_STATE_FIND_BACKWARD:
          new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, vim->find_char.rune, false);
          break;
     case CE_VIM_FIND_CHAR_STATE_UNTIL_FORWARD:
          new_position = ce_vim_move_find_rune_backward(view->buffer, motion_range->end, vim->find_char.rune, true);
          break;
     case CE_VIM_FIND_CHAR_STATE_UNTIL_BACKWARD:
          new_position = ce_vim_move_find_rune_forward(view->buffer, motion_range->end, vim->find_char.rune, true);
          break;
     }

     if(new_position.x < 0) return false;
     motion_range->end = new_position;
     return true;
}

static bool vim_motion_find(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                            CeRange_t* motion_range, bool inside){
     CePoint_t start = motion_range->start;
     CeRange_t new_range = ce_vim_find_pair(view->buffer, motion_range->end, action->motion.integer, inside);
     if(new_range.start.x < 0) return false;
     if(ce_points_equal(start, new_range.end)){
          action->exclude_end = true;
     }
     *motion_range = new_range;
     return true;
}

bool ce_vim_motion_inside(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                          CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     return vim_motion_find(vim, action, view, config_options, motion_range, true);
}

bool ce_vim_motion_around(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                          CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     return vim_motion_find(vim, action, view, config_options, motion_range, false);
}

bool ce_vim_motion_end_of_file(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                               CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     motion_range->end = ce_buffer_end_point(view->buffer);
     return true;
}

bool ce_vim_motion_search_next(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                               CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     const CeVimYank_t* yank = vim->yanks + ce_vim_register_index('/');
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

bool ce_vim_motion_search_prev(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                               CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     const CeVimYank_t* yank = vim->yanks + ce_vim_register_index('/');
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

static void search_word(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, CeRange_t* motion_range){
     CeVimYank_t* yank = vim->yanks + ce_vim_register_index('/');
     ce_vim_yank_free(yank);
     *motion_range = ce_vim_find_pair(view->buffer, view->cursor, 'w', true);
     int64_t word_len = ce_buffer_range_len(view->buffer, motion_range->start, motion_range->end);
     char* word = ce_buffer_dupe_string(view->buffer, motion_range->start, word_len);
     int64_t search_len = word_len + 4;
     yank->text = malloc(search_len + 1);
     snprintf(yank->text, search_len + 1, "\\b%s\\b", word); // TODO: this doesn't work on other platforms like macos
     yank->text[search_len] = 0;
     free(word);
     yank->type = CE_VIM_YANK_TYPE_STRING;
     vim->search_mode = CE_VIM_SEARCH_MODE_REGEX_FORWARD;
}

bool ce_vim_motion_search_word_forward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                       CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     search_word(vim, action, view, motion_range);
     return ce_vim_motion_search_next(vim, action, view, config_options, buffer_data, motion_range);
}

bool ce_vim_motion_search_word_backward(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                        CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     search_word(vim, action, view, motion_range);
     motion_range->end = motion_range->start;
     return ce_vim_motion_search_prev(vim, action, view, config_options, buffer_data, motion_range);
}

bool ce_vim_motion_match_pair(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                              CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CeRune_t rune = ce_buffer_get_rune(view->buffer, view->cursor);
     CeRange_t result = ce_vim_find_pair(view->buffer, view->cursor, rune, false);
     if(result.start.x < 0) return false;
     if(ce_points_equal(motion_range->end, result.start)){
          motion_range->end = result.end;
          return true;
     }else if(ce_points_equal(motion_range->end, result.end)){
          motion_range->end = result.start;
          return true;
     }
     return false;
}

bool ce_vim_motion_mark(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                        CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t* destination = buffer_data->marks + ce_vim_register_index(action->motion.integer);
     // TODO: come up with better method of determining if a destination is set or not
     if(destination->x != 0 || destination->y != 0){
          motion_range->end = *destination;
          motion_range->end = ce_buffer_clamp_point(view->buffer, motion_range->end, CE_CLAMP_X_INSIDE);
          return true;
     }
     return false;
}

bool ce_vim_motion_mark_soft_begin_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                        CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CePoint_t* destination = buffer_data->marks + ce_vim_register_index(action->motion.integer);
     // TODO: come up with better method of determining if a destination is set or not
     if(destination->x != 0 || destination->y != 0){
          motion_range->end = *destination;
          motion_range->end = ce_buffer_clamp_point(view->buffer, motion_range->end, CE_CLAMP_X_INSIDE);
          motion_range->end.x = ce_vim_soft_begin_line(view->buffer, motion_range->end.y);
          return true;
     }
     return false;
}

bool ce_vim_motion_top_of_view(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                               CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     motion_range->end = (CePoint_t){motion_range->start.x, view->scroll.y + config_options->vertical_scroll_off};
     motion_range->end = ce_buffer_clamp_point(view->buffer, motion_range->end, CE_CLAMP_X_INSIDE);
     return true;
}

bool ce_vim_motion_middle_of_view(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                  CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     int64_t view_height = view->rect.bottom - view->rect.top;
     motion_range->end = (CePoint_t){motion_range->start.x, view->scroll.y + (view_height / 2)};
     motion_range->end = ce_buffer_clamp_point(view->buffer, motion_range->end, CE_CLAMP_X_INSIDE);
     return true;
}

bool ce_vim_motion_bottom_of_view(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                  CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     int64_t view_height = view->rect.bottom - view->rect.top;
     motion_range->end = (CePoint_t){motion_range->start.x, view->scroll.y + (view_height - config_options->vertical_scroll_off)};
     motion_range->end = ce_buffer_clamp_point(view->buffer, motion_range->end, CE_CLAMP_X_INSIDE);
     return true;
}

static bool string_is_blank(const char* string){
     while(*string){
          if(!isblank(*string)) return false;
          string++;
     }
     return true;
}

bool ce_vim_motion_next_blank_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                   CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     bool start_blank = string_is_blank(view->buffer->lines[motion_range->end.y]);
     for(int64_t y = motion_range->end.y + 1; y < view->buffer->line_count; y++){
          bool current_blank = string_is_blank(view->buffer->lines[y]);
          if(current_blank){
               if(!start_blank){
                    motion_range->end = (CePoint_t){0, y};
                    break;
               }
          }else{
               start_blank = false;
          }
     }
     return true;
}

bool ce_vim_motion_previous_blank_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                       CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     bool start_blank = string_is_blank(view->buffer->lines[motion_range->end.y]);
     for(int64_t y = motion_range->end.y - 1; y >= 0; y--){
          bool current_blank = string_is_blank(view->buffer->lines[y]);
          if(current_blank){
               if(!start_blank){
                    motion_range->end = (CePoint_t){0, y};
                    break;
               }
          }else{
               start_blank = false;
          }
     }
     return true;
}

bool ce_vim_motion_next_zero_indentation_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                              CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CeAppBufferData_t* buffer_app_data = view->buffer->app_data;
     for(int64_t y = motion_range->end.y + 1; y < view->buffer->line_count; y++){
          if(ce_utf8_strlen(view->buffer->lines[y]) == 0) continue;
          if(buffer_app_data->syntax_function == ce_syntax_highlight_c ||
             buffer_app_data->syntax_function == ce_syntax_highlight_cpp){
               if(view->buffer->lines[y][0] == '#' ||
                  view->buffer->lines[y][0] == '/'){
                    continue;
               }
          }

          if(isprint(view->buffer->lines[y][0]) && !isspace(view->buffer->lines[y][0]) && strchr(view->buffer->lines[y], '(')){
               motion_range->end = (CePoint_t){0, y};
               return true;
          }
     }
     return false;
}

bool ce_vim_motion_previous_zero_indentation_line(CeVim_t* vim, CeVimAction_t* action, const CeView_t* view, const CeConfigOptions_t* config_options,
                                                  CeVimBufferData_t* buffer_data, CeRange_t* motion_range){
     CeAppBufferData_t* buffer_app_data = view->buffer->app_data;
     for(int64_t y = motion_range->end.y - 1; y >= 0; y--){
          if(ce_utf8_strlen(view->buffer->lines[y]) == 0) continue;
          if(buffer_app_data->syntax_function == ce_syntax_highlight_c ||
             buffer_app_data->syntax_function == ce_syntax_highlight_cpp){
               if(view->buffer->lines[y][0] == '#' ||
                  view->buffer->lines[y][0] == '/'){
                    continue;
               }
          }

          if(isprint(view->buffer->lines[y][0]) && !isspace(view->buffer->lines[y][0]) && strchr(view->buffer->lines[y], '(')){
               motion_range->end = (CePoint_t){0, y};
               return true;
          }
     }
     return false;
}

int64_t ce_vim_register_index(CeRune_t rune){
     if(rune < 32 || rune > 126) return -1; // ascii printable character range
     return rune - ' ';
}

void ce_vim_yank_free(CeVimYank_t* yank){
     switch(yank->type){
     default:
          break;
     case CE_VIM_YANK_TYPE_STRING:
     case CE_VIM_YANK_TYPE_LINE:
          free(yank->text);
          yank->text = NULL;
          break;
     case CE_VIM_YANK_TYPE_BLOCK:
          for(int64_t i = 0; i < yank->block_line_count; i++){
               free(yank->block[i]);
          }
          free(yank->block);
          yank->block = NULL;
          yank->block_line_count = 0;
          break;
     }

     yank->type = CE_VIM_YANK_TYPE_STRING;
}

bool ce_vim_verb_motion(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                        CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     if(action->motion.function == ce_vim_motion_up || action->motion.function == ce_vim_motion_down){
          motion_range.end.x = buffer_data->motion_column;
     }
     view->cursor = ce_buffer_clamp_point(view->buffer, motion_range.end, CE_CLAMP_X_INSIDE);
     if(ce_points_equal(motion_range.end, view->cursor)) buffer_data->motion_column = view->cursor.x;
     CePoint_t before_follow = view->scroll;

     // if we are searching, and our cursor goes off the screen, center it
     if(!ce_points_equal(before_follow, view->scroll) &&
        (action->motion.function == ce_vim_motion_search_next ||
         action->motion.function == ce_vim_motion_search_prev)){
          ce_view_center(view);
     }

     return true;
}

bool ce_vim_verb_delete(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                        CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     ce_range_sort(&motion_range);

     // delete the range
     CeVimYankType_t yank_type = action->yank_type;
     if(action->exclude_end){
          motion_range.end = ce_buffer_advance_point(view->buffer, motion_range.end, -1);
          if(motion_range.end.x == ce_utf8_strlen(view->buffer->lines[motion_range.end.y])){
               yank_type = CE_VIM_YANK_TYPE_LINE;
          }
     }
     int64_t delete_len = ce_buffer_range_len(view->buffer, motion_range.start, motion_range.end);
     // if the end of the range is at the end of the buffer, take off the extra newline, unless the line is empty
     if(ce_points_equal(motion_range.end, ce_buffer_end_point(view->buffer)) && motion_range.end.x != 0){
          delete_len--;
     }
     char* removed_string = ce_buffer_dupe_string(view->buffer, motion_range.start, delete_len);
     if(!ce_buffer_remove_string(view->buffer, motion_range.start, delete_len)){
          free(removed_string);
          return false;
     }

     CePoint_t end_cursor = ce_buffer_clamp_point(view->buffer, motion_range.start, action->clamp_x);

     // commit the change
     CeBufferChange_t change = {};
     change.chain = action->chain_undo;
     change.insertion = false;
     change.string = removed_string;
     change.location = motion_range.start;
     change.cursor_before = view->cursor;
     change.cursor_after = end_cursor;
     ce_buffer_change(view->buffer, &change);

     view->cursor = end_cursor;
     vim->chain_undo = action->chain_undo;
     vim->mode = CE_VIM_MODE_NORMAL;

     if(!action->do_not_yank){
          CeVimYank_t* yank = vim->yanks + ce_vim_register_index('"');
          ce_vim_yank_free(yank);
          yank->text = strdup(removed_string);
          yank->type = yank_type;
     }
     return true;
}

bool ce_vim_verb_change(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                        CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     if(!ce_vim_verb_delete(vim, action, motion_range, view, buffer_data, config_options)) return false;
     vim->chain_undo = true;
     insert_mode(vim);
     return true;
}

static bool change_character(CeView_t* view, CePoint_t point, CeRune_t new_rune, bool chain_undo){
     int64_t rune_len = 0;

     // delete current rune
     ce_buffer_remove_string_change(view->buffer, point, 1, &view->cursor,
                                    view->cursor, chain_undo);

     // insert new rune
     char* current_string = malloc(5);
     ce_utf8_encode(new_rune, current_string, 5, &rune_len);
     current_string[rune_len] = 0;

     if(!ce_buffer_insert_string_change(view->buffer, current_string, point,
                                        &view->cursor, view->cursor, true)){
          return false;
     }

     return true;
}

bool ce_vim_verb_set_character(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                               CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     ce_range_sort(&motion_range);

     bool chain_undo = false;
     while(!ce_point_after(motion_range.start, motion_range.end)){
          // do nothing if we aren't on the buffer, or the line is empty
          if(!ce_buffer_contains_point(view->buffer, motion_range.start)){
               motion_range.start = ce_buffer_advance_point(view->buffer, motion_range.start, 1);
               continue;
          }else if(view->buffer->lines[motion_range.start.y][0] == 0){
               motion_range.start = ce_buffer_advance_point(view->buffer, motion_range.start, 1);
               continue;
          }

          if(!change_character(view, motion_range.start, action->verb.integer, chain_undo)) return false;

          motion_range.start = ce_buffer_advance_point(view->buffer, motion_range.start, 1);
          if(motion_range.start.x == -1) return false;
          chain_undo = true;
     }

     return true;
}

bool ce_vim_verb_delete_character(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                                  CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     ce_range_sort(&motion_range);

     while(!ce_point_after(motion_range.start, motion_range.end)){
          ce_buffer_remove_string_change(view->buffer, view->cursor, 1, &view->cursor,
                                         view->cursor, false);

          CePoint_t new_start = ce_buffer_advance_point(view->buffer, motion_range.start, 1);
          if(ce_points_equal(new_start, motion_range.start)) return false;
          motion_range.start = new_start;
     }

     view->cursor = ce_buffer_clamp_point(view->buffer, motion_range.end, action->clamp_x);
     return true;
}

bool ce_vim_verb_substitute_character(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                                      CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     bool success = ce_vim_verb_delete_character(vim, action, motion_range, view, buffer_data, config_options);
     if(success){
          vim->chain_undo = true;
          vim->mode = CE_VIM_MODE_INSERT;
     }
     return success;
}

bool ce_vim_verb_substitute_soft_begin_line(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                                            CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     int64_t soft_begin_index = ce_vim_soft_begin_line(view->buffer, view->cursor.y);
     if(soft_begin_index < 0) return false;

     view->cursor.x = soft_begin_index;
     motion_range.start = view->cursor;
     motion_range.end.x = ce_utf8_last_index(view->buffer->lines[motion_range.end.y]);
     bool success = ce_vim_verb_change(vim, action, motion_range, view, buffer_data, config_options);
     if(success){
          vim->chain_undo = true;
          vim->mode = CE_VIM_MODE_INSERT;
     }
     return success;
}

bool ce_vim_verb_yank(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                      CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     CeVimYank_t* yank = vim->yanks + ce_vim_register_index(action->verb.integer);
     ce_vim_yank_free(yank);
     int64_t yank_len = 0;
     yank_len = ce_buffer_range_len(view->buffer, motion_range.start, motion_range.end);
     yank->text = ce_buffer_dupe_string(view->buffer, motion_range.start, yank_len);
     yank->type = action->yank_type;
     vim->mode = CE_VIM_MODE_NORMAL;
     return true;
}

static bool paste_text(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                       const CeConfigOptions_t* config_options, bool after){
     CeVimYank_t* yank = vim->yanks + ce_vim_register_index(action->verb.integer);
     if(!yank->text) return false;

     CePoint_t insertion_point = motion_range.end;

     switch(yank->type){
     default:
          break;
     case CE_VIM_YANK_TYPE_LINE:
          insertion_point.x = 0;
          if(after) insertion_point.y++;
          break;
     case CE_VIM_YANK_TYPE_STRING:
          if(after){
               insertion_point.x++;
               int64_t line_len = ce_utf8_strlen(view->buffer->lines[insertion_point.y]);
               if(insertion_point.x > line_len) insertion_point.x = line_len - 1;
               if(insertion_point.x < 0) insertion_point.x = 0;
          }
          break;
     case CE_VIM_YANK_TYPE_BLOCK:
     {
          bool chain = false;

          // if the buffer isn't long enough, insert some lines
          int64_t line_count_check = insertion_point.y + yank->block_line_count;
          if(line_count_check > view->buffer->line_count){
               int64_t line_diff = line_count_check - view->buffer->line_count;
               char* newline_str = malloc(line_diff + 1);
               memset(newline_str, '\n', line_diff);
               newline_str[line_diff] = 0;

               CePoint_t newline_point = ce_buffer_end_point(view->buffer);
               if(!ce_buffer_insert_string(view->buffer, newline_str, newline_point)) return false;

               // commit the change
               CeBufferChange_t change = {};
               change.chain = chain;
               change.insertion = true;
               change.string = newline_str;
               change.location = newline_point;
               change.cursor_before = view->cursor;
               change.cursor_after = view->cursor;
               ce_buffer_change(view->buffer, &change);
               chain = true;
          }

          for(int64_t i = 0; i < yank->block_line_count; i++){
               if(!yank->block[i]) continue;
               CePoint_t point = {insertion_point.x, insertion_point.y + i};

               // if the line isn't long enough, append space so it is long enough
               int64_t line_len = ce_utf8_strlen(view->buffer->lines[point.y]);
               if(insertion_point.x > line_len){
                    int64_t space_len = (insertion_point.x - line_len);
                    char* space_str = malloc(space_len + 1);
                    memset(space_str, ' ', space_len);
                    space_str[space_len] = 0;

                    CePoint_t space_point = {line_len, point.y};
                    if(!ce_buffer_insert_string(view->buffer, space_str, space_point)) return false;

                    // commit the change
                    CeBufferChange_t change = {};
                    change.chain = chain;
                    change.insertion = true;
                    change.string = space_str;
                    change.location = space_point;
                    change.cursor_before = view->cursor;
                    change.cursor_after = view->cursor;
                    ce_buffer_change(view->buffer, &change);
                    chain = true;
               }

               char* insert_str = strdup(yank->block[i]);
               if(!ce_buffer_insert_string(view->buffer, insert_str, point)) return false;

               // commit the change
               CeBufferChange_t change = {};
               change.chain = chain;
               change.insertion = true;
               change.string = insert_str;
               change.location = point;
               change.cursor_before = view->cursor;
               change.cursor_after = view->cursor;
               ce_buffer_change(view->buffer, &change);
               chain = true;
          }

          vim->mode = CE_VIM_MODE_NORMAL;
          return true;
     }
     }

     char* insert_str = strdup(yank->text);

     // if we are inserting at the end of a file, put the newline at the beginning and insert at the end of the previous line
     if(insertion_point.x == 0 && insertion_point.y == view->buffer->line_count){
          int64_t insert_len = strlen(insert_str);
          if(insert_len > 0){
               int64_t last_index = insert_len - 1;
               if(insert_str[last_index] == CE_NEWLINE){
                    for(int64_t i = last_index; i > 0; i--){
                         insert_str[i] = insert_str[i - 1];
                    }
                    insert_str[0] = CE_NEWLINE;
                    insertion_point.y = view->buffer->line_count - 1;
                    insertion_point.x = ce_utf8_strlen(view->buffer->lines[insertion_point.y]);
               }
          }
     }

     if(!ce_buffer_insert_string(view->buffer, insert_str, insertion_point)) return false;

     CePoint_t cursor_end = {};

     switch(yank->type){
     default:
          break;
     case CE_VIM_YANK_TYPE_LINE:
          cursor_end = insertion_point;
          break;
     case CE_VIM_YANK_TYPE_STRING:
          cursor_end = ce_buffer_advance_point(view->buffer, view->cursor, ce_utf8_strlen(yank->text));
          break;
     }

     // commit the change
     CeBufferChange_t change = {};
     change.chain = action->chain_undo;
     change.insertion = true;
     change.string = insert_str;
     change.location = insertion_point;
     change.cursor_before = view->cursor;
     change.cursor_after = cursor_end;
     ce_buffer_change(view->buffer, &change);

     view->cursor = cursor_end;
     vim->mode = CE_VIM_MODE_NORMAL;

     return true;
}

bool ce_vim_verb_paste_before(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                              CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     return paste_text(vim, action, motion_range, view, config_options, false);
}

bool ce_vim_verb_paste_after(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                             CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     return paste_text(vim, action, motion_range, view, config_options, true);
}

bool ce_vim_verb_open_above(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                            CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     // insert newline on next line
     char* insert_string = strdup("\n");
     motion_range.start.x = 0;

     if(!ce_buffer_insert_string_change(view->buffer, insert_string, motion_range.start, &view->cursor, view->cursor,
                                        false)){
          return false;
     }

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

          if(!ce_buffer_insert_string_change(view->buffer, insert_string, indentation_point, &view->cursor, cursor_end,
                                             true)){
               return false;
          }
     }else{
          view->cursor = cursor_end;
     }

     vim->chain_undo = true;
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_open_below(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                            CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     // insert newline at the end of the current line
     char* insert_string = strdup("\n");
     motion_range.start.x = ce_utf8_strlen(view->buffer->lines[motion_range.start.y]);
     if(!ce_buffer_insert_string_change(view->buffer, insert_string, motion_range.start, &view->cursor, view->cursor,
                                        false)){
          return false;
     }

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

          if(!ce_buffer_insert_string_change(view->buffer, insert_string, indentation_point, &view->cursor, cursor_end,
                                             true)){
               return false;
          }
     }else{
          view->cursor = cursor_end;
     }

     vim->chain_undo = true;
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_undo(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                      CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     return ce_buffer_undo(view->buffer, &view->cursor);
}

bool ce_vim_verb_redo(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                      CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     return ce_buffer_redo(view->buffer, &view->cursor);
}

bool ce_vim_verb_insert_mode(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                             CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_replace_mode(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                              CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     vim->mode = CE_VIM_MODE_REPLACE;
     if(!vim->verb_last_action) ce_rune_node_free(&vim->insert_rune_head);
     return true;
}

bool ce_vim_verb_visual_mode(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                             CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     vim->visual = view->cursor;
     vim->mode = CE_VIM_MODE_VISUAL;
     return true;
}

bool ce_vim_verb_visual_line_mode(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                                  CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     vim->visual = view->cursor;
     vim->mode = CE_VIM_MODE_VISUAL_LINE;
     return true;
}

bool ce_vim_verb_normal_mode(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                             CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     vim->mode = CE_VIM_MODE_NORMAL;
     return true;
}

bool ce_vim_verb_append(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                        CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     view->cursor.x++;
     if(ce_utf8_strlen(view->buffer->lines[view->cursor.y]) == 0) view->cursor.x = 0;
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_append_at_end_of_line(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                                       CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     view->cursor.x = ce_utf8_strlen(view->buffer->lines[view->cursor.y]);
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_insert_at_soft_begin_line(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                                           CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     int64_t result = ce_vim_soft_begin_line(view->buffer, view->cursor.y);
     if(result < 0) return false;
     view->cursor.x = result;
     insert_mode(vim);
     return true;
}

bool ce_vim_verb_last_action(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                             CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     CeBufferChangeNode_t* change_node = view->buffer->change_node;

     vim->verb_last_action = true;
     CeVimMode_t save_mode = vim->mode;
     assert(vim->last_action.verb.function != ce_vim_verb_last_action);
     if(!ce_vim_apply_action(vim, &vim->last_action, view, buffer_data, config_options)){
          vim->verb_last_action = false;
          return false;
     }

     if(vim->insert_rune_head){
          CeRune_t* rune_string = ce_rune_node_string(vim->insert_rune_head);
          CeRune_t* itr = rune_string;

          while(*itr){
               insert_mode_handle_key(vim, view, *itr, config_options);
               itr++;
          }

          free(rune_string);
     }

     if(change_node && change_node->next) change_node->next->change.chain = false;

     vim->mode = save_mode;
     vim->verb_last_action = false;
     return true;
}

bool ce_vim_verb_z_command(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                           CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     switch(action->verb.integer){
     default:
          break;
     case 'z':
     {
          ce_view_center(view);
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

bool ce_vim_verb_g_command(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                           CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     if(action->multiplier > 1){
          if(action->multiplier > view->buffer->line_count) return false;
          view->cursor.y = action->multiplier - 1;
          view->cursor.x = ce_vim_soft_begin_line(view->buffer, view->cursor.y);
          ce_view_follow_cursor(view, config_options->horizontal_scroll_off,
                                config_options->vertical_scroll_off,
                                config_options->tab_width);
          return true;
     }

     switch(action->verb.integer){
     default:
          break;
     case 'g':
          motion_range.end = (CePoint_t){0, 0};
          return ce_vim_verb_motion(vim, action, motion_range, view, buffer_data, config_options);
     case 'v':
          vim->visual = view->cursor;
          vim->mode = CE_VIM_MODE_VISUAL_BLOCK;
          return true;
     }

     return false;
}

bool ce_vim_verb_indent(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                        CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     bool chain = false;
     ce_range_sort(&motion_range);

     CePoint_t end_cursor = view->cursor;

     for(int64_t i = motion_range.start.y; i <= motion_range.end.y; i++){
          if(view->buffer->lines[i][0] == 0) continue;

          // calc indentation
          CePoint_t indentation_point = {0, i};

          // build indentation string
          char* insert_string = malloc(config_options->tab_width + 1);
          memset(insert_string, ' ', config_options->tab_width);
          insert_string[config_options->tab_width] = 0;

          // insert indentation
          if(!ce_buffer_insert_string(view->buffer, insert_string, indentation_point)) return false;

          if(view->cursor.y == i){
               end_cursor = ce_buffer_advance_point(view->buffer, view->cursor, config_options->tab_width);
          }

          // commit the change
          CeBufferChange_t change = {};
          change.chain = chain;
          change.insertion = true;
          change.string = insert_string;
          change.location = indentation_point;
          change.cursor_before = view->cursor;
          change.cursor_after = end_cursor;
          ce_buffer_change(view->buffer, &change);
          chain = true;
     }

     if(vim->mode == CE_VIM_MODE_VISUAL_LINE) end_cursor.y = motion_range.start.y;
     if(chain) view->cursor = end_cursor; // if we did any indentation at all, update the cursor
     vim->mode = CE_VIM_MODE_NORMAL;
     return true;
}

bool ce_vim_verb_unindent(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                          CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     bool chain = false;
     ce_range_sort(&motion_range);
     CePoint_t end_cursor = {view->cursor.x - config_options->tab_width, view->cursor.y};
     if(end_cursor.x < 0) end_cursor.x = 0;
     for(int64_t i = motion_range.start.y; i <= motion_range.end.y; i++){
          // calc indentation
          CePoint_t indentation_point = {0, i};
          int64_t tab_width = 0;

          // figure out how much we can unindent
          for(int64_t s = 0; s < config_options->tab_width; s++){
               if(isblank(view->buffer->lines[i][s])){
                    tab_width++;
               }else{
                    break;
               }
          }

          if(tab_width){
               // build indentation string
               char remove_string[tab_width + 1];
               memset(remove_string, ' ', tab_width);
               remove_string[tab_width] = 0;

               ce_buffer_remove_string_change(view->buffer, indentation_point, strlen(remove_string), &view->cursor,
                                              view->cursor, chain);
               chain = true;
          }
     }

     if(vim->mode == CE_VIM_MODE_VISUAL_LINE) end_cursor.y = motion_range.start.y;
     if(chain) view->cursor = end_cursor; // if we did any indentation at all, update the cursor
     vim->mode = CE_VIM_MODE_NORMAL;
     return true;
}

bool ce_vim_verb_join(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                      CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     if(view->cursor.y < 0) return false;
     if(view->cursor.y >= (view->buffer->line_count - 1)) return false;

     // delete up to the soft beginning of the line
     CePoint_t beginning_of_next_line = {0, view->cursor.y + 1};
     int64_t whitespace_len = ce_vim_soft_begin_line(view->buffer, beginning_of_next_line.y);
     if(whitespace_len > 0){
          ce_buffer_remove_string_change(view->buffer, beginning_of_next_line, whitespace_len, &view->cursor,
                                         view->cursor, false);
     }

     bool insert_space = (strlen(view->buffer->lines[view->cursor.y + 1]) > 0);
     CePoint_t point = {ce_utf8_strlen(view->buffer->lines[view->cursor.y]), view->cursor.y};
     ce_vim_join_next_line(view->buffer, view->cursor.y, view->cursor, true);

     if(insert_space){
          char* space_str = strdup(" ");
          CePoint_t end_cursor = (CePoint_t){point.x + 1, point.y};
          ce_buffer_insert_string_change(view->buffer, space_str, point, &view->cursor, end_cursor, true);
     }

     return true;
}

bool ce_vim_verb_flip_case(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                           CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     bool chain_undo = false;
     while(!ce_points_equal(motion_range.start, motion_range.end)){
          // do nothing if we aren't on the buffer, or the line is empty
          if(!ce_buffer_contains_point(view->buffer, motion_range.start)){
               motion_range.start = ce_buffer_advance_point(view->buffer, motion_range.start, 1);
               continue;
          }else if(view->buffer->lines[motion_range.start.y][0] == 0){
               motion_range.start = ce_buffer_advance_point(view->buffer, motion_range.start, 1);
               continue;
          }

          CeRune_t flipped = ce_buffer_get_rune(view->buffer, motion_range.start);
          if(flipped == CE_UTF8_INVALID) return false;

          if(isupper(flipped)){
               flipped = tolower(flipped);
          }else{
               flipped = toupper(flipped);
          }

          if(!change_character(view, motion_range.start, flipped, chain_undo)) return false;

          motion_range.start = ce_buffer_advance_point(view->buffer, motion_range.start, 1);
          if(motion_range.start.x == -1) return false;
          chain_undo = true;
     }

     view->cursor = motion_range.end;
     return true;
}

bool ce_vim_verb_set_mark(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                          CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     CePoint_t* destination = buffer_data->marks + ce_vim_register_index(action->verb.integer);
     *destination = view->cursor;
     return true;
}

static bool change_number(CeView_t* view, CePoint_t point, int64_t delta){
     char* start = ce_utf8_iterate_to(view->buffer->lines[point.y], point.x);
     char* itr = start;
     while(*itr && !isdigit(*itr)){
          itr++;
     }

     if(!(*itr)) return false;

     // loop backward if we are inside a number, checking for the beginning or for the negative sign
     while(itr > view->buffer->lines[point.y]){
          itr--;
          if(!isdigit(*itr)){
               if(*itr == '-') break;
               itr++;
               break;
          }
     }

     if(itr < view->buffer->lines[point.y]) itr = view->buffer->lines[point.y];

     char* end = NULL;
     int64_t value = strtol(itr, &end, 10);
     value += delta;
     assert(end);

     int64_t distance_to_number = ce_utf8_strlen_between(view->buffer->lines[point.y], itr) - 1;

     int64_t number_len = 0;
     if(*end){
          number_len = ce_utf8_strlen_between(itr, end) - 1;
     }else{
          number_len = ce_utf8_strlen(itr);
     }

     CePoint_t change_point = {distance_to_number, point.y};
     CePoint_t change_point_save = change_point;

     int64_t digits = ce_count_digits(value);
     if(value < 0) digits++; // account for negative sign
     char* new_number_string = malloc(digits + 1);
     snprintf(new_number_string, digits + 1, "%ld", value);
     new_number_string[digits] = 0;

     ce_buffer_remove_string_change(view->buffer, change_point, number_len, &change_point_save,
                                    change_point, false);
     change_point_save = change_point;
     ce_buffer_insert_string_change_at_cursor(view->buffer, new_number_string, &change_point_save, true);
     view->cursor = change_point;
     return true;
}

bool ce_vim_verb_increment_number(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                                  CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     return change_number(view, motion_range.end, 1);
}

bool ce_vim_verb_decrement_number(CeVim_t* vim, const CeVimAction_t* action, CeRange_t motion_range, CeView_t* view,
                                  CeVimBufferData_t* buffer_data, const CeConfigOptions_t* config_options){
     return change_number(view, motion_range.end, -1);
}
