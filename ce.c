#include "ce.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool ce_buffer_alloc(CeBuffer_t* buffer, int64_t line_count, const char* name){
     if(buffer->lines) ce_buffer_free(buffer);

     if(line_count <= 0){
          // print error
          return false;
     }

     buffer->lines = (char**)malloc(line_count * sizeof(*buffer->lines));
     buffer->line_count = line_count;
     buffer->name = strdup(name);

     for(int64_t i = 0; i < line_count; i++){
          buffer->lines[i] = (char*)calloc(1, sizeof(buffer->lines[i]));
     }

     buffer->status = CE_BUFFER_STATUS_MODIFIED;
     return true;
}

void ce_buffer_free(CeBuffer_t* buffer){
     for(int64_t i = 0; i < buffer->line_count; i++){
          free(buffer->lines[i]);
     }

     free(buffer->lines);
     free(buffer->name);

     memset(buffer, 0, sizeof(*buffer));
}

bool ce_buffer_load_file(CeBuffer_t* buffer, const char* filename){
     // read the entire file
     size_t content_size;
     char* contents = NULL;

     FILE* file = fopen(filename, "rb");
     if(!file){
          //ce_message("%s() fopen('%s', 'rb') failed: %s", __FUNCTION__, filename, strerror(errno));
          return true;
     }

     fseek(file, 0, SEEK_END);
     content_size = ftell(file);
     fseek(file, 0, SEEK_SET);

     contents = (char*)malloc(content_size + 1);
     fread(contents, content_size, 1, file);
     contents[content_size] = 0;

     // strip the ending '\n'
     if(contents[content_size - 1] == CE_NEWLINE) contents[content_size - 1] = 0;

     if(!ce_buffer_load_string(buffer, contents, filename)){
          return false;
     }

     fclose(file);

     if(access(filename, W_OK) != 0){
          buffer->status = CE_BUFFER_STATUS_READONLY;
     }else{
          buffer->status = CE_BUFFER_STATUS_NONE;
     }

     free(contents);
     return true;
}

bool ce_buffer_load_string(CeBuffer_t* buffer, const char* string, const char* name){
     if(buffer->lines) ce_buffer_free(buffer);

     int64_t line_count = ce_util_count_string_lines(string);

     // allocate for the number of lines contained in the string
     buffer->lines = (char**)malloc(line_count * sizeof(*buffer->lines));
     buffer->line_count = line_count;
     buffer->name = strdup(name);

     // loop over each line
     const char* newline = NULL;
     size_t line_len = 0;
     for(int64_t i = 0; i < line_count; i++){
          // look for the newline
          newline = strchr(string, CE_NEWLINE);

          if(newline){
               // allocate space for the line, excluding the newline character
               line_len = newline - string;
               buffer->lines[i] = (char*)malloc(line_len + 1);
               buffer->lines[i] = strncpy(buffer->lines[i], string, line_len);
               buffer->lines[i][line_len] = 0;

               string = newline + 1;
          }else{
               // if this is the end, just dupe it
               buffer->lines[i] = strdup(string);
               break;
          }
     }

     return true;
}

bool ce_buffer_empty(CeBuffer_t* buffer){
     if(buffer->lines == NULL) return false;

     // re allocate it down to a single blank line
     buffer->lines = realloc(buffer->lines, sizeof(*buffer->lines));
     buffer->lines[0] = realloc(buffer->lines[0], sizeof(buffer->lines[0]));
     buffer->lines[0][0] = 0;
     buffer->line_count = 1;

     return true;
}

int64_t ce_buffer_contains_point(CeBuffer_t* buffer, CePoint_t point){
     if(point.y < 0 || point.y >= buffer->line_count || point.x < 0) return false;
     int64_t line_len = ce_utf8_strlen(buffer->lines[point.y]);
     if(point.x >= line_len){
          if(line_len == 0 && point.x == 0){
               return true;
          }
          return false;
     }

     return true;
}

int64_t ce_buffer_line_len(CeBuffer_t* buffer, int64_t line){
     if(line < 0 || line > buffer->line_count) return -1;

     return ce_utf8_strlen(buffer->lines[line]);
}

CePoint_t ce_buffer_move_point(CeBuffer_t* buffer, CePoint_t point, CePoint_t delta, int64_t tab_width, bool allow_passed_end){
     if(!ce_buffer_contains_point(buffer, point)) return point;

     if(delta.y){
          // figure out where we are visibly (due to tabs being variable length)
          int64_t cur_visible_index = ce_util_string_index_to_visible_index(buffer->lines[point.y], point.x, tab_width);

          // move to the new line
          point.y += delta.y;
          CE_CLAMP(point.y, 0, (buffer->line_count - 1));

          // convert the x from visible index to a string index
          point.x = ce_util_visible_index_to_string_index(buffer->lines[point.y], cur_visible_index, tab_width);
     }

     point.x += delta.x;
     int64_t line_len = ce_buffer_line_len(buffer, point.y);

     if(allow_passed_end){
          CE_CLAMP(point.x, 0, line_len);
     }else{
          if(line_len == 0){
               point.x = 0;
          }else{
               CE_CLAMP(point.x, 0, (line_len - 1));
          }
     }

     return point;
}

void ce_view_follow_cursor(CeView_t* view, int64_t horizontal_scroll_off, int64_t vertical_scroll_off, int64_t tab_width){
     if(!view->buffer) return;

     int64_t scroll_left = view->scroll.x + horizontal_scroll_off;
     int64_t scroll_top = view->scroll.y + vertical_scroll_off;
     int64_t scroll_right = view->scroll.x + (view->rect.right - view->rect.left) - horizontal_scroll_off;
     int64_t scroll_bottom = view->scroll.y + (view->rect.bottom - view->rect.top) - vertical_scroll_off;

     int64_t visible_index = ce_util_string_index_to_visible_index(view->buffer->lines[view->buffer->cursor.y],
                                                                   view->buffer->cursor.x, tab_width);

     if(visible_index < scroll_left){
          view->scroll.x -= (scroll_left - visible_index);
          if(view->scroll.x < 0) view->scroll.x = 0;
     }else if(visible_index > scroll_right){
          view->scroll.x += (visible_index - scroll_right);
     }

     if(view->buffer->cursor.y < scroll_top){
          view->scroll.y -= (scroll_top - view->buffer->cursor.y);
          if(view->scroll.y < 0) view->scroll.y = 0;
     }else if(view->buffer->cursor.y > scroll_bottom){
          view->scroll.y += (view->buffer->cursor.y - scroll_bottom);
     }
}

int64_t ce_utf8_strlen(const char* string){
     int64_t len = 0;
     int64_t byte_count = 0;

     while(*string){
          if((*string & 0x80) == 0){
               byte_count = 1;
          }else if((*string & 0xE0) == 0xC0){
               byte_count = 2;
          }else if((*string & 0xF0) == 0xE0){
               byte_count = 3;
          }else if((*string & 0xF8) == 0xF0){
               byte_count = 4;
          }else{
               return -1;
          }

          // validate string doesn't early terminate
          for(int64_t i = 0; i < byte_count; i++){
               if(*string == 0) return -1;
               string++;
          }

          len++;
     }

     return len;
}

CeRune_t ce_utf8_decode(const char* string, int64_t* bytes_consumed){
     CeRune_t rune;

     // 0xxxxxxx is just ascii
     if((string[0] & 0x80) == 0){
          *bytes_consumed = 1;
          rune = string[0];
     // 110xxxxx is a 2 byte utf8 string
     }else if((*string & 0xE0) == 0xC0){
          *bytes_consumed = 2;
          rune = string[0] & 0x1F;
          rune <<= 6;
          rune |= string[1] & 0x3F;
     // 1110xxxx is a 3 byte utf8 string
     }else if((*string & 0xF0) == 0xE0){
          *bytes_consumed = 3;
          rune = string[0] & 0x0F;
          rune <<= 6;
          rune |= string[1] & 0x3F;
          rune <<= 6;
          rune |= string[2] & 0x3F;
     // 11110xxx is a 4 byte utf8 string
     }else if((*string & 0xF8) == 0xF0){
          *bytes_consumed = 4;
          rune = string[0] & 0x0F;
          rune <<= 6;
          rune |= string[1] & 0x3F;
          rune <<= 6;
          rune |= string[2] & 0x3F;
          rune <<= 6;
          rune |= string[3] & 0x3F;
     }else{
          return CE_UTF8_INVALID;
     }

     return rune;
}

bool ce_utf8_encode(CeRune_t u, char* string, int64_t string_len, int* bytes_written){
     if(u < 0x80){
          if(string_len < 1) return false;
          *bytes_written = 1;

          // leave as-is
          string[0] = u;
     }else if(u < 0x0800){
          if(string_len < 2) return false;
          *bytes_written = 2;

          // u = 00000000 00000000 00000abc defghijk

          // 2 bytes
          // first byte:  110abcde
          string[0] = 0xC0 | ((u >> 6) & 0x1f);

          // second byte: 10fghijk
          string[1] = 0x80 | (u & 0x3f);
     }else if(u < 0x10000){
          if(string_len < 3) return false;
          *bytes_written = 3;

          // u = 00000000 00000000 abcdefgh ijklmnop

          // 3 bytes
          // first byte:  1110abcd
          string[0] = 0xE0 | ((u >> 12) & 0x0F);

          // second byte: 10efghij
          string[1] = 0x80 | ((u >> 6) & 0x3F);

          // third byte:  10klmnop
          string[2] = 0x80 | (u & 0x3F);
     }else if(u < 0x110000){
          if(string_len < 4) return false;
          *bytes_written = 4;

          // u = 00000000 000abcde fghijklm nopqrstu

          // 4 bytes
          // first byte:  11110abc
          string[0] = 0xF0 | ((u >> 18) & 0x07);
          // second byte: 10defghi
          string[1] = 0x80 | ((u >> 12) & 0x3F);
          // third byte:  10jklmno
          string[2] = 0x80 | ((u >> 6) & 0x3F);
          // fourth byte: 10pqrstu
          string[3] = 0x80 | (u & 0x3F);
     }

     return true;
}

int64_t ce_util_count_string_lines(const char* string){
     int64_t string_length = strlen(string);
     int64_t line_count = 0;
     for(int64_t i = 0; i <= string_length; ++i){
          if(string[i] == CE_NEWLINE || string[i] == 0) line_count++;
     }

     // one line files usually contain newlines at the end
     if(line_count == 2 && string[string_length-1] == CE_NEWLINE){
          line_count--;
     }

     return line_count;
}

int64_t ce_util_string_index_to_visible_index(const char* string, int64_t index, int64_t tab_width){
     int64_t x = 0;
     int64_t rune_len = 0;
     CeRune_t rune = 1;

     while(rune > 0 && index > 0){
          rune = ce_utf8_decode(string, &rune_len);

          if(rune > 0){
               if(rune == CE_TAB){
                    x += tab_width;
               }else{
                    x++;
               }
          }else{
               x++;
          }

          string += rune_len;
          index--;
     }

     return x;
}

int64_t ce_util_visible_index_to_string_index(const char* string, int64_t index, int64_t tab_width){
     int64_t x = 0;
     int64_t rune_len = 0;
     CeRune_t rune = 1;

     while(rune > 0 && index > 0){
          rune = ce_utf8_decode(string, &rune_len);

          if(rune > 0){
               if(rune == CE_TAB){
                    index -= tab_width;
               }else{
                    index--;
               }
          }else{
               index--;
          }

          string += rune_len;
          x++;
     }

     return x;
}
