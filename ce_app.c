#include "ce_app.h"
#include "ce_syntax.h"

#include <string.h>
#include <stdlib.h>
#include <ncurses.h>

bool ce_buffer_node_insert(CeBufferNode_t** head, CeBuffer_t* buffer){
     CeBufferNode_t* node = malloc(sizeof(*node));
     if(!node) return false;
     node->buffer = buffer;
     node->next = *head;
     *head = node;
     return true;
}

bool ce_buffer_node_delete(CeBufferNode_t** head, CeBuffer_t* buffer){
     CeBufferNode_t* prev = NULL;
     CeBufferNode_t* itr = *head;
     while(itr){
          if(itr->buffer == buffer) break;
          prev = itr;
          itr = itr->next;
     }

     if(!itr) return false;

     if(prev){
          prev->next = itr->next;
     }else{
          *head = (*head)->next;
     }

     // TODO: compress with below
     free(itr->buffer->app_data);
     ce_buffer_free(itr->buffer);
     free(itr->buffer);
     free(itr);
     return true;
}

void ce_buffer_node_free(CeBufferNode_t** head){
     CeBufferNode_t* itr = *head;
     while(itr){
          CeBufferNode_t* tmp = itr;
          itr = itr->next;
          free(tmp->buffer->app_data);
          ce_buffer_free(tmp->buffer);
          free(tmp->buffer);
          free(tmp);
     }
     *head = NULL;
}

CeStringNode_t* ce_string_node_insert(CeStringNode_t** head, const char* string){
     CeStringNode_t* tail = *head;
     CeStringNode_t* node;
     if(tail){
          while(tail->next) tail = tail->next;

          // NOTE: we probably don't want this if we want the linked list to be general
          // skip the insertion if the string matches the previous string
          if(strcmp(string, tail->string) == 0) return NULL;

          node = calloc(1, sizeof(*node));
          if(!node) return node;
          node->string = strdup(string);

          tail->next = node;
          node->prev = tail;
     }else{
          node = calloc(1, sizeof(*node));
          if(!node) return node;
          node->string = strdup(string);

          *head = node;
     }

     return node;
}

void ce_string_node_free(CeStringNode_t** head){
     CeStringNode_t* itr = *head;
     while(itr){
          CeStringNode_t* tmp = itr;
          itr = itr->next;
          free(tmp->string);
          free(tmp);
     }

     *head = NULL;
}

bool ce_history_insert(History_t* history, const char* string){
     CeStringNode_t* new_node = ce_string_node_insert(&history->head, string);
     if(new_node) return true;
     return false;
}

char* ce_history_previous(History_t* history){
     if(history->current){
          if(history->current->prev){
               history->current = history->current->prev;
          }
          return history->current->string;
     }

     CeStringNode_t* tail = history->head;
     if(!tail) return NULL;

     while(tail->next) tail = tail->next;
     history->current = tail;

     return tail->string;
}

char* ce_history_next(History_t* history){
     if(history->current){
          if(history->current->next){
               history->current = history->current->next;
          }
          return history->current->string;
     }

     return NULL;
}

void ce_convert_bind_defs(CeKeyBinds_t* binds, CeKeyBindDef_t* bind_defs, int64_t bind_def_count){
     if(binds->count){
          for(int64_t i = 0; i < binds->count; ++i){
               free(binds->binds[i].keys);
          }
          free(binds->binds);
     }

     binds->count = bind_def_count;
     binds->binds = malloc(binds->count * sizeof(*binds->binds));

     for(int64_t i = 0; i < binds->count; ++i){
          ce_command_parse(&binds->binds[i].command, bind_defs[i].command);
          binds->binds[i].key_count = 0;

          for(int k = 0; k < 4; ++k){
               if(bind_defs[i].keys[k] == 0) break;
               binds->binds[i].key_count++;
          }

          if(!binds->binds[i].key_count) continue;

          binds->binds[i].keys = malloc(binds->binds[i].key_count * sizeof(binds->binds[i].keys[0]));

          for(int k = 0; k < binds->binds[i].key_count; ++k){
               binds->binds[i].keys[k] = bind_defs[i].keys[k];
          }
     }
}

void ce_app_update_terminal_view(CeApp_t* app){
     getmaxyx(stdscr, app->terminal_height, app->terminal_width);
     app->terminal_rect = (CeRect_t){0, app->terminal_width - 1, 0, app->terminal_height - 1};
     ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect);
}

CeComplete_t* ce_app_is_completing(CeApp_t* app){
     if(app->input_mode){
          if(strcmp(app->input_view.buffer->name, "COMMAND") == 0) return &app->command_complete;
          if(strcmp(app->input_view.buffer->name, "LOAD FILE") == 0) return &app->load_file_complete;
          if(strcmp(app->input_view.buffer->name, "SWITCH BUFFER") == 0) return &app->switch_buffer_complete;
     }

     return NULL;
}

void ce_set_vim_key_bind(CeVimKeyBind_t* key_binds, int64_t* key_bind_count, CeRune_t key, CeVimParseFunc_t* parse_func){
     for(int64_t i = 0; i < *key_bind_count; ++i){
          CeVimKeyBind_t* key_bind = key_binds + i;
          if(key_bind->key == key){
               key_bind->function = parse_func;
               return;
          }
     }

     // we didn't find the key to override it, so we add the binding
     ce_vim_add_key_bind(key_binds, key_bind_count, key, parse_func);
}

void ce_extend_commands(CeCommandEntry_t** command_entries, int64_t* command_entry_count, CeCommandEntry_t* new_command_entries,
                        int64_t new_command_entry_count){
     int64_t final_command_entry_count = *command_entry_count + new_command_entry_count;
     *command_entries = realloc(*command_entries, final_command_entry_count * sizeof(**command_entries));
     for(int64_t i = 0; i < new_command_entry_count; i++){
          (*command_entries)[i + *command_entry_count] = new_command_entries[i];
     }
     *command_entry_count = final_command_entry_count;
}

void ce_syntax_highlight_terminal(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                  CeSyntaxDef_t* syntax_defs, void* user_data){
     CeTerminal_t* terminal = user_data;
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     int fg = COLOR_DEFAULT;
     int bg = COLOR_DEFAULT;
     bool in_visual = false;
     CeRangeNode_t* range_node = highlight_range_list->head;

     for(int64_t y = min; y <= max; ++y){
          CePoint_t point = {0, y};

          if(in_visual){
               int new_bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_VISUAL, ce_draw_color_list_last_bg_color(draw_color_list));
               ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), new_bg, point);
          }

          for(int64_t x = 0; x < terminal->columns; ++x){
               point.x = x;
               ce_syntax_highlight_visual(&range_node, &in_visual, point, draw_color_list, syntax_defs);

               CeTerminalGlyph_t* glyph = terminal->lines[y] + x;
               if(glyph->foreground != fg || glyph->background != bg){
                    fg = glyph->foreground;
                    bg = glyph->background;
                    ce_draw_color_list_insert(draw_color_list, fg, bg, point);
               }
          }
     }
}

void ce_syntax_highlight_completions(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                     CeSyntaxDef_t* syntax_defs, void* user_data){
     if(!user_data) return;
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     CeComplete_t* complete = user_data;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     int64_t match_len = 0;
     if(complete->current_match) match_len = strlen(complete->current_match);

     // figure out which line to highlight
     int64_t selected = 0;
     for(int64_t i = 0; i < complete->count; i++){
          if(complete->elements[i].match){
               if(i == complete->current) break;
               selected++;
          }
     }

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          CePoint_t match_point = {0, y};

          if(selected == (y - min)){
               int fg = ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_SELECTED, ce_draw_color_list_last_fg_color(draw_color_list));
               int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_SELECTED, ce_draw_color_list_last_bg_color(draw_color_list));
               ce_draw_color_list_insert(draw_color_list, fg, bg, match_point);
          }else{
               ce_draw_color_list_insert(draw_color_list, COLOR_DEFAULT, COLOR_DEFAULT, match_point);
          }

          if(complete->current_match && strlen(complete->current_match)){
               char* prev_match = line;
               char* match = NULL;
               while((match = strstr(prev_match, complete->current_match))){
                    match_point.x = ce_utf8_strlen_between(line, match) - 1;
                    prev_match = match + match_len;
                    int fg = ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_MATCH, ce_draw_color_list_last_fg_color(draw_color_list));
                    int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_COMPLETE_MATCH, ce_draw_color_list_last_bg_color(draw_color_list));
                    ce_draw_color_list_insert(draw_color_list, fg, bg, match_point);

                    match_point.x += match_len;
                    fg = ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, ce_draw_color_list_last_fg_color(draw_color_list));
                    bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, ce_draw_color_list_last_bg_color(draw_color_list));
                    ce_draw_color_list_insert(draw_color_list, fg, bg, match_point);
               }
          }
     }
}

bool ce_jump_list_insert(CeJumpList_t* jump_list, CeDestination_t destination){
     if(jump_list->count == 0 && jump_list->current <= 0){
          jump_list->destinations[0] = destination;
          jump_list->count = 1;
          jump_list->current = 0;
          return true;
     }

     // if we are on the latest jump
     if(jump_list->current == (jump_list->count - 1)){
          if(jump_list->count >= JUMP_LIST_COUNT) return false;

          jump_list->destinations[jump_list->count] = destination;
          jump_list->count++;
          jump_list->current++;
          return true;
     }else if(jump_list->current == jump_list->count){
          if(jump_list->count >= JUMP_LIST_COUNT) return false;
          // TODO: we need to shift down the jumps
     }

     jump_list->current++;
     jump_list->count = jump_list->current + 1;
     jump_list->destinations[jump_list->current] = destination;
     return true;
}

CeDestination_t* ce_jump_list_previous(CeJumpList_t* jump_list){
     if(jump_list->current < 0) return NULL;
     CeDestination_t* result = jump_list->destinations + jump_list->current;
     jump_list->current--;
     return result;
}

CeDestination_t* ce_jump_list_next(CeJumpList_t* jump_list){
     if(jump_list->current > (jump_list->count - 1)) return NULL;
     jump_list->current++;
     CeDestination_t* result = jump_list->destinations + jump_list->current;
     return result;
}

void ce_view_switch_buffer(CeView_t* view, CeBuffer_t* buffer, CeVim_t* vim, CeConfigOptions_t* config_options){
     // save the cursor on the old buffer
     view->buffer->cursor_save = view->cursor;
     view->buffer->scroll_save = view->scroll;

     // update new buffer, using the buffer's cursor
     view->buffer = buffer;
     view->cursor = buffer->cursor_save;
     view->scroll = buffer->scroll_save;

     ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off, config_options->tab_width);

     vim->mode = CE_VIM_MODE_NORMAL;
}

void ce_run_command_in_terminal(CeTerminal_t* terminal, const char* command){
     while(*command){
          ce_terminal_send_key(terminal, *command);
          command++;
     }

     ce_terminal_send_key(terminal, 10);
}

CeView_t* ce_switch_to_terminal(CeApp_t* app, CeView_t* view, CeLayout_t* tab_layout){
     CeLayout_t* terminal_layout = ce_layout_buffer_in_view(tab_layout, app->terminal.buffer);
     if(terminal_layout){
          tab_layout->tab.current = terminal_layout;
          app->vim.mode = CE_VIM_MODE_INSERT;
          return &terminal_layout->view;
     }

     ce_view_switch_buffer(view, app->terminal.buffer, &app->vim, &app->config_options);
     app->vim.mode = CE_VIM_MODE_INSERT;

     int64_t width = view->rect.right - view->rect.left;
     int64_t height = view->rect.bottom - view->rect.top;
     ce_terminal_resize(&app->terminal, width, height);
     return view;
}
