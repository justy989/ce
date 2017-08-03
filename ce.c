#include "ce.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

FILE* g_ce_log = NULL;

static void ce_buffer_change_node_free(CeBufferChangeNode_t** head){
     CeBufferChangeNode_t* itr = *head;
     while(itr){
          CeBufferChangeNode_t* tmp = itr;
          itr = itr->next;
          free(tmp->change.string);
          free(tmp);
     }

     *head = NULL;
}

bool ce_log_init(const char* filename){
     g_ce_log = fopen(filename, "wa");
     if(!g_ce_log){
          fprintf(stderr, "error: unable to create ce log: fopen(\"%s\", \"wa\") failed: '%s'\n", filename, strerror(errno));
          return false;
     }

     static const char* greetings [] = {
          "Thank you for flying ce",
          "There's nothing like a fresh cup of ce in the morning",
          "Why do kids love the taste of C Editor?\n\nIt's the taste you can ce",
          "ce is for C Editor, that's good enough for me",
          "I missed you.",
          "Hope you're having a great day! -ce",
          "You're a special person -- or robot. I don't judge.",
          "I missed you... in a creepy way.",
          "I'm a potato",
          "At least this isn't emacs? Am I right!",
          "TACOCAT is the best palindrome",
          "Found a bug? It's a feature.",
          "Yo.",
          "Slurp'n up whitespace since 2016",
          "Welcome to GNU Emacs, one component of the GNU/Linux operating system.",
          "ce, the world's only editor with a Michelin star.",
          "Oy! ce's a beaut!",
          "The default config has a great vimplementation!",
          "They see me slurpin' They hatin'",
          "'Days of pain are worth the years of upcoming prosperity' -confucius, probably",
          "ce, aka, 'the cache miss king'",
          "All the terminal you want with none of the illness",
          "I used ce before it was cool",
          "'Where has this been all my life' -emacs enthusiast",
     };

     srand(time(NULL));
     ce_log("%s\n", greetings[rand() % (sizeof(greetings) / sizeof(greetings[0]))]);
     return true;
}

void ce_log(const char* fmt, ...){
     va_list args;
     va_start(args, fmt);
     vfprintf(g_ce_log, fmt, args);
     va_end(args);
}

// NOTE: we expect that if we are downsizing, the lines that will be overwritten are freed prior to calling this func
static bool buffer_realloc_lines(CeBuffer_t* buffer, int64_t new_line_count){
     // if we want to realloc 0 lines, clear everything
     if(new_line_count == 0){
          free(buffer->lines);
          buffer->lines = NULL;
          buffer->line_count = 0;
          return true;
     }

     buffer->lines = realloc(buffer->lines, new_line_count * sizeof(buffer->lines[0]));
     if(buffer->lines == NULL) return false;
     buffer->line_count = new_line_count;
     return true;
}

bool ce_buffer_alloc(CeBuffer_t* buffer, int64_t line_count, const char* name){
     if(buffer->lines) ce_buffer_free(buffer);

     if(line_count <= 0){
          ce_log("%s() error: 0 line_count specified.\n", __FUNCTION__);
          return false;
     }

     buffer->lines = (char**)malloc(line_count * sizeof(*buffer->lines));
     if(!buffer->lines){
          ce_log("%s() failed to malloc() %ld lines.\n", __FUNCTION__, line_count);
          return false;
     }

     buffer->line_count = line_count;
     buffer->name = strdup(name);

     for(int64_t i = 0; i < line_count; i++){
          buffer->lines[i] = (char*)calloc(1, sizeof(buffer->lines[i]));
     }

     buffer->status = CE_BUFFER_STATUS_MODIFIED;
     pthread_mutex_init(&buffer->lock, NULL);
     return true;
}

void ce_buffer_free(CeBuffer_t* buffer){
     for(int64_t i = 0; i < buffer->line_count; i++){
          free(buffer->lines[i]);
     }

     free(buffer->lines);
     free(buffer->name);
     if(buffer->change_node) ce_buffer_change_node_free(&buffer->change_node);

     pthread_mutex_destroy(&buffer->lock);

     memset(buffer, 0, sizeof(*buffer));
}

bool ce_buffer_load_file(CeBuffer_t* buffer, const char* filename){
     // read the entire file
     size_t content_size;
     char* contents = NULL;

     FILE* file = fopen(filename, "rb");
     if(!file){
          ce_log("%s() fopen('%s', 'rb') failed: '%s'\n", __FUNCTION__, filename, strerror(errno));
          return false;
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
     ce_log("%s() loaded '%s'\n", __FUNCTION__, filename);
     return true;
}

bool ce_buffer_load_string(CeBuffer_t* buffer, const char* string, const char* name){
     if(buffer->lines) ce_buffer_free(buffer);

     int64_t line_count = ce_util_count_string_lines(string);

     // allocate for the number of lines contained in the string
     buffer->lines = (char**)malloc(line_count * sizeof(*buffer->lines));
     if(!buffer->lines){
          ce_log("%s() failed to allocate %ld lines\n", __FUNCTION__, line_count);
          return false;
     }

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

     pthread_mutex_init(&buffer->lock, NULL);
     return true;
}

bool ce_buffer_save(CeBuffer_t* buffer){
     FILE* file = fopen(buffer->name, "wb");
     if(!file){
          ce_log("%s() fopen('%s', 'wb') failed: '%s'\n", __FUNCTION__, buffer->name, strerror(errno));
          return false;
     }

     char newline = CE_NEWLINE;
     for(int64_t i = 0; i < buffer->line_count; ++i){
          int64_t line_len = strlen(buffer->lines[i]);
          fwrite(buffer->lines[i], 1, line_len, file);
          fwrite(&newline, 1, 1, file);
     }

     fclose(file);
     if(buffer->status == CE_BUFFER_STATUS_MODIFIED) buffer->status = CE_BUFFER_STATUS_NONE;
     buffer->save_at_change_node = buffer->change_node;
     return true;
}

bool ce_buffer_empty(CeBuffer_t* buffer){
     if(buffer->lines == NULL) return false;

     // free all lines after the first
     for(int64_t i = 1; i < buffer->line_count; ++i){
          free(buffer->lines[i]);
     }

     // re allocate it down to a single blank line
     buffer->lines = realloc(buffer->lines, sizeof(*buffer->lines));
     buffer->lines[0] = realloc(buffer->lines[0], sizeof(buffer->lines[0]));
     buffer->lines[0][0] = 0;
     buffer->line_count = 1;
     buffer->status = CE_BUFFER_STATUS_NONE;

     return true;
}

bool ce_buffer_contains_point(CeBuffer_t* buffer, CePoint_t point){
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

int64_t ce_buffer_point_is_valid(CeBuffer_t* buffer, CePoint_t point){
     if(point.y < 0 || point.y >= buffer->line_count || point.x < 0) return false;
     int64_t line_len = ce_utf8_strlen(buffer->lines[point.y]);
     if(point.x > line_len) return false;

     return true;
}

CeRune_t ce_buffer_get_rune(CeBuffer_t* buffer, CePoint_t point){
     if(!ce_buffer_point_is_valid(buffer, point)) return CE_UTF8_INVALID;

     char* str = ce_utf8_find_index(buffer->lines[point.y], point.x);
     int64_t rune_len = 0;
     return ce_utf8_decode(str, &rune_len);
}

CePoint_t ce_buffer_search_forward(CeBuffer_t* buffer, CePoint_t start, const char* pattern){
     CePoint_t result = (CePoint_t){-1, -1};

     if(!ce_buffer_point_is_valid(buffer, start)) return result;

     int64_t save_y = start.y;
     char* itr = ce_utf8_find_index(buffer->lines[start.y], start.x);
     char* match = NULL;

     // try to match the pattern on each line to the end
     while(true){
          match = strstr(itr, pattern);
          if(match) break;
          start.y++;
          if(start.y >= buffer->line_count) break;
          itr = buffer->lines[start.y];
     }

     if(match){
          // figure out index in the line
          int64_t index = 0;
          int64_t rune_len = 0;
          while(match > itr){
               ce_utf8_decode(itr, &rune_len);
               itr += rune_len;
               index++;
          }

          if(start.y == save_y){
               // if we are on the same line, use the starting x plus the index
               result.x = start.x + index;
          }else{
               result.x = index;
          }
          result.y = start.y;
     }

     return result;
}

CePoint_t ce_buffer_search_backward(CeBuffer_t* buffer, CePoint_t start, const char* pattern){
     CePoint_t result = (CePoint_t){-1, -1};

     if(!ce_buffer_point_is_valid(buffer, start)) return result;

     char* beginning_of_line = buffer->lines[start.y];
     char* itr = ce_utf8_find_index(beginning_of_line, start.x);
     bool match = false;
     size_t pattern_len = strlen(pattern);

     // try to match the pattern on each line to the end
     while(true){
          while(itr > beginning_of_line){
               if(strncmp(itr, pattern, pattern_len) == 0){
                    match = true;
                    break;
               }

               itr--;
          }
          if(match) break;
          start.y--;
          if(start.y < 0) break;
          beginning_of_line = buffer->lines[start.y];
          itr = beginning_of_line + ce_utf8_last_index(beginning_of_line);
     }

     if(match){
          // figure out index in the line
          int64_t index = 0;
          int64_t rune_len = 0;
          while(itr > beginning_of_line){
               ce_utf8_decode(beginning_of_line, &rune_len);
               beginning_of_line += rune_len;
               index++;
          }

          result.x = index;
          result.y = start.y;
     }

     return result;
}

CeRegexSearchResult_t ce_buffer_regex_search_forward(CeBuffer_t* buffer, CePoint_t start, const regex_t* regex){
     CeRegexSearchResult_t result = {(CePoint_t){-1, -1}, -1};

     if(!ce_buffer_point_is_valid(buffer, start)) return result;

     const size_t match_count = 1;
     regmatch_t matches[match_count];

     while(start.y < buffer->line_count){
          int rc = regexec(regex, buffer->lines[start.y] + start.x, match_count, matches, 0);
          if(rc == 0){
               result.point = start;
               result.point.x += matches[0].rm_so;
               result.length = matches[0].rm_eo - matches[0].rm_so;
               break;
          }else if(rc != REG_NOMATCH){
               char error_buffer[128];
               regerror(rc, regex, error_buffer, 128);
               ce_log("regexec() failed: '%s'", error_buffer);
               break;
          }

          start.y++;
          start.x = 0;
     }

     return result;
}

CeRegexSearchResult_t ce_buffer_regex_search_backward(CeBuffer_t* buffer, CePoint_t start, const regex_t* regex){
     CeRegexSearchResult_t result = {(CePoint_t){-1, -1}, -1};

     if(!ce_buffer_point_is_valid(buffer, start)) return result;

     const size_t match_count = 1;
     regmatch_t matches[match_count];

     CePoint_t location = start;

     // loop over each line, backwards
     while(true){
          CePoint_t last_valid_match = {-1, location.y};
          int64_t last_valid_match_len = 0;

          location.x = 0;

          if(buffer->lines[location.y][0]){
               // dupe the line up to the current index
               char* search_str = strdup(buffer->lines[location.y]);
               int64_t search_str_len = strlen(search_str);

               // start at the beginning of the line, find all matches up to the cursor and take that one
               while(location.x < search_str_len){
                    int rc = regexec(regex, search_str + location.x, match_count, matches, 0);

                    if(rc == 0){
                         int64_t match_x = location.x + matches[0].rm_so;

                         // if the match is after the start, then stop looking in this line
                         if(match_x >= start.x && location.y == start.y) break;

                         // save the match if we find one
                         last_valid_match.x = match_x;
                         last_valid_match_len = matches[0].rm_eo - matches[0].rm_so;
                         if(last_valid_match_len == 0) break;
                    }else{
                         // error out if regexec() fails for some reason other than no match
                         if(rc != REG_NOMATCH){
                              char error_buffer[128];
                              regerror(rc, regex, error_buffer, 128);
                              ce_log("regexec() failed: '%s'", error_buffer);
                              return result;
                         }

                         // if there was no match, stop looking in this line
                         break;
                    }

                    // update the next location to start after the match
                    location.x = last_valid_match.x + last_valid_match_len;
               }

               free(search_str);
          }

          if(last_valid_match.x >= 0){
               result.point = last_valid_match;
               result.length = last_valid_match_len;
               break;
          }

          location.y--;

          if(location.y < 0) break;
     }

     return result;
}

int64_t ce_buffer_range_len(CeBuffer_t* buffer, CePoint_t start, CePoint_t end){
     if(!ce_buffer_point_is_valid(buffer, start)) return -1;
     if(!ce_buffer_point_is_valid(buffer, end)) return -1;
     if(ce_point_after(start, end)) return -1; // TODO: calculate negative value?

     // easy case where they are on the same line
     if(start.y == end.y) return (end.x - start.x) + 1;

     int64_t length = 0;
     for(int64_t y = start.y; y <= end.y; ++y){
          if(y == start.y){
               // count from the star to the end of the line
               length = ce_utf8_strlen(ce_utf8_find_index(buffer->lines[y], start.x));
          }else if(y == end.y){
               // count up until the end
               int64_t line_length = ce_utf8_strlen(buffer->lines[y]);
               if(line_length == 0) length++;
               else length += end.x + 1;
          }else{
               // count entire line
               int64_t line_length = ce_utf8_strlen(buffer->lines[y]);
               if(line_length == 0) length++;
               length += line_length;
          }
     }

     return length;
}

int64_t ce_buffer_line_len(CeBuffer_t* buffer, int64_t line){
     if(line < 0 || line >= buffer->line_count) return -1;

     return ce_utf8_strlen(buffer->lines[line]);
}

CePoint_t ce_buffer_move_point(CeBuffer_t* buffer, CePoint_t point, CePoint_t delta, int64_t tab_width, CeClampX_t clamp_x){
     if(delta.y){
          // figure out where we are visibly (due to tabs being variable length)
          int64_t cur_visible_index = ce_util_string_index_to_visible_index(buffer->lines[point.y], point.x, tab_width);

          // move to the new line
          point.y += delta.y;

          // always clamp y
          CE_CLAMP(point.y, 0, (buffer->line_count - 1));

          // convert the x from visible index to a string index
          point.x = ce_util_visible_index_to_string_index(buffer->lines[point.y], cur_visible_index, tab_width);
     }

     point.x += delta.x;
     int64_t line_len = ce_buffer_line_len(buffer, point.y);

     switch(clamp_x){
     case CE_CLAMP_X_NONE:
          break;
     case CE_CLAMP_X_ON:
          CE_CLAMP(point.x, 0, line_len);
          break;
     case CE_CLAMP_X_INSIDE:
          if(line_len == 0){
               point.x = 0;
          }else{
               CE_CLAMP(point.x, 0, (line_len - 1));
          }
          break;
     }

     return point;
}

CePoint_t ce_buffer_advance_point(CeBuffer_t* buffer, CePoint_t point, int64_t delta){
     if(!ce_buffer_point_is_valid(buffer, point)) return (CePoint_t){-1, -1};

     if(delta < 0){
          while(delta < 0){
               int64_t destination = point.x + delta;
               if(destination < 0){
                    // if we are already at the beginning of the buffer, get out
                    int64_t new_line = point.y - 1;
                    if(new_line < 0){
                         point.x = 0;
                         break;
                    }

                    // move to the previous line, add to the delta
                    delta += point.x;
                    point.y = new_line;
                    point.x = ce_utf8_strlen(buffer->lines[point.y]);
               }else{
                    point.x = destination;
                    break;
               }
          }
     }else if(delta > 0){
          while(delta > 0){
               int64_t line_len = ce_utf8_strlen(buffer->lines[point.y]);
               int64_t destination = point.x + delta;
               if(destination > line_len){
                    // if we are already at the end of the buffer, get out
                    int64_t new_line = point.y + 1;
                    if(new_line >= buffer->line_count) break;

                    // move to the next line, and subtract from the delta
                    delta -= (line_len - point.x) + 1;
                    point.x = 0;
                    point.y = new_line;
               }else{
                    point.x = destination;
                    break;
               }
          }
     }

     return point;
}

CePoint_t ce_buffer_clamp_point(CeBuffer_t* buffer, CePoint_t point, CeClampX_t clamp_x){
     switch(clamp_x){
     case CE_CLAMP_X_NONE:
          return point;
     case CE_CLAMP_X_ON:
          if(buffer->line_count){
               CE_CLAMP(point.y, 0, (buffer->line_count - 1));
               int64_t line_len = ce_utf8_strlen(buffer->lines[point.y]);
               if(line_len){
                    CE_CLAMP(point.x, 0, (line_len - 1));
               }else{
                    point.x = 0;
               }
          }else{
               point.x = 0;
               point.y = 0;
          }
          break;
     case CE_CLAMP_X_INSIDE:
          if(buffer->line_count){
               CE_CLAMP(point.y, 0, (buffer->line_count - 1));
               int64_t line_len = ce_utf8_strlen(buffer->lines[point.y]);
               CE_CLAMP(point.x, 0, line_len);
          }else{
               point.x = 0;
               point.y = 0;
          }
          break;
     }

     return point;
}

bool ce_buffer_insert_string(CeBuffer_t* buffer, const char* string, CePoint_t point){
     if(buffer->status == CE_BUFFER_STATUS_READONLY) return false;

     if(!ce_buffer_point_is_valid(buffer, point)){
          if(buffer->line_count == 0 && ce_points_equal(point, (CePoint_t){0, 0})){
               int64_t string_len = strlen(string);
               buffer->lines = malloc(sizeof(*buffer->lines));
               buffer->lines[0] = malloc(string_len + 1);
               strcpy(buffer->lines[0], string);
               buffer->lines[0][string_len] = 0;
               buffer->line_count = 1;
               return true;
          }else{
               return false;
          }
     }

     int64_t string_lines = ce_util_count_string_lines(string);
     if(string_lines == 0){
          return true; // sure, yeah, we inserted that empty string
     }else if(string_lines == 1){
          pthread_mutex_lock(&buffer->lock);

          char* line = buffer->lines[point.y];
          size_t insert_len = strlen(string);
          size_t existing_len = strlen(line);
          size_t total_len = insert_len + existing_len;

          // re-alloc the new size
          line = realloc(line, total_len + 1);
          if(!line){
               pthread_mutex_unlock(&buffer->lock);
               return false;
          }

          // figure out where to move from and to
          char* src = ce_utf8_find_index(line, point.x);
          char* dst = src + insert_len;
          size_t src_len = strlen(src);
          memmove(dst, src, src_len);

          // insert the string
          memcpy(src, string, insert_len);

          // tidy up
          line[total_len] = 0;
          buffer->lines[point.y] = line;
          pthread_mutex_unlock(&buffer->lock);
          buffer->status = CE_BUFFER_STATUS_MODIFIED;
          return true;
     }

     int64_t shift_lines = string_lines - 1;
     int64_t old_line_count = buffer->line_count;

     // allocate space to fit the buffer plus the new multiline string
     if(!buffer_realloc_lines(buffer, buffer->line_count + shift_lines)){
          return false;
     }

     // shift down all the line pointers
     int64_t first_new_line = point.y + 1;
     char** src_line = buffer->lines + first_new_line;
     char** dst_line = src_line + shift_lines;
     size_t move_count = old_line_count - first_new_line;
     memmove(dst_line, src_line, move_count * sizeof(src_line));

     // save the last part of the first line to stick on the end of the multiline string
     char* end_string = NULL;
     int64_t end_string_len = strlen(buffer->lines[point.y] + point.x);
     if(end_string_len) end_string = strdup(buffer->lines[point.y] + point.x);

     // insert the first line of the string at the point specified
     const char* next_newline = strchr(string, CE_NEWLINE);
     assert(next_newline);
     size_t first_line_len = next_newline - string;
     size_t new_line_len = point.x + first_line_len;
     buffer->lines[point.y] = realloc(buffer->lines[point.y], new_line_len + 1);
     if(*string != CE_NEWLINE){ // if the first character is a newline, there is no first line of the string
          memcpy(buffer->lines[point.y] + point.x, string, first_line_len);
     }
     buffer->lines[point.y][new_line_len] = 0;

     // copy in each of the new lines
     string = next_newline + 1;
     next_newline = strchr(string, CE_NEWLINE);
     int64_t next_line = point.y + 1;
     while(next_newline){
          new_line_len = next_newline - string;
          buffer->lines[next_line] = calloc(1, new_line_len + 1);
          memcpy(buffer->lines[next_line], string, new_line_len);
          buffer->lines[next_line][new_line_len] = 0;
          string = next_newline + 1;
          next_newline = strchr(string, CE_NEWLINE);
          next_line++;
     }

     // copy in the last line
     new_line_len = strlen(string);
     int64_t last_line_len = new_line_len + end_string_len;
     buffer->lines[next_line] = calloc(1, last_line_len + 1);
     memcpy(buffer->lines[next_line], string, new_line_len);

     // attach the end part of the line we inserted into at the end of the last line
     if(end_string){
          memcpy(buffer->lines[next_line] + new_line_len, end_string, end_string_len);
          free(end_string);
     }
     buffer->lines[next_line][last_line_len] = 0;

     buffer->status = CE_BUFFER_STATUS_MODIFIED;
     return true;
}

bool ce_buffer_insert_rune(CeBuffer_t* buffer, CeRune_t rune, CePoint_t point){
     char str[5];
     int64_t written = 0;
     ce_utf8_encode(rune, str, 5, &written);
     str[written] = 0;
     return ce_buffer_insert_string(buffer, str, point);
}

bool ce_buffer_remove_string(CeBuffer_t* buffer, CePoint_t point, int64_t length, bool remove_line_if_empty){
     if(buffer->status == CE_BUFFER_STATUS_READONLY) return false;
     if(!ce_buffer_point_is_valid(buffer, point)) return false;

     int64_t length_left_on_line = ce_utf8_strlen(buffer->lines[point.y] + point.x);
     if(length == 0){
          if(length_left_on_line == 0){
               // perform a join with the next line
               int64_t next_line_index = point.y + 1;
               if(next_line_index > buffer->line_count) return false;
               int64_t cur_line_len = strlen(buffer->lines[point.y]);
               int64_t next_line_len = strlen(buffer->lines[next_line_index]);
               int64_t new_line_len = next_line_len + cur_line_len;
               buffer->lines[point.y] = realloc(buffer->lines[point.y], new_line_len + 1);
               strncpy(buffer->lines[point.y] + cur_line_len, buffer->lines[next_line_index], next_line_len);
               buffer->lines[point.y][new_line_len] = 0;
               buffer->status = CE_BUFFER_STATUS_MODIFIED;
               return ce_buffer_remove_lines(buffer, next_line_index, 1);
          }else if(point.x == 0){
               // perform a join with the previous line
               int64_t prev_line_index = point.y - 1;
               if(prev_line_index < 0) return false;
               int64_t cur_line_len = strlen(buffer->lines[point.y]);
               int64_t prev_line_len = strlen(buffer->lines[prev_line_index]);
               int64_t new_line_len = prev_line_len + cur_line_len;
               buffer->lines[prev_line_index] = realloc(buffer->lines[prev_line_index], new_line_len + 1);
               strncpy(buffer->lines[prev_line_index] + prev_line_len, buffer->lines[point.y], cur_line_len);
               buffer->lines[prev_line_index][new_line_len] = 0;
               buffer->status = CE_BUFFER_STATUS_MODIFIED;
               return ce_buffer_remove_lines(buffer, point.y, 1);
          }

          assert(!"we should never get here");
     }

     if(length_left_on_line > length){
          // case: glue together left and right sides and cut out the middle
          char* end_of_start = ce_utf8_find_index(buffer->lines[point.y], point.x);
          assert(end_of_start);
          char* beginning_of_end = ce_utf8_find_index(buffer->lines[point.y], point.x + length);
          assert(beginning_of_end);

          // figure out how big of a line to allocate
          size_t start_line_len = end_of_start - buffer->lines[point.y];
          size_t end_line_len = strlen(beginning_of_end);
          size_t full_line_len = start_line_len + end_line_len;
          char* new_line = calloc(full_line_len + 1, sizeof(*new_line));
          if(!new_line) return false;

          // copy over the data to our new line
          memcpy(new_line, buffer->lines[point.y], start_line_len);
          memcpy(new_line + start_line_len, beginning_of_end, end_line_len);
          new_line[full_line_len] = 0;

          // free and overwrite our new line
          free(buffer->lines[point.y]);
          buffer->lines[point.y] = new_line;

          buffer->status = CE_BUFFER_STATUS_MODIFIED;
          return true;
     }else if(length_left_on_line == length){
          // case: cut the line, keeping only the left side
          if(point.x == 0 && remove_line_if_empty){
               // remove the empty line from the buffer lines
               char** dst_line = buffer->lines + point.y;
               char** src_line = dst_line + 1;
               int64_t lines_to_shift = buffer->line_count - (point.y + 1);
               free(buffer->lines[point.y]);
               memmove(dst_line, src_line, lines_to_shift * sizeof(dst_line));
               if(!buffer_realloc_lines(buffer, buffer->line_count - 1)){
                    return false;
               }
          }else{
               // re-alloc for just the first part of the line, even if it is 0 len
               buffer->lines[point.y] = realloc(buffer->lines[point.y], point.x + 1);
               buffer->lines[point.y][point.x] = 0;
          }

          buffer->status = CE_BUFFER_STATUS_MODIFIED;
          return true;
     }

     // case: cut the end of the initial line, N lines in the middle and N leftover characters in the final
     //       line, and join the remaining characters in the last line to the initial line
     int64_t length_left = length - length_left_on_line;
     int64_t current_line = point.y + 1;
     // check whether we should delete the whole first line, or start at the next line
     int64_t save_current_line = (point.x == 0) ? point.y : current_line;
     int64_t line_len = 0;
     int64_t last_line_offset = 0;

     // how many lines do we have to delete?
     for(; current_line < buffer->line_count; current_line++){
          line_len = ce_utf8_strlen(buffer->lines[current_line]);
          if(line_len == 0) line_len = 1;

          if(length_left >= line_len){
               length_left -= line_len;
          }else{
               last_line_offset = length_left;
               length_left = 0;
               break;
          }
     }

     int64_t lines_to_delete = current_line - point.y;

     // join the rest of the last line in the deletion, to the first line
     if(last_line_offset){
          char* end_to_join = ce_utf8_find_index(buffer->lines[current_line], last_line_offset);
          int64_t join_len = strlen(end_to_join);
          int64_t new_len = point.x + join_len;
          buffer->lines[point.y] = realloc(buffer->lines[point.y], new_len + 1);
          memcpy(buffer->lines[point.y] + point.x, end_to_join, join_len);
          buffer->lines[point.y][new_len] = 0;
     }

     // remove the intermediate lines
     return ce_buffer_remove_lines(buffer, save_current_line, lines_to_delete);
}

bool ce_buffer_remove_lines(CeBuffer_t* buffer, int64_t line_start, int64_t lines_to_remove){
     // check invalid input
     if(line_start < 0) return false;
     if(line_start >= buffer->line_count) return false;
     if(lines_to_remove <= 0) return false;
     if(line_start + lines_to_remove > buffer->line_count) return false;

     // free lines we are going to remove and overwrite
     for(int64_t i = line_start; i < line_start + lines_to_remove; i++){
          free(buffer->lines[i]);
     }

     // shift lines down, overwriting lines we want to remove
     int64_t last_line_to_shift = buffer->line_count - lines_to_remove;
     for(int64_t dst = line_start; dst < last_line_to_shift; dst++){
          int64_t src = dst + lines_to_remove;
          buffer->lines[dst] = buffer->lines[src];
     }

     // update line count, and shrink our allocation
     buffer->line_count -= lines_to_remove;
     buffer->lines = realloc(buffer->lines, buffer->line_count * sizeof(*buffer->lines));

     buffer->status = CE_BUFFER_STATUS_MODIFIED;
     return buffer->lines != NULL;
}

char* ce_buffer_dupe_string(CeBuffer_t* buffer, CePoint_t point, int64_t length, bool newline_if_entire_line){
     if(!ce_buffer_point_is_valid(buffer, point)) return NULL;

     char* start = ce_utf8_find_index(buffer->lines[point.y], point.x);
     // NOTE: would a function that returns both utf8 len and byte len be helpful here?
     int64_t buffer_utf8_length = ce_utf8_strlen(start);
     int64_t real_length = strlen(start);

     // exit early if the whole string is just on this line
     if(buffer_utf8_length > length){
          char* end = ce_utf8_find_index(start, length);
          return strndup(start, end - start);
     }else if(buffer_utf8_length == length){
          if(newline_if_entire_line){
               // copy the entire line, with a newline at the end
               char* line = malloc(real_length + 2);
               strncpy(line, start, real_length);
               line[real_length] = CE_NEWLINE;
               line[real_length + 1] = 0;
               return line;
          }

          return strndup(start, real_length);
     }

     real_length++; // account for newline

     // calculate how big of an array we need to allocate for the dupe
     int64_t current_line = point.y + 1;
     if(current_line >= buffer->line_count) return NULL;

     while(true){
          int64_t line_utf8_length = ce_utf8_strlen(buffer->lines[current_line]);
          if(line_utf8_length == 0) line_utf8_length = 1; // treat empty lines as taking up 1 character
          buffer_utf8_length += line_utf8_length;
          if(buffer_utf8_length > length){
               int64_t diff = buffer_utf8_length - length;
               char* end_of_dupe = ce_utf8_find_index(buffer->lines[current_line], line_utf8_length - diff);
               real_length += end_of_dupe - buffer->lines[current_line];
               break;
          }

          real_length += strlen(buffer->lines[current_line]);
          if(buffer_utf8_length == length){
               if(newline_if_entire_line) real_length++; // account for newline
               break;
          }
          real_length++; // account for newline
          current_line++;
          if(current_line > buffer->line_count) return NULL; // not enough length in the buffer
     }

     // alloc
     char* dupe = malloc(real_length + 1);
     char* itr = dupe;

     // copy in the first line
     int64_t copy_length = strlen(start);
     memcpy(itr, start, copy_length);
     itr += copy_length;

     if(copy_length < real_length){
          // append newline
          *itr = CE_NEWLINE;
          itr++;
          copy_length++; // account for newline

          // loop over each line again from the beginning
          current_line = point.y + 1;
          while(true){
               int64_t line_length = strlen(buffer->lines[current_line]);
               copy_length += line_length;

               // just copy in the rest of the characters
               if(copy_length > real_length){
                    int64_t diff = copy_length - real_length;
                    memcpy(itr, buffer->lines[current_line], line_length - diff);
                    break;
               }

               // copy in the whole line
               memcpy(itr, buffer->lines[current_line], line_length);
               itr += line_length;

               // append a newline
               *itr = CE_NEWLINE;
               itr++;
               copy_length++;
               if(copy_length >= real_length) break;
               current_line++;
          }

          if(dupe[real_length - 1] == CE_NEWLINE && !newline_if_entire_line) dupe[real_length - 1] = 0;
     }

     dupe[real_length] = 0;
     return dupe;
}

char* ce_buffer_dupe(CeBuffer_t* buffer){
     CePoint_t start = {0, 0};
     CePoint_t end = {buffer->line_count, 0};
     if(end.y) end.y--;
     end.x = ce_utf8_last_index(buffer->lines[end.y]);
     int64_t len = ce_buffer_range_len(buffer, start, end);
     if(len > 0) return ce_buffer_dupe_string(buffer, start, len, false);
     return NULL;
}

bool ce_buffer_change(CeBuffer_t* buffer, CeBufferChange_t* change){
     CeBufferChangeNode_t* node = calloc(1, sizeof(*node));
     node->change = *change;
     node->next = NULL;

     if(buffer->change_node){
          if(buffer->change_node->next){
               ce_buffer_change_node_free(&buffer->change_node->next);
          }

          node->prev = buffer->change_node;
          buffer->change_node->next = node;
     }else{
          CeBufferChangeNode_t* first_empty_node = calloc(1, sizeof(*node));
          first_empty_node->next = node;
          node->prev = first_empty_node;
          if(buffer->save_at_change_node == NULL) buffer->save_at_change_node = first_empty_node;
     }

     buffer->change_node = node;
     return true;
}

bool ce_buffer_undo(CeBuffer_t* buffer, CePoint_t* cursor){
     // nothing to undo
     if(!buffer->change_node) return true;
     if(!buffer->change_node->prev) return true;

     CeBufferChange_t* change = &buffer->change_node->change;
     if(change->insertion){
          ce_buffer_remove_string(buffer, change->location, ce_utf8_insertion_strlen(change->string), change->remove_line_if_empty);
     }else{
          ce_buffer_insert_string(buffer, change->string, change->location);
     }

     *cursor = change->cursor_before;
     buffer->change_node = buffer->change_node->prev;

     if(change->chain) return ce_buffer_undo(buffer, cursor);

     if(buffer->status == CE_BUFFER_STATUS_MODIFIED && buffer->change_node == buffer->save_at_change_node){
          buffer->status = CE_BUFFER_STATUS_NONE;
     }

     return true;
}

bool ce_buffer_redo(CeBuffer_t* buffer, CePoint_t* cursor){
     // nothing to redo
     if(!buffer->change_node) return false;
     if(!buffer->change_node->next) return false;

     buffer->change_node = buffer->change_node->next;

     CeBufferChange_t* change = &buffer->change_node->change;
     if(change->insertion){
          ce_buffer_insert_string(buffer, change->string, change->location);
     }else{
          ce_buffer_remove_string(buffer, change->location, ce_utf8_insertion_strlen(change->string), change->remove_line_if_empty);
     }

     *cursor = change->cursor_after;

     if(buffer->change_node->next && buffer->change_node->next->change.chain) return ce_buffer_redo(buffer, cursor);

     if(buffer->status == CE_BUFFER_STATUS_MODIFIED && buffer->change_node == buffer->save_at_change_node){
          buffer->status = CE_BUFFER_STATUS_NONE;
     }

     return true;
}

void ce_view_follow_cursor(CeView_t* view, int64_t horizontal_scroll_off, int64_t vertical_scroll_off, int64_t tab_width){
     if(!view->buffer) return;

     int64_t view_height = (view->rect.bottom - view->rect.top);
     int64_t scroll_left = view->scroll.x + horizontal_scroll_off;
     int64_t scroll_top = view->scroll.y + vertical_scroll_off;
     int64_t scroll_right = view->scroll.x + (view->rect.right - view->rect.left) - horizontal_scroll_off;
     int64_t scroll_bottom = view->scroll.y + view_height - vertical_scroll_off;

     int64_t visible_index = 0;
     if(ce_buffer_point_is_valid(view->buffer, view->cursor)){
          visible_index = ce_util_string_index_to_visible_index(view->buffer->lines[view->cursor.y],
                                                                view->cursor.x, tab_width);
     }

     pthread_mutex_lock(&view->buffer->lock);

     if(visible_index < scroll_left){
          view->scroll.x -= (scroll_left - visible_index);
          if(view->scroll.x < 0) view->scroll.x = 0;
     }else if(visible_index > scroll_right){
          view->scroll.x += (visible_index - scroll_right);
     }

     if(view->cursor.y < scroll_top){
          view->scroll.y -= (scroll_top - view->cursor.y);
          if(view->scroll.y < 0) view->scroll.y = 0;
     }else if(view->cursor.y > scroll_bottom){
          view->scroll.y += (view->cursor.y - scroll_bottom);
     }

     int64_t max_scroll_y = (view->buffer->line_count - view_height);
     if(max_scroll_y < 0) max_scroll_y = 0;
     CE_CLAMP(view->scroll.y, 0, max_scroll_y);

     pthread_mutex_unlock(&view->buffer->lock);
}

void ce_view_scroll_to(CeView_t* view, CePoint_t point){
     view->scroll = point;
     if(view->scroll.x < 0) view->scroll.x = 0;
     if(view->scroll.y < 0) view->scroll.y = 0;
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

int64_t ce_utf8_insertion_strlen(const char* string){
     int64_t len = 0;
     int64_t byte_count = 0;
     char last_char = 0;;

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

          // if we see a newline, don't count it, but if we see 2 newlins in a row, count it
          if(*string != CE_NEWLINE ||
             (*string == CE_NEWLINE && last_char == CE_NEWLINE)) len++;
          last_char = *string;

          // validate string doesn't early terminate
          for(int64_t i = 0; i < byte_count; i++){
               if(*string == 0) return -1;
               string++;
          }
     }

     return len;
}

int64_t ce_utf8_strlen_between(const char* start, const char* end){
     int64_t len = 0;
     int64_t byte_count = 0;

     while(start <= end){
          if((*start & 0x80) == 0){
               byte_count = 1;
          }else if((*start & 0xE0) == 0xC0){
               byte_count = 2;
          }else if((*start & 0xF0) == 0xE0){
               byte_count = 3;
          }else if((*start & 0xF8) == 0xF0){
               byte_count = 4;
          }else{
               return -1;
          }

          // validate string doesn't early terminate
          for(int64_t i = 0; i < byte_count; i++){
               if(*start == 0) return -1;
               start++;
          }

          len++;
     }

     return len;

}

int64_t ce_utf8_last_index(const char* string){
     int64_t len = ce_utf8_strlen(string);
     if(len > 0) len--;
     return len;
}

char* ce_utf8_find_index(char* string, int64_t index){
     int64_t bytes = 0;
     while(index){
          if((*string & 0x80) == 0){
               bytes = 1;
          }else if((*string & 0xE0) == 0xC0){
               bytes = 2;
          }else if((*string & 0xF0) == 0xE0){
               bytes = 3;
          }else if((*string & 0xF8) == 0xF0){
               bytes = 4;
          }else{
               return NULL;
          }

          for(int64_t i = 0; i < bytes; ++i){
               if(*string == 0) return NULL;
               string++;
          }

          index--;
     }

     return string;
}

CeRune_t ce_utf8_decode(const char* string, int64_t* bytes_consumed){
     CeRune_t rune;

     // 0xxxxxxx is just ascii
     if((*string & 0x80) == 0){
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

CeRune_t ce_utf8_decode_reverse(const char* string, const char* string_start, int64_t* bytes_consumed){
     if((*string & 0x80) == 0){
          *bytes_consumed = 1;
          return string[0];
     }

     while(string >= string_start){
          if((*string & 0x80) == 0x80){
               string--;
          }else{
               return ce_utf8_decode(string, bytes_consumed);
          }
     }

     return CE_UTF8_INVALID;
}

bool ce_utf8_encode(CeRune_t u, char* string, int64_t string_len, int64_t* bytes_written){
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

     // TODO: do we need this?
     // one line files usually contain newlines at the end
     // if(line_count == 2 && string[string_length-1] == CE_NEWLINE){
          // line_count--;
     // }

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

bool ce_point_after(CePoint_t a, CePoint_t b){
     return b.y < a.y || (b.y == a.y && b.x < a.x);
}

bool ce_points_equal(CePoint_t a, CePoint_t b){
     return (a.x == b.x) && (a.y == b.y);
}

bool ce_point_in_rect(CePoint_t a, CeRect_t r){
     return (a.x >= r.left && a.x <= r.right &&
             a.y >= r.top && a.y <= r.bottom);
}

bool ce_rune_node_insert(CeRuneNode_t** head, CeRune_t rune){
     CeRuneNode_t* node = malloc(sizeof(*node));
     if(!node) return false;

     // always insert at head
     node->rune = rune;
     node->next = *head;
     *head = node;

     return true;
}

CeRune_t* ce_rune_node_string(CeRuneNode_t* head){
     // count nodes
     CeRuneNode_t* itr = head;
     int64_t len = 0;
     while(itr){
          itr = itr->next;
          len++;
     }

     CeRune_t* runes = malloc((len + 1) * sizeof(*runes));

     // copy into string
     int64_t save_len = len;
     itr = head;
     while(itr){
          runes[len - 1] = itr->rune;
          itr = itr->next;
          len--;
     }

     // null terminate
     runes[save_len] = 0;
     return runes;
}

void ce_rune_node_free(CeRuneNode_t** head){
     CeRuneNode_t* itr = *head;
     while(itr){
          CeRuneNode_t* tmp = itr;
          itr = itr->next;
          free(tmp);
     }

     *head = NULL;
}
