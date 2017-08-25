#include "ce_syntax.h"

#include <stdlib.h>
#include <ncurses.h>
#include <string.h>
#include <ctype.h>

int ce_syntax_def_get_fg(CeSyntaxDef_t* syntax_defs, CeSyntaxColor_t syntax_color, int current_fg){
     int new_color = syntax_defs[syntax_color].fg;
     if(new_color == CE_SYNTAX_USE_CURRENT_COLOR) return current_fg;
     return new_color;
}

int ce_syntax_def_get_bg(CeSyntaxDef_t* syntax_defs, CeSyntaxColor_t syntax_color, int current_bg){
     int new_color = syntax_defs[syntax_color].bg;
     if(new_color == CE_SYNTAX_USE_CURRENT_COLOR) return current_bg;
     return new_color;
}

bool ce_draw_color_list_insert(CeDrawColorList_t* list, int fg, int bg, CePoint_t point){
     CeDrawColorNode_t* node = malloc(sizeof(*node));
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

void ce_draw_color_list_free(CeDrawColorList_t* list){
     CeDrawColorNode_t* itr = list->head;
     while(itr){
          CeDrawColorNode_t* tmp = itr;
          itr = itr->next;
          free(tmp);
     }

     list->head = NULL;
     list->tail = NULL;
}

bool ce_range_list_insert(CeRangeList_t* list, CePoint_t start, CePoint_t end){
     CeRangeNode_t* node = malloc(sizeof(*node));
     if(!node) return false;
     node->range.start = start;
     node->range.end = end;
     node->next = NULL;
     if(list->tail) list->tail->next = node;
     list->tail = node;
     if(!list->head) list->head = node;
     return true;
}

void ce_range_list_free(CeRangeList_t* list){
     CeRangeNode_t* itr = list->head;
     while(itr){
          CeRangeNode_t* tmp = itr;
          itr = itr->next;
          free(tmp);
     }

     list->head = NULL;
     list->tail = NULL;
}

int ce_draw_color_list_last_fg_color(CeDrawColorList_t* draw_color_list){
     int fg = COLOR_DEFAULT;
     if(draw_color_list->tail) fg = draw_color_list->tail->fg;
     return fg;
}

int ce_draw_color_list_last_bg_color(CeDrawColorList_t* draw_color_list){
     int bg = COLOR_DEFAULT;
     if(draw_color_list->tail) bg = draw_color_list->tail->bg;
     return bg;
}

int ce_color_def_get(CeColorDefs_t* color_defs, int fg, int bg){
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

static bool is_c_type_char(int ch){
     return isalnum(ch) || ch == '_';
}

static int64_t match_words(const char* str, const char* beginning_of_line, const char** words, int64_t word_count){
     for(int64_t i = 0; i < word_count; ++i){
          int64_t word_len = strlen(words[i]);
          if(strncmp(words[i], str, word_len) == 0){
               // make sure word isn't in the middle of an identifier
               char post_char = str[word_len];
               if(is_c_type_char(post_char)) return 0;
               if(str > beginning_of_line){
                    char pre_char = *(str - 1);
                    if(is_c_type_char(pre_char)) return 0;
               }

               return word_len;
          }
     }

     return 0;
}

static int64_t match_c_type(const char* str, const char* beginning_of_line){
     if(!isalpha(*str)) return false;

     const char* itr = str;
     while(*itr){
          if(!is_c_type_char(*itr)) break;
          itr++;
     }

     int64_t len = itr - str;
     if(len <= 2) return 0;

     if(strncmp((itr - 2), "_t", 2) == 0) return len;

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

static int64_t match_c_keyword(const char* str, const char* beginning_of_line){
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

     return match_words(str, beginning_of_line, keywords, keyword_count);
}

static int64_t match_c_control(const char* str, const char* beginning_of_line){
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

static int64_t match_caps_var(const char* str, const char* beginning_of_line){
     const char* itr = str;
     while(*itr){
          if(!is_caps_var_char(*itr)) break;
          itr++;
     }

     // check before and after the match for non c type chars
     if(str > beginning_of_line){
          if(is_c_type_char(*(str - 1))) return 0;
     }
     if(is_c_type_char(*itr)) return 0;

     int64_t len = itr - str;
     if(len > 1) return len;
     return 0;
}

static int64_t match_c_preproc(const char* str){
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

static int64_t match_c_comment(const char* str){
     if(strncmp("//", str, 2) == 0) return ce_utf8_strlen(str);

     return 0;
}

static int64_t match_c_multiline_comment(const char* str){
     if(strncmp("/*", str, 2) == 0){
          char* matching_comment = strstr(str, "*/");
          if(matching_comment) return (matching_comment - str);
          return ce_utf8_strlen(str);
     }

     return 0;
}

static int64_t match_c_multiline_comment_end(const char* str){
     if(strncmp("*/", str, 2) == 0) return 2;

     return 0;
}

static int64_t match_c_string(const char* str){
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

static int64_t match_c_character_literal(const char* str){
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

static void change_draw_color(CeDrawColorList_t* draw_color_list, CeSyntaxDef_t* syntax_defs, CeSyntaxColor_t syntax_color, CePoint_t point){
     int fg = ce_syntax_def_get_fg(syntax_defs, syntax_color, ce_draw_color_list_last_fg_color(draw_color_list));
     int bg = ce_syntax_def_get_bg(syntax_defs, syntax_color, ce_draw_color_list_last_bg_color(draw_color_list));
     ce_draw_color_list_insert(draw_color_list, fg, bg, point);
}

void ce_syntax_highlight_c(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                           CeSyntaxDef_t* syntax_defs, void* user_data){
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
     bool in_visual = false;
     CeRangeNode_t* range_node = highlight_range_list->head;

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          int64_t line_len = ce_utf8_strlen(line);
          int64_t current_match_len = 1;
          CePoint_t match_point = {0, y};

          if(multiline_comment){
               change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_COMMENT, match_point);
          }

          for(int64_t x = 0; x < line_len; ++x){
               char* str = ce_utf8_iterate_to(line, x);
               match_point.x = x;

               if(range_node){
                    if(in_visual){
                         if(ce_point_after(match_point, range_node->range.end)){
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                              range_node = range_node->next;
                              in_visual = false;
                         }
                    }else{
                         if(ce_points_equal(match_point, range_node->range.start)){
                              int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_VISUAL, ce_draw_color_list_last_bg_color(draw_color_list));
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), bg, match_point);
                              in_visual = true;
                         }else if(ce_point_after(match_point, range_node->range.end)){
                              range_node = range_node->next;
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
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_TYPE, match_point);
                         }else if((match_len = match_c_keyword(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_KEYWORD, match_point);
                         }else if((match_len = match_c_control(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CONTROL, match_point);
                         }else if((match_len = match_caps_var(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CAPS_VAR, match_point);
                         }else if((match_len = match_c_comment(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_COMMENT, match_point);
                         }else if((match_len = match_c_string(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_STRING, match_point);
                         }else if((match_len = match_c_character_literal(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CHAR_LITERAL, match_point);
                         }else if((match_len = match_c_literal(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NUMBER_LITERAL, match_point);
                         }else if((match_len = match_c_preproc(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_PREPROCESSOR, match_point);
                         }else if((match_len = match_c_multiline_comment(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_COMMENT, match_point);
                              multiline_comment = true;
                         }else if(((view->cursor.y != y) || (x > view->cursor.x)) && (match_len = match_trailing_whitespace(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_TRAILING_WHITESPACE, match_point);
                              ce_draw_color_list_insert(draw_color_list, ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                        ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                        (CePoint_t){0, match_point.y + 1});
                         }else if(!draw_color_list->tail || (draw_color_list->tail->fg != COLOR_DEFAULT || draw_color_list->tail->bg != COLOR_DEFAULT)){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NORMAL, match_point);
                         }
                    }

                    if(match_len) current_match_len = match_len;
               }else{
                    current_match_len--;
               }
          }

          // handle case where visual mode ends at the end of the line
          match_point.x = line_len;
          if(range_node && in_visual){
               // TODO: compress with above
               if(ce_point_after(match_point, range_node->range.end)){
                    ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                    range_node = range_node->next;
                    in_visual = false;
               }
          }
     }
}

static int64_t match_java_type(const char* str, const char* beginning_of_line){
     static const char* keywords[] = {
          "boolean",
          "byte",
          "float",
          "int",
          "long",
          "short",
          "void",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, beginning_of_line, keywords, keyword_count);
}

static int64_t match_java_keyword(const char* str, const char* beginning_of_line){
     static const char* keywords[] = {
          "abstract",
          "for",
          "new",
          "switch",
          "assert",
          "default",
          "package",
          "synchronized",
          "do",
          "if",
          "private",
          "this",
          "double",
          "implements",
          "protected",
          "else",
          "import",
          "public",
          "case",
          "enum",
          "instanceof",
          "transient",
          "extends",
          "char",
          "final",
          "interface",
          "static",
          "class",
          "strictfp",
          "volatile",
          "native",
          "super",
          "while",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, beginning_of_line, keywords, keyword_count);
}

static int64_t match_java_control(const char* str, const char* beginning_of_line){
     static const char* keywords [] = {
          "try",
          "continue",
          "goto",
          "break",
          "throw",
          "throws",
          "return",
          "catch",
          "finally",
          "const",


          "yield",
          "break",
          "except",
          "raise",
          "continue",
          "finally",
          "return",
          "try",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, beginning_of_line, keywords, keyword_count);
}

void ce_syntax_highlight_java(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                              CeSyntaxDef_t* syntax_defs, void* user_data){
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
     bool in_visual = false;
     CeRangeNode_t* range_node = highlight_range_list->head;

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          int64_t line_len = ce_utf8_strlen(line);
          int64_t current_match_len = 1;
          CePoint_t match_point = {0, y};

          if(multiline_comment){
               change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_COMMENT, match_point);
          }

          for(int64_t x = 0; x < line_len; ++x){
               char* str = ce_utf8_iterate_to(line, x);
               match_point.x = x;

               if(range_node){
                    if(in_visual){
                         if(ce_point_after(match_point, range_node->range.end)){
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                              range_node = range_node->next;
                              in_visual = false;
                         }
                    }else{
                         if(ce_points_equal(match_point, range_node->range.start)){
                              int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_VISUAL, ce_draw_color_list_last_bg_color(draw_color_list));
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), bg, match_point);
                              in_visual = true;
                         }else if(ce_point_after(match_point, range_node->range.end)){
                              range_node = range_node->next;
                         }
                    }
               }

               if(current_match_len <= 1){
                    if(multiline_comment){
                         if((match_len = match_c_multiline_comment_end(str))){
                              multiline_comment = false;
                         }
                    }else{
                         if((match_len = match_java_type(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_TYPE, match_point);
                         }else if((match_len = match_java_keyword(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_KEYWORD, match_point);
                         }else if((match_len = match_java_control(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CONTROL, match_point);
                         }else if((match_len = match_caps_var(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CAPS_VAR, match_point);
                         }else if((match_len = match_c_comment(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_COMMENT, match_point);
                         }else if((match_len = match_c_string(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_STRING, match_point);
                         }else if((match_len = match_c_character_literal(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CHAR_LITERAL, match_point);
                         }else if((match_len = match_c_literal(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NUMBER_LITERAL, match_point);
                         }else if((match_len = match_c_multiline_comment(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_COMMENT, match_point);
                              multiline_comment = true;
                         }else if(((view->cursor.y != y) || (x > view->cursor.x)) && (match_len = match_trailing_whitespace(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_TRAILING_WHITESPACE, match_point);
                              ce_draw_color_list_insert(draw_color_list, ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                        ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                        (CePoint_t){0, match_point.y + 1});
                         }else if(!draw_color_list->tail || (draw_color_list->tail->fg != COLOR_DEFAULT || draw_color_list->tail->bg != COLOR_DEFAULT)){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NORMAL, match_point);
                         }
                    }

                    if(match_len) current_match_len = match_len;
               }else{
                    current_match_len--;
               }
          }

          // handle case where visual mode ends at the end of the line
          match_point.x = line_len;
          if(range_node && in_visual){
               // TODO: compress with above
               if(ce_point_after(match_point, range_node->range.end)){
                    ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                    range_node = range_node->next;
                    in_visual = false;
               }
          }
     }
}

static int64_t match_python_keyword(const char* str, const char* beginning_of_line){
     static const char* keywords[] = {
          "and",
          "del",
          "from",
          "not",
          "while",
          "as",
          "elif",
          "global",
          "or",
          "with",
          "assert",
          "else",
          "if",
          "pass",
          "import",
          "print",
          "class",
          "exec",
          "in",
          "finally",
          "is",
          "def",
          "for",
          "lambda",
          "self",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, beginning_of_line, keywords, keyword_count);
}

static int64_t match_python_control(const char* str, const char* beginning_of_line){
     static const char* keywords[] = {
          "yield",
          "break",
          "except",
          "raise",
          "continue",
          "finally",
          "return",
          "try",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, beginning_of_line, keywords, keyword_count);
}

static int64_t match_python_comment(const char* str){
     if(*str == '#') return ce_utf8_strlen(str);

     return 0;
}

static int64_t match_python_string(const char* str){
     if(*str == '\'' || *str == '"'){
          const char* match = str;
          while(match){
               match = strchr(match + 1, *str);
               if(match && *(match - 1) != '\\'){
                    return ce_utf8_strlen_between(str, match) + 1;
               }
          }
     }

     return 0;
}

typedef enum{
     PYTHON_DOCSTRING_NONE,
     PYTHON_DOCSTRING_SINGLE_QUOTE,
     PYTHON_DOCSTRING_DOUBLE_QUOTE,
}PythonDocstring_t;

static int64_t match_python_docstring(const char* str, PythonDocstring_t* python_docstring){
     if(strncmp(str, "\"\"\"", 3) == 0){
          *python_docstring = PYTHON_DOCSTRING_DOUBLE_QUOTE;
          char* match = strstr(str, "\"\"\"");
          if(match) return ce_utf8_strlen_between(str, match) + 3;
          return ce_utf8_strlen(str);
     }

     if(strncmp(str, "'''", 3) == 0){
          *python_docstring = PYTHON_DOCSTRING_SINGLE_QUOTE;
          char* match = strstr(str, "'''");
          if(match) return ce_utf8_strlen_between(str, match) + 3;
          return ce_utf8_strlen(str);
     }

     return 0;
}

void ce_syntax_highlight_python(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                CeSyntaxDef_t* syntax_defs, void* user_data){
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     int64_t match_len = 0;
     bool in_visual = false;
     PythonDocstring_t docstring = PYTHON_DOCSTRING_NONE;
     CeRangeNode_t* range_node = highlight_range_list->head;

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          int64_t line_len = ce_utf8_strlen(line);
          int64_t current_match_len = 1;
          CePoint_t match_point = {0, y};

          if(docstring){
               change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_STRING, match_point);
          }

          for(int64_t x = 0; x < line_len; ++x){
               char* str = ce_utf8_iterate_to(line, x);
               match_point.x = x;

               // TODO: compress
               if(range_node){
                    if(in_visual){
                         if(ce_point_after(match_point, range_node->range.end)){
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                              range_node = range_node->next;
                              in_visual = false;
                         }
                    }else{
                         if(ce_points_equal(match_point, range_node->range.start)){
                              int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_VISUAL, ce_draw_color_list_last_bg_color(draw_color_list));
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), bg, match_point);
                              in_visual = true;
                         }else if(ce_point_after(match_point, range_node->range.end)){
                              range_node = range_node->next;
                         }
                    }
               }

               if(current_match_len <= 1){
                    if(docstring){
                         if(docstring == PYTHON_DOCSTRING_DOUBLE_QUOTE && strncmp(str, "\"\"\"", 3) == 0){
                              docstring = PYTHON_DOCSTRING_NONE;
                              match_len = 3;
                         }else if(docstring == PYTHON_DOCSTRING_SINGLE_QUOTE && strncmp(str, "'''", 3) == 0){
                              docstring = PYTHON_DOCSTRING_NONE;
                              match_len = 3;
                         }
                    }else{
                         if((match_len = match_c_type(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_TYPE, match_point);
                         }else if((match_len = match_python_keyword(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_KEYWORD, match_point);
                         }else if((match_len = match_python_control(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CONTROL, match_point);
                         }else if((match_len = match_caps_var(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CAPS_VAR, match_point);
                         }else if((match_len = match_python_comment(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_COMMENT, match_point);
                         }else if((match_len = match_python_docstring(str, &docstring))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_STRING, match_point);
                         }else if((match_len = match_python_string(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_STRING, match_point);
                         }else if((match_len = match_c_literal(str, line))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NUMBER_LITERAL, match_point);
                         }else if(((view->cursor.y != y) || (x > view->cursor.x)) && (match_len = match_trailing_whitespace(str))){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_TRAILING_WHITESPACE, match_point);
                              ce_draw_color_list_insert(draw_color_list, ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                        ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                        (CePoint_t){0, match_point.y + 1});
                         }else if(!draw_color_list->tail || (draw_color_list->tail->fg != COLOR_DEFAULT || draw_color_list->tail->bg != COLOR_DEFAULT)){
                              change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NORMAL, match_point);
                         }
                    }

                    if(match_len) current_match_len = match_len;
               }else{
                    current_match_len--;
               }
          }

          // handle case where visual mode ends at the end of the line
          match_point.x = line_len;
          if(range_node && in_visual){
               // TODO: compress with above
               if(ce_point_after(match_point, range_node->range.end)){
                    ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                    range_node = range_node->next;
                    in_visual = false;
               }
          }
     }
}

static int64_t match_bash_keyword(const char* str, const char* beginning_of_line){
     static const char* keywords [] = {
          "if",
          "then",
          "else",
          "elif",
          "fi",
          "case",
          "esac",
          "for",
          "select",
          "while",
          "until",
          "do",
          "done",
          "in",
          "function",
          "time",
          "coproc",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, beginning_of_line, keywords, keyword_count);
}

void ce_syntax_highlight_bash(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                              CeSyntaxDef_t* syntax_defs, void* user_data){
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     int64_t match_len = 0;
     bool in_visual = false;
     CeRangeNode_t* range_node = highlight_range_list->head;

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          int64_t line_len = ce_utf8_strlen(line);
          int64_t current_match_len = 1;
          CePoint_t match_point = {0, y};

          for(int64_t x = 0; x < line_len; ++x){
               char* str = ce_utf8_iterate_to(line, x);
               match_point.x = x;

               // TODO: compress
               if(range_node){
                    if(in_visual){
                         if(ce_point_after(match_point, range_node->range.end)){
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                              range_node = range_node->next;
                              in_visual = false;
                         }
                    }else{
                         if(ce_points_equal(match_point, range_node->range.start)){
                              int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_VISUAL, ce_draw_color_list_last_bg_color(draw_color_list));
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), bg, match_point);
                              in_visual = true;
                         }else if(ce_point_after(match_point, range_node->range.end)){
                              range_node = range_node->next;
                         }
                    }
               }

               if(current_match_len <= 1){
                    if((match_len = match_bash_keyword(str, line))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_KEYWORD, match_point);
                    }else if((match_len = match_caps_var(str, line))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CAPS_VAR, match_point);
                    }else if((match_len = match_python_comment(str))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_COMMENT, match_point);
                    }else if((match_len = match_python_string(str))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_STRING, match_point);
                    }else if((match_len = match_c_literal(str, line))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NUMBER_LITERAL, match_point);
                    }else if(((view->cursor.y != y) || (x > view->cursor.x)) && (match_len = match_trailing_whitespace(str))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_TRAILING_WHITESPACE, match_point);
                         ce_draw_color_list_insert(draw_color_list, ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                   ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                   (CePoint_t){0, match_point.y + 1});
                    }else if(!draw_color_list->tail || (draw_color_list->tail->fg != COLOR_DEFAULT || draw_color_list->tail->bg != COLOR_DEFAULT)){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NORMAL, match_point);
                    }

                    if(match_len) current_match_len = match_len;
               }else{
                    current_match_len--;
               }
          }

          // handle case where visual mode ends at the end of the line
          match_point.x = line_len;
          if(range_node && in_visual){
               // TODO: compress with above
               if(ce_point_after(match_point, range_node->range.end)){
                    ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                    range_node = range_node->next;
                    in_visual = false;
               }
          }
     }
}

static int64_t match_config_keyword(const char* str, const char* beginning_of_line){
     static const char* keywords [] = {
          "true",
          "false",
     };

     static const int64_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

     return match_words(str, beginning_of_line, keywords, keyword_count);
}

void ce_syntax_highlight_config(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                                CeSyntaxDef_t* syntax_defs, void* user_data){
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     int64_t match_len = 0;
     bool in_visual = false;
     CeRangeNode_t* range_node = highlight_range_list->head;

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          int64_t line_len = ce_utf8_strlen(line);
          int64_t current_match_len = 1;
          CePoint_t match_point = {0, y};

          for(int64_t x = 0; x < line_len; ++x){
               char* str = ce_utf8_iterate_to(line, x); // TODO: decode rather than iterating to each time
               match_point.x = x;

               // TODO: compress
               if(range_node){
                    if(in_visual){
                         if(ce_point_after(match_point, range_node->range.end)){
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                              range_node = range_node->next;
                              in_visual = false;
                         }
                    }else{
                         if(ce_points_equal(match_point, range_node->range.start)){
                              int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_VISUAL, ce_draw_color_list_last_bg_color(draw_color_list));
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), bg, match_point);
                              in_visual = true;
                         }else if(ce_point_after(match_point, range_node->range.end)){
                              range_node = range_node->next;
                         }
                    }
               }

               if(current_match_len <= 1){
                    if((match_len = match_config_keyword(str, line))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_KEYWORD, match_point);
                    }else if((match_len = match_caps_var(str, line))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_CAPS_VAR, match_point);
                    }else if((match_len = match_python_comment(str))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_COMMENT, match_point);
                    }else if((match_len = match_python_string(str))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_STRING, match_point);
                    }else if((match_len = match_c_literal(str, line))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NUMBER_LITERAL, match_point);
                    }else if(((view->cursor.y != y) || (x > view->cursor.x)) && (match_len = match_trailing_whitespace(str))){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_TRAILING_WHITESPACE, match_point);
                         ce_draw_color_list_insert(draw_color_list, ce_syntax_def_get_fg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                   ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_NORMAL, COLOR_DEFAULT),
                                                   (CePoint_t){0, match_point.y + 1});
                    }else if(!draw_color_list->tail || (draw_color_list->tail->fg != COLOR_DEFAULT || draw_color_list->tail->bg != COLOR_DEFAULT)){
                         change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NORMAL, match_point);
                    }

                    if(match_len) current_match_len = match_len;
               }else{
                    current_match_len--;
               }
          }

          // handle case where visual mode ends at the end of the line
          match_point.x = line_len;
          if(range_node && in_visual){
               // TODO: compress with above
               if(ce_point_after(match_point, range_node->range.end)){
                    ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                    range_node = range_node->next;
                    in_visual = false;
               }
          }
     }
}

void ce_syntax_highlight_diff(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                              CeSyntaxDef_t* syntax_defs, void* user_data){
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     bool in_visual = false;
     CeRangeNode_t* range_node = highlight_range_list->head;

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          int64_t line_len = ce_utf8_strlen(line);
          int64_t current_match_len = 1;
          CePoint_t match_point = {0, y};

          if(*line == '+'){
               change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_DIFF_ADD, match_point);
               current_match_len = line_len + 1;
          }else if(*line == '-'){
               change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_DIFF_REMOVE, match_point);
               current_match_len = line_len + 1;
          }else if(*line == '#'){
               change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_DIFF_COMMENT, match_point);
               current_match_len = line_len + 1;
          }else if(strncmp(line, "@@", 2) == 0){
               char* end = strstr(line + 2, "@@");
               if(end){
                    change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_DIFF_HEADER, match_point);
                    current_match_len = ce_utf8_strlen_between(line, end + 2);
               }
          }else{
               change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NORMAL, match_point);
          }

          for(int64_t x = 0; x < line_len; ++x){
               match_point.x = x;

               // TODO: compress
               if(range_node){
                    if(in_visual){
                         if(ce_point_after(match_point, range_node->range.end)){
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                              range_node = range_node->next;
                              in_visual = false;
                         }
                    }else{
                         if(ce_points_equal(match_point, range_node->range.start)){
                              int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_VISUAL, ce_draw_color_list_last_bg_color(draw_color_list));
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), bg, match_point);
                              in_visual = true;
                         }else if(ce_point_after(match_point, range_node->range.end)){
                              range_node = range_node->next;
                         }
                    }
               }

               if(current_match_len <= 1){
                    change_draw_color(draw_color_list, syntax_defs, CE_SYNTAX_COLOR_NORMAL, match_point);
               }else{
                    current_match_len--;
               }
          }

          // handle case where visual mode ends at the end of the line
          match_point.x = line_len;
          if(range_node && in_visual){
               // TODO: compress with above
               if(ce_point_after(match_point, range_node->range.end)){
                    ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                    range_node = range_node->next;
                    in_visual = false;
               }
          }
     }
}

void ce_syntax_highlight_plain(CeView_t* view, CeRangeList_t* highlight_range_list, CeDrawColorList_t* draw_color_list,
                               CeSyntaxDef_t* syntax_defs, void* user_data){
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     bool in_visual = false;
     CeRangeNode_t* range_node = highlight_range_list->head;

     for(int64_t y = min; y <= max; ++y){
          char* line = view->buffer->lines[y];
          int64_t line_len = ce_utf8_strlen(line);
          CePoint_t match_point = {0, y};

          if(in_visual){
               int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_VISUAL, ce_draw_color_list_last_bg_color(draw_color_list));
               ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), bg, match_point);
          }

          for(int64_t x = 0; x < line_len; ++x){
               match_point.x = x;

               // TODO: compress
               if(range_node){
                    if(in_visual){
                         if(ce_point_after(match_point, range_node->range.end)){
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                              range_node = range_node->next;
                              in_visual = false;
                         }
                    }else{
                         if(ce_points_equal(match_point, range_node->range.start)){
                              int bg = ce_syntax_def_get_bg(syntax_defs, CE_SYNTAX_COLOR_VISUAL, ce_draw_color_list_last_bg_color(draw_color_list));
                              ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), bg, match_point);
                              in_visual = true;
                         }else if(ce_point_after(match_point, range_node->range.end)){
                              range_node = range_node->next;
                         }
                    }
               }
          }

          // handle case where visual mode ends at the end of the line
          match_point.x = line_len;
          if(range_node && in_visual){
               // TODO: compress with above
               if(ce_point_after(match_point, range_node->range.end)){
                    ce_draw_color_list_insert(draw_color_list, ce_draw_color_list_last_fg_color(draw_color_list), COLOR_DEFAULT, match_point);
                    range_node = range_node->next;
                    in_visual = false;
               }
          }
     }
}
