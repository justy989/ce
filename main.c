#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <ncurses.h>
#include <unistd.h>
#include <ctype.h>

#include "ce.h"
#include "ce_vim.h"

typedef struct BufferNode_t{
     CeBuffer_t* buffer;
     struct BufferNode_t* next;
}BufferNode_t;

bool buffer_node_insert(BufferNode_t** head, CeBuffer_t* buffer){
     BufferNode_t* node = malloc(sizeof(*node));
     if(!node) return false;
     node->buffer = buffer;
     node->next = *head;
     *head = node;
     return true;
}

typedef struct DrawColorNode_t{
     int fg;
     int bg;
     CePoint_t point;
     struct DrawColorNode_t* next;
}DrawColorNode_t;

typedef struct{
     DrawColorNode_t* head;
     DrawColorNode_t* tail;
}DrawColorList_t;

bool draw_color_list_insert(DrawColorList_t* list, int fg, int bg, CePoint_t point){
     DrawColorNode_t* node = malloc(sizeof(*node));
     if(!node) return false;
     node->fg = fg;
     node->bg = bg;
     node->point = point;
     node->next = NULL;
     if(list->tail) list->tail->next = node;
     list->tail = node;
     if(!list->head) list->head = node;
     return true;
}

void draw_color_list_free(DrawColorList_t* list){
     DrawColorNode_t* itr = list->head;
     while(itr){
          DrawColorNode_t* tmp = itr;
          itr = itr->next;
          free(tmp);
     }

     list->head = NULL;
     list->tail = NULL;
}

typedef struct{
     int fg;
     int bg;
}ColorPair_t;

typedef struct{
     int32_t count;
     int32_t current;
     ColorPair_t pairs[256]; // NOTE: this is what COLOR_PAIRS was for me (which is for some reason not const?)
}ColorDefs_t;

int color_def_get(ColorDefs_t* color_defs, int fg, int bg){
     // search for the already defined color
     for(int32_t i = 0; i < color_defs->count; ++i){
          if(color_defs->pairs[i].fg == fg && color_defs->pairs[i].bg == bg){
               return i;
          }
     }

     // increment the color pair we are going to define, but make sure it wraps around to 0 at the max
     color_defs->current++;
     color_defs->current %= 256;
     if(color_defs->current <= 0) color_defs->current = 1; // when we wrap around, start at 1, because curses doesn't like 0 index color pairs

     // create the pair definition
     init_pair(color_defs->current, fg, bg);

     // set our internal definition
     color_defs->pairs[color_defs->current].fg = fg;
     color_defs->pairs[color_defs->current].bg = bg;

     if(color_defs->current >= color_defs->count){
          color_defs->count = color_defs->current + 1;
     }

     return color_defs->current;
}

bool buffer_append_on_new_line(CeBuffer_t* buffer, const char* string){
     int64_t last_line = buffer->line_count;
     if(last_line) last_line--;
     int64_t line_len = ce_utf8_strlen(buffer->lines[last_line]);
     if(!ce_buffer_insert_string(buffer, "\n", (CePoint_t){line_len, last_line})) return false;
     int64_t next_line = last_line;
     if(line_len) next_line++;
     return ce_buffer_insert_string(buffer, string, (CePoint_t){0, next_line});
}

void build_buffer_list(CeBuffer_t* buffer, BufferNode_t* head){
     int64_t index = 1;
     char line[256];
     ce_buffer_empty(buffer);
     while(head){
          snprintf(line, 256, "%ld %s %ld", index, head->buffer->name, head->buffer->line_count);
          buffer_append_on_new_line(buffer, line);
          head = head->next;
          index++;
     }
}

void view_switch_buffer(CeView_t* view, CeBuffer_t* buffer){
     // save the cursor on the old buffer
     view->buffer->cursor = view->cursor;

     // update new buffer, using the buffer's cursor
     view->buffer = buffer;
     view->cursor = buffer->cursor;
}

// 60 fps
#define DRAW_USEC_LIMIT 16666

bool custom_vim_verb_substitute(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view, \
                                const CeConfigOptions_t* config_options){
     char reg = action->verb.character;
     if(reg == 0) reg = '"';
     CeVimYank_t* yank = vim->yanks + ce_vim_yank_register_index(reg);
     if(!yank->text) return false;

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
     change.chain = false;
     change.insertion = false;
     change.remove_line_if_empty = action->yank_line;
     change.string = removed_string;
     change.location = motion_range.start;
     change.cursor_before = view->cursor;
     change.cursor_after = motion_range.start;
     ce_buffer_change(view->buffer, &change);

     // insert the yank
     int64_t yank_len = ce_utf8_insertion_strlen(yank->text);
     if(!ce_buffer_insert_string(view->buffer, yank->text, motion_range.start)) return false;
     CePoint_t cursor_end = ce_buffer_advance_point(view->buffer, motion_range.start, yank_len);

     // commit the change
     change.chain = true;
     change.insertion = true;
     change.remove_line_if_empty = true;
     change.string = strdup(yank->text);
     change.location = motion_range.start;
     change.cursor_before = view->cursor;
     change.cursor_after = cursor_end;
     ce_buffer_change(view->buffer, &change);

     view->cursor = cursor_end;
     vim->chain_undo = action->chain_undo;

     return true;
}

CeVimParseResult_t custom_vim_parse_verb_substitute(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &custom_vim_verb_substitute;
     return CE_VIM_PARSE_IN_PROGRESS;
}

static int64_t match_words(const char* str, const char** words, int64_t word_count){
     for(int64_t i = 0; i < word_count; ++i){
          int64_t word_len = strlen(words[i]);
          if(strncmp(words[i], str, word_len) == 0){
               return word_len;
          }
     }

     return 0;
}

static bool is_c_type_char(int ch){
     return isalnum(ch) || ch == '_';
}

int64_t match_c_type(const char* str, const char* beginning_of_line){
     if(!isalpha(*str)) return false;

     const char* itr = str;
     while(*itr){
          if(!is_c_type_char(*itr)) break;
          itr++;
     }

     int64_t len = itr - str;
     if(len <= 2) return 0;

     if(strncmp((itr - 2), "_t", 2) == 0) return len;

     // weed out middle of words
     if(str > beginning_of_line){
          if(is_c_type_char(*(str - 1))) return 0;
     }

     static const char* keywords[] = {
          "bool",
          "char",
          "double",
          "float",
          "int",
          "long",
          "short",
          "signed",
          "unsigned",
          "void",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, keywords, keyword_count);
}

int64_t match_c_keyword(const char* str, const char* beginning_of_line){
     static const char* keywords[] = {
          "__thread",
          "auto",
          "case",
          "default",
          "do",
          "else",
          "enum",
          "extern",
          "false",
          "for",
          "if",
          "inline",
          "register",
          "sizeof",
          "static",
          "struct",
          "switch",
          "true",
          "typedef",
          "typeof",
          "union",
          "volatile",
          "while",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     // weed out middle of words
     if(str > beginning_of_line){
          if(is_c_type_char(*(str - 1))) return 0;
     }

     return match_words(str, keywords, keyword_count);
}

int64_t match_c_control(const char* str){
     static const char* keywords [] = {
          "break",
          "const",
          "continue",
          "goto",
          "return",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, keywords, keyword_count);
}

static bool is_caps_var_char(int ch){
     return (ch >= 'A' && ch <= 'Z') || ch == '_';
}

int64_t match_caps_var(const char* str){
     const char* itr = str;
     while(*itr){
          if(!is_caps_var_char(*itr)) break;
          itr++;
     }

     int64_t len = itr - str;
     if(len > 1) return len;
     return 0;
}

int64_t match_c_preproc(const char* str){
     if(*str == '#'){
          const char* itr = str + 1;
          while(*itr){
               if(!isalpha(*itr)) break;
               itr++;
          }

          return itr - str;
     }

     return 0;
}

int64_t match_c_comment(const char* str){
     if(strncmp("//", str, 2) == 0) return ce_utf8_strlen(str);

     return 0;
}

int64_t match_c_multiline_comment(const char* str){
     if(strncmp("/*", str, 2) == 0) return ce_utf8_strlen(str);

     return 0;
}

int64_t match_c_multiline_comment_end(const char* str){
     if(strncmp("*/", str, 2) == 0) return 2;

     return 0;
}

int64_t match_c_string(const char* str){
     if(*str == '"'){
          const char* match = str;
          while(match){
               match = strchr(match + 1, '"');
               if(match && *(match - 1) != '\\'){
                    return (match - str) + 1;
               }
          }
     }

     return 0;
}

int64_t match_c_character_literal(const char* str){
     if(*str == '\''){
          const char* match = str;
          while(match){
               match = strchr(match + 1, '\'');
               if(match && *(match - 1) != '\\'){
                    int64_t len = (match - str) + 1;
                    if(len == 3) return len;
                    if(*(str + 1) == '\\') return len;
                    return 0;
               }
          }
     }

     return 0;
}

static int64_t match_c_literal(const char* str, const char* beginning_of_line)
{
     const char* itr = str;
     int64_t count = 0;
     char ch = *itr;
     bool seen_decimal = false;
     bool seen_hex = false;
     bool seen_u = false;
     bool seen_digit = false;
     int seen_l = 0;

     while(ch != 0){
          if(isdigit(ch)){
               if(seen_u || seen_l) break;
               seen_digit = true;
               count++;
          }else if(!seen_decimal && ch == '.'){
               if(seen_u || seen_l) break;
               seen_decimal = true;
               count++;
          }else if(ch == 'f' && seen_decimal){
               if(seen_u || seen_l) break;
               count++;
               break;
          }else if(ch == '-' && itr == str){
               count++;
          }else if(ch == 'x' && itr == (str + 1)){
               seen_hex = true;
               count++;
          }else if((ch == 'u' || ch == 'U') && !seen_u){
               seen_u = true;
               count++;
          }else if((ch == 'l' || ch == 'L') && seen_l < 2){
               seen_l++;
               count++;
          }else if(seen_hex){
               if(seen_u || seen_l) break;

               bool valid_hex_char = false;

               switch(ch){
               default:
                    break;
               case 'a':
               case 'b':
               case 'c':
               case 'd':
               case 'e':
               case 'f':
               case 'A':
               case 'B':
               case 'C':
               case 'D':
               case 'E':
               case 'F':
                    count++;
                    valid_hex_char = true;
                    break;
               }

               if(!valid_hex_char) break;
          }else{
               break;
          }

          itr++;
          ch = *itr;
     }

     if(count == 1 && (str[0] == '-' || str[0] == '.')) return 0;
     if(!seen_digit) return 0;

     // check if the previous character is not a delimiter
     if(str > beginning_of_line){
          const char* prev = str - 1;
          if(is_caps_var_char(*prev) || isalpha(*prev)) return 0;
     }

     return count;
}

void syntax_highlight(CeView_t* view, DrawColorList_t* draw_color_list){
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     CE_CLAMP(min, 0, (view->buffer->line_count - 1));
     CE_CLAMP(max, 0, (view->buffer->line_count - 1));
     int64_t match_len = 0;
     bool multiline_comment = false;

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          int64_t line_len = strlen(line);
          for(int64_t x = 0; x < line_len; ++x){
               char* str = line + x;

               if(multiline_comment){
                    if((match_len = match_c_multiline_comment_end(str))){
                         multiline_comment = false;
                    }
               }else{
                    if((match_len = match_c_keyword(str, line))){
                         draw_color_list_insert(draw_color_list, COLOR_BLUE, COLOR_DEFAULT, (CePoint_t){x, y});
                    }else if((match_len = match_c_type(str, line))){
                         draw_color_list_insert(draw_color_list, COLOR_BRIGHT_BLUE, COLOR_DEFAULT, (CePoint_t){x, y});
                    }else if((match_len = match_c_control(str))){
                         draw_color_list_insert(draw_color_list, COLOR_YELLOW, COLOR_DEFAULT, (CePoint_t){x, y});
                    }else if((match_len = match_caps_var(str))){
                         draw_color_list_insert(draw_color_list, COLOR_MAGENTA, COLOR_DEFAULT, (CePoint_t){x, y});
                    }else if((match_len = match_c_comment(str))){
                         draw_color_list_insert(draw_color_list, COLOR_GREEN, COLOR_DEFAULT, (CePoint_t){x, y});
                    }else if((match_len = match_c_string(str))){
                         draw_color_list_insert(draw_color_list, COLOR_RED, COLOR_DEFAULT, (CePoint_t){x, y});
                    }else if((match_len = match_c_character_literal(str))){
                         draw_color_list_insert(draw_color_list, COLOR_RED, COLOR_DEFAULT, (CePoint_t){x, y});
                    }else if((match_len = match_c_literal(str, line))){
                         draw_color_list_insert(draw_color_list, COLOR_MAGENTA, COLOR_DEFAULT, (CePoint_t){x, y});
                    }else if((match_len = match_c_preproc(str))){
                         draw_color_list_insert(draw_color_list, COLOR_BRIGHT_MAGENTA, COLOR_DEFAULT, (CePoint_t){x, y});
                    }else if((match_len = match_c_multiline_comment(str))){
                         draw_color_list_insert(draw_color_list, COLOR_GREEN, COLOR_DEFAULT, (CePoint_t){x, y});
                         multiline_comment = true;
                    }else if(!draw_color_list->tail || (draw_color_list->tail->fg != COLOR_DEFAULT || draw_color_list->tail->bg != COLOR_DEFAULT)){
                         draw_color_list_insert(draw_color_list, COLOR_DEFAULT, COLOR_DEFAULT, (CePoint_t){x, y});
                    }
               }

               if(match_len) x += (match_len - 1);
          }
     }
}

typedef struct{
     CeView_t* view;
     CeVim_t* vim;
     int64_t tab_width;
     CePoint_t scroll;
     volatile bool ready_to_draw;
     bool done;
}DrawThreadData_t;

void draw_view(CeView_t* view, int64_t tab_width, DrawColorList_t* draw_color_list, ColorDefs_t* color_defs){
     pthread_mutex_lock(&view->buffer->lock);

     int64_t view_height = view->rect.bottom - view->rect.top;
     int64_t view_width = view->rect.right - view->rect.left;
     int64_t row_min = view->scroll.y;
     int64_t col_min = view->scroll.x;
     int64_t col_max = col_min + view_width;

     char tab_str[tab_width + 1];
     memset(tab_str, ' ', tab_width);
     tab_str[tab_width] = 0;

     DrawColorNode_t* draw_color_node = draw_color_list->head;

     if(view->buffer->line_count > 0){
          move(0, 0);

          for(int64_t y = 0; y < view_height; y++){
               int64_t x = 0;
               int64_t rune_len = 0;
               int64_t line_index = y + row_min;
               CeRune_t rune = 1;

               move(view->rect.top + y, view->rect.left);

               if(line_index < view->buffer->line_count){
                    const char* line = view->buffer->lines[y + row_min];

                    while(rune > 0){
                         rune = ce_utf8_decode(line, &rune_len);

                         if(x >= col_min && x <= col_max && rune > 0){
                              if(draw_color_node && !ce_point_after(draw_color_node->point, (CePoint_t){x, y + view->scroll.y})){
                                   int change_color_pair = color_def_get(color_defs, draw_color_node->fg, draw_color_node->bg);
                                   attron(COLOR_PAIR(change_color_pair));
                                   draw_color_node = draw_color_node->next;
                              }

                              if(rune == CE_TAB){
                                   x += tab_width;
                                   addstr(tab_str);
                              }else if(rune >= 0x80){
                                   char utf8_string[CE_UTF8_SIZE + 1];
                                   int64_t bytes_written = 0;
                                   ce_utf8_encode(rune, utf8_string, CE_UTF8_SIZE, &bytes_written);
                                   utf8_string[bytes_written] = 0;
                                   addstr(utf8_string);
                                   x++;
                              }else{
                                   addch(rune);
                                   x++;
                              }
                         }else if(rune == CE_TAB){
                              x += tab_width;
                         }else{
                              x++;
                         }

                         line += rune_len;
                    }
               }

               for(; x < col_max; x++){
                    addch(' ');
               }
          }
     }

     mvprintw(view->rect.bottom, 0, "%ld, %ld", view->cursor.x, view->cursor.y);
     pthread_mutex_unlock(&view->buffer->lock);
}

void draw_view_status(CeView_t* view, CeVim_t* vim){
     const char* vim_mode_string = "UNKNOWN";

     switch(vim->mode){
     default:
          break;
     case CE_VIM_MODE_NORMAL:
          vim_mode_string = "N";
          break;
     case CE_VIM_MODE_INSERT:
          vim_mode_string = "I";
          break;
     case CE_VIM_MODE_VISUAL:
          vim_mode_string = "V";
          break;
     case CE_VIM_MODE_VISUAL_LINE:
          vim_mode_string = "VL";
          break;
     case CE_VIM_MODE_VISUAL_BLOCK:
          vim_mode_string = "VB";
          break;
     case CE_VIM_MODE_REPLACE:
          vim_mode_string = "R";
          break;
     }

     standend();
     int64_t width = (view->rect.right - view->rect.left) + 1;
     move(view->rect.bottom, view->rect.left);
     for(int64_t i = 0; i < width; ++i){
          addch(ACS_HLINE);
     }

     mvprintw(view->rect.bottom, view->rect.left + 1, " %s %s ", vim_mode_string, view->buffer->name);
}

void* draw_thread(void* thread_data){
     DrawThreadData_t* data = (DrawThreadData_t*)thread_data;
     struct timeval previous_draw_time;
     struct timeval current_draw_time;
     uint64_t time_since_last_draw = 0;
     DrawColorList_t draw_color_list = {};
     ColorDefs_t color_defs = {};

     while(!data->done){
          time_since_last_draw = 0;
          gettimeofday(&previous_draw_time, NULL);

          while(!data->ready_to_draw || time_since_last_draw < DRAW_USEC_LIMIT){
               gettimeofday(&current_draw_time, NULL);
               time_since_last_draw = (current_draw_time.tv_sec - previous_draw_time.tv_sec) * 1000000LL +
                                      (current_draw_time.tv_usec - previous_draw_time.tv_usec);
               sleep(0);
          }

          standend();
          draw_color_list_free(&draw_color_list);
          syntax_highlight(data->view, &draw_color_list);
          draw_view(data->view, data->tab_width, &draw_color_list, &color_defs);
          draw_view_status(data->view, data->vim);

          // move the visual cursor to the right location
          int64_t visible_cursor_x = 0;
          if(ce_buffer_point_is_valid(data->view->buffer, data->view->cursor)){
               visible_cursor_x = ce_util_string_index_to_visible_index(data->view->buffer->lines[data->view->cursor.y],
                                                                        data->view->cursor.x, data->tab_width);
          }
          move(data->view->cursor.y - data->view->scroll.y + data->view->rect.top,
               visible_cursor_x - data->view->scroll.x + data->view->rect.left);

          refresh();

          data->ready_to_draw = false;
     }

     return NULL;
}

int main(int argc, char** argv){
     setlocale(LC_ALL, "");

     if(!ce_log_init("ce.log")){
          return 1;
     }

     // init ncurses
     {
          initscr();
          keypad(stdscr, TRUE);
          raw();
          cbreak();
          noecho();

          if(has_colors() == FALSE){
               printf("Your terminal doesn't support colors. what year do you live in?\n");
               return 1;
          }

          start_color();
          use_default_colors();

          define_key("\x11", KEY_CLOSE);
          define_key("\x12", KEY_REDO);
     }

     CeConfigOptions_t config_options = {};
     config_options.tab_width = 8;
     config_options.horizontal_scroll_off = 10;
     config_options.vertical_scroll_off = 5;

     int terminal_width;
     int terminal_height;

     BufferNode_t* buffer_node_head = NULL;

     CeBuffer_t* buffer_list_buffer = calloc(1, sizeof(*buffer_list_buffer));
     ce_buffer_alloc(buffer_list_buffer, 1, "buffers");
     buffer_node_insert(&buffer_node_head, buffer_list_buffer);

     CeView_t view = {};
     view.scroll.x = 0;
     view.scroll.y = 0;

     if(argc > 1){
          for(int64_t i = 1; i < argc; i++){
               CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
               ce_buffer_load_file(buffer, argv[i]);
               buffer_node_insert(&buffer_node_head, buffer);
               view.buffer = buffer;

               // TODO: figure out type based on extention
               buffer->type = CE_BUFFER_FILE_TYPE_C;
          }
     }else{
          CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
          ce_buffer_alloc(buffer, 1, "unnamed");
     }

     CeVim_t vim = {};
     ce_vim_init(&vim);

     ce_vim_add_key_bind(&vim, 'S', &custom_vim_parse_verb_substitute);

     // init draw thread
     pthread_t thread_draw;
     DrawThreadData_t* draw_thread_data = calloc(1, sizeof(*draw_thread_data));
     {
          draw_thread_data->view = &view;
          draw_thread_data->vim = &vim;
          draw_thread_data->tab_width = config_options.tab_width;
          pthread_create(&thread_draw, NULL, draw_thread, draw_thread_data);
          draw_thread_data->ready_to_draw = true;
     }

     bool done = false;
     while(!done){
          getmaxyx(stdscr, terminal_height, terminal_width);
          view.rect.left = 0;
          view.rect.right = terminal_width - 1;
          view.rect.top = 0;
          view.rect.bottom = terminal_height - 1;

          int key = getch();
          switch(key){
          default:
               if(key == CE_NEWLINE && view.buffer == buffer_list_buffer){
                    BufferNode_t* itr = buffer_node_head;
                    int64_t index = 0;
                    while(itr){
                         if(index == view.cursor.y){
                              view_switch_buffer(&view, itr->buffer);
                              break;
                         }
                         itr = itr->next;
                         index++;
                    }
               }

               ce_vim_handle_key(&vim, &view, key, &config_options);
               break;
          case KEY_CLOSE:
               done = true;
               break;
          case 23: // Ctrl + w
               ce_buffer_save(view.buffer);
               break;
          case 2: // Ctrl + b
               build_buffer_list(buffer_list_buffer, buffer_node_head);
               view_switch_buffer(&view, buffer_list_buffer);
               break;
          }

          draw_thread_data->ready_to_draw = true;
     }

     draw_thread_data->done = true;
     pthread_cancel(thread_draw);
     pthread_join(thread_draw, NULL);

     // cleanup
     // TODO: free buffer_node_head
     ce_buffer_free(buffer_list_buffer);
     free(buffer_list_buffer);
     ce_vim_free(&vim);
     free(draw_thread_data);
     endwin();
     return 0;
}
