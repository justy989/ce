#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <ncurses.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>

#include "ce.h"
#include "ce_vim.h"
#include "ce_layout.h"
#include "ce_command.h"

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

void buffer_node_free(BufferNode_t** head){
     BufferNode_t* itr = *head;
     while(itr){
          BufferNode_t* tmp = itr;
          itr = itr->next;
          ce_buffer_free(tmp->buffer);
          free(tmp->buffer);
          free(tmp);
     }
     *head = NULL;
}

typedef enum{
     SYNTAX_COLOR_NORMAL,
     SYNTAX_COLOR_TYPE,
     SYNTAX_COLOR_KEYWORD,
     SYNTAX_COLOR_CONTROL,
     SYNTAX_COLOR_CAPS_VAR,
     SYNTAX_COLOR_COMMENT,
     SYNTAX_COLOR_STRING,
     SYNTAX_COLOR_CHAR_LITERAL,
     SYNTAX_COLOR_NUMBER_LITERAL,
     SYNTAX_COLOR_LITERAL,
     SYNTAX_COLOR_PREPROCESSOR,
     SYNTAX_COLOR_TRAILING_WHITESPACE,
     SYNTAX_COLOR_VISUAL,
     SYNTAX_COLOR_MATCH,
     SYNTAX_COLOR_CURRENT_LINE,
     SYNTAX_COLOR_COUNT,
}SyntaxColor_t;

#define SYNTAX_USE_CURRENT_COLOR -2

typedef struct{
     int fg;
     int bg;
}SyntaxDef_t;

int syntax_def_get_fg(SyntaxDef_t* syntax_defs, SyntaxColor_t syntax_color, int current_fg){
     int new_color = syntax_defs[syntax_color].fg;
     if(new_color == SYNTAX_USE_CURRENT_COLOR) return current_fg;
     return new_color;
}

int syntax_def_get_bg(SyntaxDef_t* syntax_defs, SyntaxColor_t syntax_color, int current_bg){
     int new_color = syntax_defs[syntax_color].bg;
     if(new_color == SYNTAX_USE_CURRENT_COLOR) return current_bg;
     return new_color;
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

int draw_color_list_last_fg_color(DrawColorList_t* draw_color_list){
     int fg = COLOR_DEFAULT;
     if(draw_color_list->tail) fg = draw_color_list->tail->fg;
     return fg;
}

int draw_color_list_last_bg_color(DrawColorList_t* draw_color_list){
     int bg = COLOR_DEFAULT;
     if(draw_color_list->tail) bg = draw_color_list->tail->bg;
     return bg;
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

static void build_buffer_list(CeBuffer_t* buffer, BufferNode_t* head){
     int64_t index = 1;
     char line[256];
     ce_buffer_empty(buffer);
     while(head){
          snprintf(line, 256, "%ld %s %ld", index, head->buffer->name, head->buffer->line_count);
          buffer_append_on_new_line(buffer, line);
          head = head->next;
          index++;
     }

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

static void build_yank_list(CeBuffer_t* buffer, CeVimYank_t* yanks){
     char line[256];
     ce_buffer_empty(buffer);
     for(int64_t i = 0; i < ASCII_PRINTABLE_CHARACTERS; i++){
          CeVimYank_t* yank = yanks + i;
          char reg = i + '!';
          if(yank->text == NULL) continue;
          snprintf(line, 256, "// register '%c': line: %s\n%s\n", reg, yank->line ? "true" : "false", yank->text);
          buffer_append_on_new_line(buffer, line);
     }

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

void view_switch_buffer(CeView_t* view, CeBuffer_t* buffer, CeVim_t* vim, CeConfigOptions_t* config_options){
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

static CeBuffer_t* load_new_file_into_view(BufferNode_t** buffer_node_head, CeView_t* view, CeConfigOptions_t* config_options, CeVim_t* vim, const char* filepath){
     // have we already loaded this file?
     BufferNode_t* itr = *buffer_node_head;
     while(itr){
          if(strcmp(itr->buffer->name, filepath) == 0){
               view_switch_buffer(view, itr->buffer, vim, config_options);
               return itr->buffer;
          }
          itr = itr->next;
     }

     // load file
     CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
     if(ce_buffer_load_file(buffer, filepath)){
          buffer_node_insert(buffer_node_head, buffer);
          view_switch_buffer(view, buffer, vim, config_options);

          // TODO: figure out type based on extention
          buffer->type = CE_BUFFER_FILE_TYPE_C;
     }else{
          free(buffer);
          return NULL;
     }

     return buffer;
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

static int64_t match_words(const char* str, const char* beginning_of_line, const char** words, int64_t word_count){
     for(int64_t i = 0; i < word_count; ++i){
          int64_t word_len = strlen(words[i]);
          if(strncmp(words[i], str, word_len) == 0){

               // make sure word isn't in the middle of an identifier
               char post_char = str[word_len];
               if(isalnum(post_char) || post_char == '_') return 0;
               if(str > beginning_of_line){
                    char pre_char = *(str - 1);
                    if(isalnum(pre_char) || pre_char == '_') return 0;
               }

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

     return match_words(str, beginning_of_line, keywords, keyword_count);
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

     return match_words(str, beginning_of_line, keywords, keyword_count);
}

int64_t match_c_control(const char* str, const char* beginning_of_line){
     static const char* keywords [] = {
          "break",
          "const",
          "continue",
          "goto",
          "return",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, beginning_of_line, keywords, keyword_count);
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
     if(strncmp("/*", str, 2) == 0){
          char* matching_comment = strstr(str, "*/");
          if(matching_comment) return (matching_comment - str);
          return ce_utf8_strlen(str);
     }

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

static int64_t match_trailing_whitespace(const char* str){
     const char* itr = str;
     while(*itr){
          if(!isspace(*itr)) return 0;
          itr++;
     }

     return (itr - str);
}

static void change_draw_color(DrawColorList_t* draw_color_list, SyntaxDef_t* syntax_defs, SyntaxColor_t syntax_color, CePoint_t point){
     int fg = syntax_def_get_fg(syntax_defs, syntax_color, draw_color_list_last_fg_color(draw_color_list));
     int bg = syntax_def_get_bg(syntax_defs, syntax_color, draw_color_list_last_bg_color(draw_color_list));
     draw_color_list_insert(draw_color_list, fg, bg, point);
}

void syntax_highlight(CeView_t* view, CeVim_t* vim, DrawColorList_t* draw_color_list, SyntaxDef_t* syntax_defs){
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     int64_t match_len = 0;
     bool multiline_comment = false;
     int bg_color = COLOR_DEFAULT; // TODO: eval bg_color to see if we can eliminate it since updating how visual mode is highlighted
     CePoint_t visual_start;
     CePoint_t visual_end;

     if(vim->mode == CE_VIM_MODE_VISUAL || vim->mode == CE_VIM_MODE_VISUAL_LINE){
          if(ce_point_after(view->cursor, vim->visual)){
               visual_start = vim->visual;
               visual_end = view->cursor;
          }else{
               visual_start = view->cursor;
               visual_end = vim->visual;
          }
     }

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          int64_t line_len = strlen(line);
          int64_t current_match_len = 1;
          CePoint_t match_point = {0, y};

          if(vim->mode == CE_VIM_MODE_VISUAL_LINE){
               if(match_point.y >= visual_start.y &&
                  match_point.y <= visual_end.y){
                    bg_color = COLOR_WHITE;
                    change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_VISUAL, match_point);
               }else{
                    if(bg_color == COLOR_WHITE){
                         bg_color = COLOR_DEFAULT;
                         draw_color_list_insert(draw_color_list, draw_color_list_last_fg_color(draw_color_list), bg_color, match_point);
                    }else{
                         bg_color = COLOR_DEFAULT;
                    }
               }
          }else if(vim->mode == CE_VIM_MODE_VISUAL){
               if(ce_points_equal(match_point, visual_start) ||
                  ce_points_equal(match_point, visual_end) ||
                  (ce_point_after(match_point, visual_start) &&
                   !ce_point_after(match_point, visual_end))){
                    change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_VISUAL, match_point);
               }
          }

          for(int64_t x = 0; x < line_len; ++x){
               char* str = line + x;
               match_point.x = x;

               if(vim->mode == CE_VIM_MODE_VISUAL){
                    if(ce_points_equal(match_point, visual_start) ||
                       ce_points_equal(match_point, visual_end) ||
                       (ce_point_after(match_point, visual_start) &&
                        !ce_point_after(match_point, visual_end))){
                         if(bg_color == COLOR_DEFAULT){
                              bg_color = COLOR_WHITE;
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_VISUAL, match_point);
                         }else{
                              bg_color = COLOR_WHITE;
                         }
                    }else{
                         if(bg_color == COLOR_WHITE){
                              bg_color = COLOR_DEFAULT;
                              draw_color_list_insert(draw_color_list, draw_color_list_last_fg_color(draw_color_list), bg_color, match_point);
                         }else{
                              bg_color = COLOR_DEFAULT;
                         }
                    }
               }

               if(current_match_len <= 1){
                    if(multiline_comment){
                         if((match_len = match_c_multiline_comment_end(str))){
                              multiline_comment = false;
                         }
                    }else{
                         if((match_len = match_c_type(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_TYPE, match_point);
                         }else if((match_len = match_c_keyword(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_KEYWORD, match_point);
                         }else if((match_len = match_c_control(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_CONTROL, match_point);
                         }else if((match_len = match_caps_var(str))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_CAPS_VAR, match_point);
                         }else if((match_len = match_c_comment(str))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_COMMENT, match_point);
                         }else if((match_len = match_c_string(str))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_STRING, match_point);
                         }else if((match_len = match_c_character_literal(str))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_CHAR_LITERAL, match_point);
                         }else if((match_len = match_c_literal(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_NUMBER_LITERAL, match_point);
                         }else if((match_len = match_c_preproc(str))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_PREPROCESSOR, match_point);
                         }else if((match_len = match_c_multiline_comment(str))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_COMMENT, match_point);
                              multiline_comment = true;
                         }else if(((view->cursor.y != y) || (x >= view->cursor.x)) && (match_len = match_trailing_whitespace(str))){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_TRAILING_WHITESPACE, match_point);
                              draw_color_list_insert(draw_color_list, syntax_def_get_fg(syntax_defs, SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                     syntax_def_get_bg(syntax_defs, SYNTAX_COLOR_NORMAL, COLOR_DEFAULT), (CePoint_t){0, match_point.y + 1});
                         }else if(!draw_color_list->tail || (draw_color_list->tail->fg != COLOR_DEFAULT || draw_color_list->tail->bg != bg_color)){
                              change_draw_color(draw_color_list, syntax_defs, SYNTAX_COLOR_NORMAL, match_point);
                         }
                    }

                    if(match_len) current_match_len = match_len;
               }else{
                    current_match_len--;
               }
          }

          // handle case where visual mode ends at the end of the line
          match_point.x = line_len;
          if(vim->mode == CE_VIM_MODE_VISUAL && bg_color == COLOR_WHITE && ce_point_after(match_point, visual_end)){
               bg_color = COLOR_DEFAULT;
               draw_color_list_insert(draw_color_list, draw_color_list_last_fg_color(draw_color_list), bg_color, match_point);
          }
     }
}

typedef struct{
     CeLayout_t* layout;
     CeVim_t* vim;
     int64_t tab_width;
     SyntaxDef_t* syntax_defs;
     volatile bool ready_to_draw;
     volatile bool* input_mode;
     CeView_t* input_view;
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

     standend();
     if(view->buffer->line_count > 0){
          move(0, 0);

          for(int64_t y = 0; y < view_height; y++){
               int64_t index = 0;
               int64_t x = 0;
               int64_t rune_len = 0;
               int64_t line_index = y + row_min;
               CeRune_t rune = 1;

               move(view->rect.top + y, view->rect.left);

               if(line_index < view->buffer->line_count){
                    const char* line = view->buffer->lines[y + row_min];

                    while(rune > 0){
                         rune = ce_utf8_decode(line, &rune_len);

                         // check if we need to move to the next color
                         while(draw_color_node && !ce_point_after(draw_color_node->point, (CePoint_t){index, y + view->scroll.y})){
                              int change_color_pair = color_def_get(color_defs, draw_color_node->fg, draw_color_node->bg);
                              attron(COLOR_PAIR(change_color_pair));
                              draw_color_node = draw_color_node->next;
                         }

                         if(x >= col_min && x <= col_max && rune > 0){
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
                         index++;
                    }
               }

               standend();
               for(; x <= col_max; x++){
                    addch(' ');
               }
          }
     }

     pthread_mutex_unlock(&view->buffer->lock);
}

void draw_view_status(CeView_t* view, CeVim_t* vim, ColorDefs_t* color_defs, int64_t height_offset){
     // create bottom bar bg
     int64_t bottom = view->rect.bottom + height_offset;
     int color_pair = color_def_get(color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
     attron(COLOR_PAIR(color_pair));
     int64_t width = (view->rect.right - view->rect.left) + 1;
     move(bottom, view->rect.left);
     for(int64_t i = 0; i < width; ++i){
          addch(' ');
     }

     // set the mode line
     int vim_mode_fg = COLOR_DEFAULT;
     const char* vim_mode_string = NULL;

     if(vim){
          switch(vim->mode){
          default:
               break;
          case CE_VIM_MODE_NORMAL:
               vim_mode_string = "N";
               vim_mode_fg = COLOR_BLUE;
               break;
          case CE_VIM_MODE_INSERT:
               vim_mode_string = "I";
               vim_mode_fg = COLOR_GREEN;
               break;
          case CE_VIM_MODE_VISUAL:
               vim_mode_string = "V";
               vim_mode_fg = COLOR_YELLOW;
               break;
          case CE_VIM_MODE_VISUAL_LINE:
               vim_mode_string = "VL";
               vim_mode_fg = COLOR_BRIGHT_YELLOW;
               break;
          case CE_VIM_MODE_VISUAL_BLOCK:
               vim_mode_string = "VB";
               vim_mode_fg = COLOR_BRIGHT_YELLOW;
               break;
          case CE_VIM_MODE_REPLACE:
               vim_mode_string = "R";
               vim_mode_fg = COLOR_RED;
               break;
          }
     }

     if(vim_mode_string){
          color_pair = color_def_get(color_defs, vim_mode_fg, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          mvprintw(bottom, view->rect.left + 1, "%s", vim_mode_string);

          color_pair = color_def_get(color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          printw(" %s", view->buffer->name);
     }else{
          color_pair = color_def_get(color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          mvprintw(bottom, view->rect.left + 1, "%s", view->buffer->name);
     }

     if(view->buffer->status == CE_BUFFER_STATUS_MODIFIED ||
        view->buffer->status == CE_BUFFER_STATUS_NEW_FILE){
          addch('*');
     }else if(view->buffer->status == CE_BUFFER_STATUS_READONLY){
          printw("[RO]");
     }

     char cursor_pos_string[32];
     int64_t cursor_pos_string_len = snprintf(cursor_pos_string, 32, "%ld, %ld", view->cursor.x, view->cursor.y);
     mvprintw(bottom, view->rect.right - (cursor_pos_string_len + 1), "%s", cursor_pos_string);
}

void draw_layout(CeLayout_t* layout, CeVim_t* vim, ColorDefs_t* color_defs, int64_t tab_width, CeLayout_t* current,
                 SyntaxDef_t* syntax_defs){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
     {
          DrawColorList_t draw_color_list = {};
          syntax_highlight(&layout->view, vim, &draw_color_list, syntax_defs);
          draw_view(&layout->view, tab_width, &draw_color_list, color_defs);
          draw_color_list_free(&draw_color_list);
          draw_view_status(&layout->view, layout == current ? vim : NULL, color_defs, 0);
          int64_t rect_height = layout->view.rect.bottom - layout->view.rect.top;
          int color_pair = color_def_get(color_defs, COLOR_BRIGHT_BLACK, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          for(int i = 0; i < rect_height; i++){
               mvaddch(layout->view.rect.top + i, layout->view.rect.right, ACS_VLINE);
          }
     } break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               draw_layout(layout->list.layouts[i], vim, color_defs, tab_width, current, syntax_defs);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          draw_layout(layout->tab.root, vim, color_defs, tab_width, current, syntax_defs);
          break;
     }
}

static CePoint_t view_cursor_on_screen(CeView_t* view, int64_t tab_width){
     // move the visual cursor to the right location
     int64_t visible_cursor_x = 0;
     if(ce_buffer_point_is_valid(view->buffer, view->cursor)){
          visible_cursor_x = ce_util_string_index_to_visible_index(view->buffer->lines[view->cursor.y],
                                                                   view->cursor.x, tab_width);
     }

     return (CePoint_t){visible_cursor_x - view->scroll.x + view->rect.left,
                        view->cursor.y - view->scroll.y + view->rect.top};
}

void* draw_thread(void* thread_data){
     DrawThreadData_t* data = (DrawThreadData_t*)thread_data;
     struct timeval previous_draw_time;
     struct timeval current_draw_time;
     uint64_t time_since_last_draw = 0;
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

          CeLayout_t* tab_layout = data->layout->tab_list.current;

          // draw a tab bar if there is more than 1 tab
          if(data->layout->tab_list.tab_count > 1){
               move(0, 0);
               int color_pair = color_def_get(&color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
               attron(COLOR_PAIR(color_pair));
               for(int64_t i = data->layout->tab_list.rect.left; i <= data->layout->tab_list.rect.right; i++){
                    addch(' ');
               }

               move(0, 0);

               for(int64_t i = 0; i < data->layout->tab_list.tab_count; i++){
                    if(data->layout->tab_list.tabs[i] == data->layout->tab_list.current){
                         color_pair = color_def_get(&color_defs, COLOR_BRIGHT_WHITE, COLOR_DEFAULT);
                         attron(COLOR_PAIR(color_pair));
                    }else{
                         color_pair = color_def_get(&color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
                         attron(COLOR_PAIR(color_pair));
                    }

                    if(data->layout->tab_list.tabs[i]->tab.current->type == CE_LAYOUT_TYPE_VIEW){
                         const char* buffer_name = data->layout->tab_list.tabs[i]->tab.current->view.buffer->name;

                         printw(" %s ", buffer_name);
                    }else{
                         printw(" selection ");
                    }
               }
          }

          standend();
          draw_layout(tab_layout, data->vim, &color_defs, data->tab_width, tab_layout->tab.current, data->syntax_defs);

          if(*data->input_mode){
               DrawColorList_t draw_color_list = {};
               syntax_highlight(data->input_view, data->vim, &draw_color_list, data->syntax_defs);
               draw_view(data->input_view, data->tab_width, &draw_color_list, &color_defs);
               draw_color_list_free(&draw_color_list);
               int64_t new_status_bar_offset = (data->input_view->rect.bottom - data->input_view->rect.top) + 1;
               draw_view_status(data->input_view, data->vim, &color_defs, 0);
               draw_view_status(&tab_layout->tab.current->view, NULL, &color_defs, -new_status_bar_offset);
          }

          // show border when non view is selected
          if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW){
               int64_t rect_height = 0;
               int64_t rect_width = 0;
               CeRect_t* rect = NULL;
               switch(tab_layout->tab.current->type){
               default:
                    break;
               case CE_LAYOUT_TYPE_LIST:
                    rect = &tab_layout->tab.current->list.rect;
                    rect_width = rect->right - rect->left;
                    rect_height = rect->bottom - rect->top;
                    break;
               case CE_LAYOUT_TYPE_TAB:
                    rect = &tab_layout->tab.current->tab.rect;
                    rect_width = rect->right - rect->left;
                    rect_height = rect->bottom - rect->top;
                    break;
               }

               int color_pair = color_def_get(&color_defs, COLOR_BRIGHT_WHITE, COLOR_DEFAULT);
               attron(COLOR_PAIR(color_pair));
               for(int i = 1; i < rect_height - 1; i++){
                    mvaddch(rect->top + i, rect->right, ACS_VLINE);
                    mvaddch(rect->top + i, rect->left, ACS_VLINE);
               }

               for(int i = 1; i < rect_width - 1; i++){
                    mvaddch(rect->top, rect->left + i, ACS_HLINE);
                    mvaddch(rect->bottom, rect->left + i, ACS_HLINE);
               }

               move(0, 0);
          }else if(*data->input_mode){
               CePoint_t screen_cursor = view_cursor_on_screen(data->input_view, data->tab_width);
               move(screen_cursor.y, screen_cursor.x);
          }else{
               CeView_t* view = &tab_layout->tab.current->view;
               CePoint_t screen_cursor = view_cursor_on_screen(view, data->tab_width);
               move(screen_cursor.y, screen_cursor.x);
          }

          refresh();

          data->ready_to_draw = false;
     }

     return NULL;
}

void input_view_overlay(CeView_t* input_view, CeView_t* view){
     input_view->rect.left = view->rect.left;
     input_view->rect.right = view->rect.right;
     input_view->rect.bottom = view->rect.bottom;
     int64_t max_height = (view->rect.bottom - view->rect.top) - 1;
     int64_t height = input_view->buffer->line_count;
     if(height <= 0) height = 1;
     if(height > max_height) height = max_height;
     input_view->rect.top = view->rect.bottom - height;

}

bool enable_input_mode(CeView_t* input_view, CeView_t* view, CeVim_t* vim, const char* dialogue){
     // update input view to overlay the current view
     input_view_overlay(input_view, view);

     // update name based on dialog
     bool success = ce_buffer_alloc(input_view->buffer, 1, dialogue);
     input_view->cursor = (CePoint_t){0, 0};
     vim->mode = CE_VIM_MODE_INSERT;

     return success;
}

typedef struct{
     int* keys;
     int64_t key_count;
     CeCommand_t command;
     CeVimMode_t vim_mode;
}KeyBind_t;

typedef struct{
     KeyBind_t* binds;
     int64_t count;
}KeyBinds_t;

typedef struct{
     int keys[4];
     const char* command;
}KeyBindDef_t;

static void convert_bind_defs(KeyBinds_t* binds, KeyBindDef_t* bind_defs, int64_t bind_def_count)
{
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

#define APP_MAX_KEY_COUNT 16

typedef struct{
     CeRect_t terminal_rect;
     CeVim_t vim;
     CeConfigOptions_t config_options;
     int terminal_width;
     int terminal_height;
     CeView_t input_view;
     bool input_mode;
     CeLayout_t* tab_list_layout;
     SyntaxDef_t* syntax_defs;
     BufferNode_t* buffer_node_head;
     CeCommandEntry_t* command_entries;
     int64_t command_entry_count;
     CeVimParseResult_t last_vim_handle_result;
     CeBuffer_t* buffer_list_buffer;
     CeBuffer_t* yank_list_buffer;
     KeyBinds_t key_binds[CE_VIM_MODE_COUNT];
     CeRune_t keys[APP_MAX_KEY_COUNT];
     int64_t key_count;
     bool quit;
}App_t;

CeCommandStatus_t command_quit(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     app->quit = true;
     return CE_COMMAND_SUCCESS;
}

// TODO: is this useful or did I pre-maturely create this
static bool get_view_info_from_tab(CeLayout_t* tab_layout, CeView_t** view, CeRect_t* view_rect){
     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          *view = &tab_layout->tab.current->view;
     }else if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_LIST){
          *view_rect = tab_layout->list.rect;
     }else if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_TAB){
          *view_rect = tab_layout->tab.rect;
     }else{
          return false;
     }

     return true;
}

CeCommandStatus_t command_select_adjacent_layout(CeCommand_t* command, void* user_data)
{
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CePoint_t target;
     CeView_t* view = NULL;
     CeRect_t view_rect = {};
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(!get_view_info_from_tab(tab_layout, &view, &view_rect)){
          assert(!"unknown layout type");
          return CE_COMMAND_FAILURE;
     }

     if(strcmp(command->args[0].string, "up") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width);
               target = (CePoint_t){screen_cursor.x, view->rect.top - 1};
          }else{
               target = (CePoint_t){view_rect.left, view_rect.top - 1};
          }
     }else if(strcmp(command->args[0].string, "down") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width);
               target = (CePoint_t){screen_cursor.x, view->rect.bottom + 1};
          }else{
               target = (CePoint_t){view_rect.left, view_rect.bottom + 1};
          }
     }else if(strcmp(command->args[0].string, "left") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width);
               target = (CePoint_t){view->rect.left - 1, screen_cursor.y};
          }else{
               target = (CePoint_t){view_rect.left - 1, view_rect.top};
          }
     }else if(strcmp(command->args[0].string, "right") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width);
               target = (CePoint_t){view->rect.right + 1, screen_cursor.y};
          }else{
               target = (CePoint_t){view_rect.right + 1, view_rect.top};
          }
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0].string);
          return CE_COMMAND_PRINT_HELP;
     }

     target.x %= app->terminal_width;
     target.y %= app->terminal_height;
     CeLayout_t* layout = ce_layout_find_at(tab_layout, target);
     if(layout){
          tab_layout->tab.current = layout;
          app->vim.mode = CE_VIM_MODE_NORMAL;
          app->input_mode = false;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_save_buffer(CeCommand_t* command, void* user_data)
{
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
          ce_buffer_save(view->buffer);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_show_buffers(CeCommand_t* command, void* user_data)
{
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
          view_switch_buffer(view, app->buffer_list_buffer, &app->vim, &app->config_options);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_show_yanks(CeCommand_t* command, void* user_data)
{
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
          view_switch_buffer(view, app->yank_list_buffer, &app->vim, &app->config_options);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_split_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     bool vertical = false;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }

     if(strcmp(command->args[0].string, "vertical") == 0){
          vertical = true;
     }else if(strcmp(command->args[0].string, "horizontal") == 0){
          // pass
     }else{
          ce_log("unrecognized argument '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     if(view) pthread_mutex_lock(&view->buffer->lock);
     ce_layout_split(tab_layout, vertical);
     if(view){
          pthread_mutex_unlock(&view->buffer->lock);
          view = &tab_layout->tab.current->view;

          ce_layout_distribute_rect(tab_layout, app->terminal_rect);

          ce_view_follow_cursor(view, app->config_options.horizontal_scroll_off, app->config_options.vertical_scroll_off,
                                app->config_options.tab_width);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_select_parent_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     CeLayout_t* layout = ce_layout_find_parent(tab_layout, tab_layout->tab.current);
     if(layout) tab_layout->tab.current = layout;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_delete_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeRect_t view_rect = {};
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     // check if this is the only view, and ignore the delete request
     if(app->tab_list_layout->tab_list.tab_count == 1 &&
        tab_layout->tab.root->type == CE_LAYOUT_TYPE_LIST &&
        tab_layout->tab.root->list.layout_count == 1 &&
        tab_layout->tab.current == tab_layout->tab.root->list.layouts[0]){
          return CE_COMMAND_NO_ACTION;
     }

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(!get_view_info_from_tab(tab_layout, &view, &view_rect)){
          assert(!"unknown layout type");
          return CE_COMMAND_FAILURE;
     }

     CePoint_t cursor = {view_rect.left, view_rect.top};
     if(view) cursor = view->cursor;
     ce_layout_delete(tab_layout, tab_layout->tab.current);
     ce_layout_distribute_rect(tab_layout, app->terminal_rect);
     CeLayout_t* layout = ce_layout_find_at(tab_layout, cursor);
     if(layout) tab_layout->tab.current = layout;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_load_file(CeCommand_t* command, void* user_data){
     if(command->arg_count < 0 || command->arg_count > 1) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     if(command->arg_count == 1){
          if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
          load_new_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, command->args[0].string);
     }else{ // it's 0
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "LOAD FILE");
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_new_tab(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;

     CeLayout_t* new_tab_layout = ce_layout_tab_list_add(app->tab_list_layout);
     if(!new_tab_layout) return CE_COMMAND_NO_ACTION;
     app->tab_list_layout->tab_list.current = new_tab_layout;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_select_adjacent_tab(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;

     if(strcmp(command->args[0].string, "left") == 0){
          for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
               if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
                    if(i > 0){
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i - 1];
                         return CE_COMMAND_SUCCESS;
                    }else{
                         // wrap around
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[app->tab_list_layout->tab_list.tab_count - 1];
                         return CE_COMMAND_SUCCESS;
                    }
                    break;
               }
          }
     }else if(strcmp(command->args[0].string, "right") == 0){
          for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
               if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
                    if(i < (app->tab_list_layout->tab_list.tab_count - 1)){
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i + 1];
                         return CE_COMMAND_SUCCESS;
                    }else{
                         // wrap around
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[0];
                         return CE_COMMAND_SUCCESS;
                    }
                    break;
               }
          }
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_NO_ACTION;
}

CeCommandStatus_t command_search(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     if(strcmp(command->args[0].string, "forward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_FORWARD;
     }else if(strcmp(command->args[0].string, "backward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REVERSE SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_BACKWARD;
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_regex_search(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     if(strcmp(command->args[0].string, "forward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REGEX SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_REGEX_FORWARD;
     }else if(strcmp(command->args[0].string, "backward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REGEX REVERSE SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_REGEX_BACKWARD;
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

// lol
CeCommandStatus_t command_command(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "COMMAND");
     return CE_COMMAND_SUCCESS;
}

static int int_strneq(int* a, int* b, size_t len)
{
     for(size_t i = 0; i < len; ++i){
          if(!*a) return false;
          if(!*b) return false;
          if(*a != *b) return false;
          a++;
          b++;
     }

     return true;
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

          define_key("\x12", KEY_REDO);
          define_key(NULL, KEY_ENTER);       // Blow away enter
          define_key("\x0D", KEY_ENTER);     // Enter       (13) (0x0D) ASCII "CR"  NL Carriage Return
     }

     // TODO: allocate this on the heap when/if it gets too big?
     App_t app = {};

     // init custon syntax highlighting
     SyntaxDef_t syntax_defs[SYNTAX_COLOR_COUNT];
     {
          syntax_defs[SYNTAX_COLOR_NORMAL].fg = COLOR_DEFAULT;
          syntax_defs[SYNTAX_COLOR_NORMAL].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_TYPE].fg = COLOR_BRIGHT_BLUE;
          syntax_defs[SYNTAX_COLOR_TYPE].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_KEYWORD].fg = COLOR_BLUE;
          syntax_defs[SYNTAX_COLOR_KEYWORD].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_CONTROL].fg = COLOR_YELLOW;
          syntax_defs[SYNTAX_COLOR_CONTROL].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_CAPS_VAR].fg = COLOR_MAGENTA;
          syntax_defs[SYNTAX_COLOR_CAPS_VAR].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_COMMENT].fg = COLOR_GREEN;
          syntax_defs[SYNTAX_COLOR_COMMENT].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_STRING].fg = COLOR_RED;
          syntax_defs[SYNTAX_COLOR_STRING].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_CHAR_LITERAL].fg = COLOR_RED;
          syntax_defs[SYNTAX_COLOR_CHAR_LITERAL].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_NUMBER_LITERAL].fg = COLOR_MAGENTA;
          syntax_defs[SYNTAX_COLOR_NUMBER_LITERAL].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_PREPROCESSOR].fg = COLOR_BRIGHT_MAGENTA;
          syntax_defs[SYNTAX_COLOR_PREPROCESSOR].bg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_TRAILING_WHITESPACE].fg = COLOR_RED;
          syntax_defs[SYNTAX_COLOR_TRAILING_WHITESPACE].bg = COLOR_RED;
          syntax_defs[SYNTAX_COLOR_VISUAL].fg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_VISUAL].bg = COLOR_WHITE;
          syntax_defs[SYNTAX_COLOR_MATCH].fg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_MATCH].bg = COLOR_WHITE;
          syntax_defs[SYNTAX_COLOR_CURRENT_LINE].fg = SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[SYNTAX_COLOR_CURRENT_LINE].bg = COLOR_BRIGHT_BLACK;

          app.syntax_defs = syntax_defs;
     }

     // init config options
     {
          app.config_options.tab_width = 5;
          app.config_options.horizontal_scroll_off = 10;
          app.config_options.vertical_scroll_off = 5;
     }

     // init commands
     CeCommandEntry_t command_entries[] = {
          {command_quit, "quit", "quit ce"},
          {command_select_adjacent_layout, "select_adjacent_layout", "select 'left', 'right', 'up' or 'down adjacent layouts"},
          {command_save_buffer, "save_buffer", "save the currently selected view's buffer"},
          {command_show_buffers, "show_buffers", "show the list of buffers"},
          {command_show_yanks, "show_yanks", "show the state of your vim yanks"},
          {command_split_layout, "split_layout", "split the current layout 'horizontal' or 'vertical' into 2 layouts"},
          {command_select_parent_layout, "select_parent_layout", "select the parent of the current layout"},
          {command_delete_layout, "delete_layout", "delete the current layout (unless it's the only one left)"},
          {command_load_file, "load_file", "load a file (optionally specified)"},
          {command_new_tab, "new_tab", "create a new tab"},
          {command_select_adjacent_tab, "select_adjacent_tab", "selects either the 'left' or 'right' tab"},
          {command_search, "search", "interactive search 'forward' or 'backward'"},
          {command_regex_search, "regex_search", "interactive regex search 'forward' or 'backward'"},
          {command_command, "command", "interactively send a commmand"},
     };
     app.command_entries = command_entries;
     app.command_entry_count = sizeof(command_entries) / sizeof(command_entries[0]);

     app.buffer_list_buffer = calloc(1, sizeof(*app.buffer_list_buffer));
     app.yank_list_buffer = calloc(1, sizeof(*app.yank_list_buffer));

     // init buffers
     {
          ce_buffer_alloc(app.buffer_list_buffer, 1, "buffers");
          buffer_node_insert(&app.buffer_node_head, app.buffer_list_buffer);
          ce_buffer_alloc(app.yank_list_buffer, 1, "yanks");
          buffer_node_insert(&app.buffer_node_head, app.yank_list_buffer);

          if(argc > 1){
               for(int64_t i = 1; i < argc; i++){
                    CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
                    if(ce_buffer_load_file(buffer, argv[i])){
                         buffer_node_insert(&app.buffer_node_head, buffer);

                         // TODO: figure out type based on extention
                         buffer->type = CE_BUFFER_FILE_TYPE_C;
                    }else{
                         free(buffer);
                    }
               }
          }else{
               CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
               ce_buffer_alloc(buffer, 1, "unnamed");
          }
     }

     // init keybinds
     {
          KeyBindDef_t normal_mode_bind_defs[] = {
               {{'\\', 'q'}, "quit"},
               {{8}, "select_adjacent_layout left"}, // ctrl h
               {{12}, "select_adjacent_layout right"}, // ctrl l
               {{11}, "select_adjacent_layout up"}, // ctrl k
               {{10}, "select_adjacent_layout down"}, // ctrl j
               {{23}, "save_buffer"}, // ctrl w
               {{2}, "show_buffers"}, // ctrl b
               {{22}, "split_layout horizontal"}, // ctrl s
               {{19}, "split_layout vertical"}, // ctrl v
               {{16}, "select_parent_layout"}, // ctrl p
               {{1}, "delete_layout"}, // ctrl a
               {{6}, "load_file"}, // ctrl f
               {{20}, "new_tab"}, // ctrl t
               {{'/'}, "search forward"},
               {{'?'}, "search backward"},
               {{':'}, "command"},
               {{'g', 't'}, "select_adjacent_tab right"},
               {{'g', 'T'}, "select_adjacent_tab left"},
               {{'\\', '/'}, "regex_search forward"},
               {{'\\', '?'}, "regex_search backward"},
               {{'"', '?'}, "show_yanks"},
          };

          convert_bind_defs(&app.key_binds[CE_VIM_MODE_NORMAL], normal_mode_bind_defs, sizeof(normal_mode_bind_defs) / sizeof(normal_mode_bind_defs[0]));
     }

     // init vim
     {
          ce_vim_init(&app.vim);

          // override 'S' key
          for(int64_t i = 0; i < app.vim.key_bind_count; ++i){
               CeVimKeyBind_t* key_bind = app.vim.key_binds + i;
               if(key_bind->key == 'S'){
                    key_bind->function = &custom_vim_parse_verb_substitute;
                    break;
               }
          }
     }

     // init layout
     {
          CeLayout_t* tab_layout = ce_layout_tab_init(app.buffer_node_head->buffer);
          app.tab_list_layout = ce_layout_tab_list_init(tab_layout);
     }

     // setup input buffer
     {
          CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
          ce_buffer_alloc(buffer, 1, "input");
          app.input_view.buffer = buffer;
          buffer_node_insert(&app.buffer_node_head, buffer);
     }

     // init draw thread
     pthread_t thread_draw;
     DrawThreadData_t* draw_thread_data = calloc(1, sizeof(*draw_thread_data));
     {
          draw_thread_data->layout = app.tab_list_layout;
          draw_thread_data->vim = &app.vim;
          draw_thread_data->tab_width = app.config_options.tab_width;
          draw_thread_data->input_mode = &app.input_mode;
          draw_thread_data->input_view = &app.input_view;
          draw_thread_data->syntax_defs = syntax_defs;
          pthread_create(&thread_draw, NULL, draw_thread, draw_thread_data);
          draw_thread_data->ready_to_draw = true;
     }

     // main loop
     while(!app.quit){
          // handle if terminal was resized
          // TODO: we can optimize by only doing this at the start and when we see a resized event
          getmaxyx(stdscr, app.terminal_height, app.terminal_width);
          app.terminal_rect = (CeRect_t){0, app.terminal_width - 1, 0, app.terminal_height - 1};
          ce_layout_distribute_rect(app.tab_list_layout, app.terminal_rect);

          // figure out our current view rect
          CeView_t* view = NULL;
          CeLayout_t* tab_layout = app.tab_list_layout->tab_list.current;

          switch(tab_layout->tab.current->type){
          default:
               break;
          case CE_LAYOUT_TYPE_VIEW:
               view = &tab_layout->tab.current->view;
               break;
          }

          // setup input view rect
          if(app.input_mode && view) input_view_overlay(&app.input_view, view);

          // wait for input from the user
          int key = getch();
          bool handled_key = false;

          // as long as vim isn't in the middle of handling keys, in insert mode vim returns VKH_HANDLED_KEY TODO: is that what we want?
          if(app.last_vim_handle_result != CE_VIM_PARSE_IN_PROGRESS &&
             app.last_vim_handle_result != CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY &&
             app.vim.mode != CE_VIM_MODE_INSERT){
               // append to keys
               if(app.key_count < APP_MAX_KEY_COUNT){
                    app.keys[app.key_count] = key;
                    app.key_count++;

                    bool no_matches = true;
                    for(int64_t i = 0; i < app.key_binds[app.vim.mode].count; ++i){
                         if(int_strneq(app.key_binds[app.vim.mode].binds[i].keys, app.keys, app.key_count)){
                              no_matches = false;
                              // if we have matches, but don't completely match, then wait for more keypresses,
                              // otherwise, execute the action
                              if(app.key_binds[app.vim.mode].binds[i].key_count == app.key_count){
                                   CeCommand_t* command = &app.key_binds[app.vim.mode].binds[i].command;
                                   CeCommandFunc_t* command_func = NULL;
                                   CeCommandEntry_t* entry = NULL;
                                   for(int64_t c = 0; c < app.command_entry_count; ++c){
                                        entry = app.command_entries + c;
                                        if(strcmp(entry->name, command->name) == 0){
                                             command_func = entry->func;
                                             break;
                                        }
                                   }

                                   if(command_func){
                                        CeCommandStatus_t cs = command_func(command, &app);

                                        switch(cs){
                                        default:
                                             handled_key = true;
                                             break;
                                        case CE_COMMAND_NO_ACTION:
                                             break;
                                        case CE_COMMAND_FAILURE:
                                             ce_log("'%s' failed", entry->name);
                                             break;
                                        case CE_COMMAND_PRINT_HELP:
                                             ce_log("command help:\n'%s' %s\n", entry->name, entry->description);
                                             break;
                                        }
                                   }else{
                                        ce_log("unknown command: '%s'", command->name);
                                   }

                                   app.key_count = 0;
                              }else{
                                   handled_key = true;
                              }
                         }
                    }

                    if(no_matches){
                         app.vim.current_command[0] = 0;
                         for(int64_t i = 0; i < app.key_count - 1; ++i){
                              ce_vim_append_key(&app.vim, key);
                         }

                         app.key_count = 0;
                    }
               }
          }

          if(!handled_key && view){
               if(key == KEY_ENTER){
                    if(view->buffer == app.buffer_list_buffer){
                         BufferNode_t* itr = app.buffer_node_head;
                         int64_t index = 0;
                         while(itr){
                              if(index == view->cursor.y){
                                   view_switch_buffer(view, itr->buffer, &app.vim, &app.config_options);
                                   break;
                              }
                              itr = itr->next;
                              index++;
                         }
                    }else if(app.input_mode){
                         if(strcmp(app.input_view.buffer->name, "LOAD FILE") == 0){
                              for(int64_t i = 0; i < app.input_view.buffer->line_count; i++){
                                   load_new_file_into_view(&app.buffer_node_head, view, &app.config_options, &app.vim, app.input_view.buffer->lines[i]);
                              }
                         }else if(strcmp(app.input_view.buffer->name, "SEARCH") == 0 ||
                                  strcmp(app.input_view.buffer->name, "REVERSE SEARCH") == 0 ||
                                  strcmp(app.input_view.buffer->name, "REGEX SEARCH") == 0 ||
                                  strcmp(app.input_view.buffer->name, "REGEX REVERSE SEARCH") == 0){
                              // update yanks
                              int64_t index = ce_vim_yank_register_index('/');
                              CeVimYank_t* yank = app.vim.yanks + index;
                              free(yank->text);
                              yank->text = strdup(app.input_view.buffer->lines[0]);
                              yank->line = false;
                         }else if(strcmp(app.input_view.buffer->name, "COMMAND") == 0){
                              CeCommand_t command = {};
                              if(!ce_command_parse(&command, app.input_view.buffer->lines[0])){
                                   ce_log("failed to parse command: '%s'\n", app.input_view.buffer->lines[0]);
                              }else{
                                   CeCommandFunc_t* command_func = NULL;
                                   for(int64_t i = 0; i < app.command_entry_count; i++){
                                        CeCommandEntry_t* entry = app.command_entries + i;
                                        if(strcmp(entry->name, command.name) == 0){
                                             command_func = entry->func;
                                             break;
                                        }
                                   }

                                   if(command_func){
                                        command_func(&command, &app);
                                   }else{
                                        ce_log("unknown command: '%s'\n", command.name);
                                   }
                              }
                         }

                         // TODO: compress this, we do it a lot, and I'm sure there will be more we need to do in the future
                         app.input_mode = false;
                         app.vim.mode = CE_VIM_MODE_NORMAL;
                    }else{
                         key = CE_NEWLINE;
                    }
               }else if(key == 27 && app.input_mode){ // Escape
                    app.input_mode = false;
                    app.vim.mode = CE_VIM_MODE_NORMAL;
               }

               if(app.input_mode){
                    app.last_vim_handle_result = ce_vim_handle_key(&app.vim, &app.input_view, key, &app.config_options);
               }else{
                    app.last_vim_handle_result = ce_vim_handle_key(&app.vim, view, key, &app.config_options);
               }
          }

          // incremental search
          if(app.input_mode){
               if(strcmp(app.input_view.buffer->name, "SEARCH") == 0){
                    if(app.input_view.buffer->line_count && view->buffer->line_count && strlen(app.input_view.buffer->lines[0])){
                         CePoint_t match_point = ce_buffer_search_forward(view->buffer, view->cursor, app.input_view.buffer->lines[0]);
                         if(match_point.x >= 0){
                              view->cursor = match_point;
                              view->scroll = ce_view_follow_cursor(view, app.config_options.horizontal_scroll_off,
                                                                   app.config_options.vertical_scroll_off,
                                                                   app.config_options.tab_width);
                         }
                    }
               }else if(strcmp(app.input_view.buffer->name, "REVERSE SEARCH") == 0){
                    if(app.input_view.buffer->line_count && view->buffer->line_count && strlen(app.input_view.buffer->lines[0])){
                         CePoint_t match_point = ce_buffer_search_backward(view->buffer, view->cursor, app.input_view.buffer->lines[0]);
                         if(match_point.x >= 0){
                              view->cursor = match_point;
                              view->scroll = ce_view_follow_cursor(view, app.config_options.horizontal_scroll_off,
                                                                   app.config_options.vertical_scroll_off,
                                                                   app.config_options.tab_width);
                         }
                    }
               }else if(strcmp(app.input_view.buffer->name, "REGEX SEARCH") == 0){
                    if(app.input_view.buffer->line_count && view->buffer->line_count && strlen(app.input_view.buffer->lines[0])){
                         regex_t regex = {};
                         int rc = regcomp(&regex, app.input_view.buffer->lines[0], REG_EXTENDED);
                         if(rc != 0){
                              char error_buffer[BUFSIZ];
                              regerror(rc, &regex, error_buffer, BUFSIZ);
                              ce_log("regcomp() failed: '%s'", error_buffer);
                         }else{
                              CeRegexSearchResult_t result = ce_buffer_regex_search_forward(view->buffer, view->cursor, &regex);
                              if(result.point.x >= 0){
                                   view->cursor = result.point;
                                   view->scroll = ce_view_follow_cursor(view, app.config_options.horizontal_scroll_off,
                                                                        app.config_options.vertical_scroll_off,
                                                                        app.config_options.tab_width);
                              }
                         }
                    }
               }else if(strcmp(app.input_view.buffer->name, "REGEX REVERSE SEARCH") == 0){
                    if(app.input_view.buffer->line_count && view->buffer->line_count && strlen(app.input_view.buffer->lines[0])){
                         regex_t regex = {};
                         int rc = regcomp(&regex, app.input_view.buffer->lines[0], REG_EXTENDED);
                         if(rc != 0){
                              char error_buffer[BUFSIZ];
                              regerror(rc, &regex, error_buffer, BUFSIZ);
                              ce_log("regcomp() failed: '%s'", error_buffer);
                         }else{
                              CeRegexSearchResult_t result = ce_buffer_regex_search_backward(view->buffer, view->cursor, &regex);
                              if(result.point.x >= 0){
                                   view->cursor = result.point;
                                   view->scroll = ce_view_follow_cursor(view, app.config_options.horizontal_scroll_off,
                                                                        app.config_options.vertical_scroll_off,
                                                                        app.config_options.tab_width);
                              }
                         }
                    }
               }
          }

          if(ce_layout_buffer_in_view(tab_layout, app.buffer_list_buffer)){
               build_buffer_list(app.buffer_list_buffer, app.buffer_node_head);
          }

          if(ce_layout_buffer_in_view(tab_layout, app.yank_list_buffer)){
               build_yank_list(app.yank_list_buffer, app.vim.yanks);
          }

          draw_thread_data->ready_to_draw = true;
     }

     // cleanup
     draw_thread_data->done = true;
     pthread_cancel(thread_draw);
     pthread_join(thread_draw, NULL);

     buffer_node_free(&app.buffer_node_head);
     ce_layout_free(&app.tab_list_layout);
     ce_vim_free(&app.vim);
     free(draw_thread_data);
     endwin();
     return 0;
}
