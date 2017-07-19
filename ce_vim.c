#include "ce_vim.h"

#include <string.h>
#include <ctype.h>
#include <ncurses.h>

bool ce_vim_init(CeVim_t* vim){
     vim->chain_undo = false;
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

bool ce_vim_handle_key(CeVim_t* vim, CeView_t* view, CeRune_t key, CeConfigOptions_t* config_options){
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
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){-1, 0}, config_options->tab_width, true);
               view->scroll = ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off, config_options->tab_width);
               vim->chain_undo = false;
               break;
          case KEY_DOWN:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){0, 1}, config_options->tab_width, true);
               view->scroll = ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off, config_options->tab_width);
               vim->chain_undo = false;
               break;
          case KEY_UP:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){0, -1}, config_options->tab_width, true);
               view->scroll = ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off, config_options->tab_width);
               vim->chain_undo = false;
               break;
          case KEY_RIGHT:
               view->cursor = ce_buffer_move_point(view->buffer, view->cursor, (CePoint_t){1, 0}, config_options->tab_width, true);
               view->scroll = ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off, config_options->tab_width);
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
          CeVimParseActionResult_t result = ce_vim_parse_action(vim->current_command, &action);

          if(result == CE_VIM_PARSE_COMPLETE){
               ce_vim_apply_action(view->buffer, &view->cursor, );
          }

          return result;
     } break;
     }

     return CE_VIM_PARSE_COMPLETE;
}

CeVimParseResult_t ce_vim_parse_action(const CeRune_t* keys, CeVimAction_t* action){
     CeVimAction_t action = {};

     // parse multiplier if it exists

     // parse verb

     // parse multiplier

     // parse motion

     return CE_VIM_PARSE_COMPLETE
}
