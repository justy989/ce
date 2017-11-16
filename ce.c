#include "ce.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <ctype.h>
#include <ncurses.h>
#include <sys/stat.h>

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

char g_log_string[BUFSIZ];

void ce_log(const char* fmt, ...){
     va_list args;
     va_start(args, fmt);
     size_t string_len = vsnprintf(g_log_string, BUFSIZ, fmt, args);
     va_end(args);

     fwrite(g_log_string, string_len, 1, g_ce_log);
     CePoint_t end = ce_buffer_end_point(g_ce_log_buffer);
     g_ce_log_buffer->status = CE_BUFFER_STATUS_NONE;
     ce_buffer_insert_string(g_ce_log_buffer, g_log_string, end);
     g_ce_log_buffer->status = CE_BUFFER_STATUS_READONLY;
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
     return true;
}

void ce_buffer_free(CeBuffer_t* buffer){
     for(int64_t i = 0; i < buffer->line_count; i++){
          free(buffer->lines[i]);
     }

     free(buffer->lines);
     free(buffer->name);
     if(buffer->change_node) ce_buffer_change_node_free(&buffer->change_node);

     memset(buffer, 0, sizeof(*buffer));
}

bool ce_buffer_load_file(CeBuffer_t* buffer, const char* filename){
     struct stat statbuf;
     if (stat(filename, &statbuf) != 0) return false;
     if(S_ISDIR(statbuf.st_mode)){
          errno = EPERM;
          return false;
     }

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
     buffer->lines[0] = malloc(sizeof(buffer->lines[0]));
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

     char* str = ce_utf8_iterate_to(buffer->lines[point.y], point.x);
     int64_t rune_len = 0;
     return ce_utf8_decode(str, &rune_len);
}

CePoint_t ce_buffer_search_forward(CeBuffer_t* buffer, CePoint_t start, const char* pattern){
     CePoint_t result = (CePoint_t){-1, -1};

     if(!ce_buffer_point_is_valid(buffer, start)) return result;

     int64_t save_y = start.y;
     char* itr = ce_utf8_iterate_to(buffer->lines[start.y], start.x);
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
     char* itr = ce_utf8_iterate_to(beginning_of_line, start.x);
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
               length = ce_utf8_strlen(ce_utf8_iterate_to(buffer->lines[y], start.x)) + 1;
          }else if(y == end.y){
               length += end.x + 1;
          }else{
               // count entire line
               int64_t line_length = ce_utf8_strlen(buffer->lines[y]) + 1;
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
                    if(point.x == 0){
                         delta += 1;
                    }else{
                         delta += point.x;
                    }
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
               CE_CLAMP(point.x, 0, line_len);
          }else{
               point.x = 0;
               point.y = 0;
          }
          break;
     case CE_CLAMP_X_INSIDE:
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
     }

     return point;
}

CePoint_t ce_buffer_end_point(CeBuffer_t* buffer){
     CePoint_t point = {0, buffer->line_count};
     if(point.y > 0){
          point.y--;
          point.x = ce_utf8_last_index(buffer->lines[point.y]);
     }
     return point;
}

bool ce_buffer_insert_string(CeBuffer_t* buffer, const char* string, CePoint_t point){
     if(buffer->status == CE_BUFFER_STATUS_READONLY) return false;

     if(!ce_buffer_point_is_valid(buffer, point)){
          if(buffer->line_count == 0 && ce_points_equal(point, (CePoint_t){0, 0})){
               int64_t line_count = ce_util_count_string_lines(string);
               buffer_realloc_lines(buffer, line_count);
          }else if(point.y == buffer->line_count && point.x == 0){
               // allow inserting a string after a buffer by resizing
               if(!buffer_realloc_lines(buffer, buffer->line_count + 1)) return false;
               buffer->lines[point.y] = calloc(1, 1); // allocate an empty string
          }else{
               return false;
          }
     }

     int64_t string_lines = ce_util_count_string_lines(string);
     if(string_lines == 0){
          return true; // sure, yeah, we inserted that empty string
     }else if(string_lines == 1){
          char* line = buffer->lines[point.y];
          size_t insert_len = strlen(string);
          size_t existing_len = strlen(line);
          size_t total_len = insert_len + existing_len;

          // re-alloc the new size
          line = realloc(line, total_len + 1);
          if(!line) return false;

          // figure out where to move from and to
          char* src = ce_utf8_iterate_to(line, point.x);
          char* dst = src + insert_len;
          size_t src_len = strlen(src);
          memmove(dst, src, src_len);

          // insert the string
          memcpy(src, string, insert_len);

          // tidy up
          line[total_len] = 0;
          buffer->lines[point.y] = line;
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

bool ce_buffer_remove_string(CeBuffer_t* buffer, CePoint_t point, int64_t length){
     if(buffer->status == CE_BUFFER_STATUS_READONLY) return false;
     if(!ce_buffer_point_is_valid(buffer, point)) return false;

     char* first_line_start = ce_utf8_iterate_to(buffer->lines[point.y], point.x);
     int64_t length_left_on_line = ce_utf8_strlen(first_line_start) + 1;

     if(length_left_on_line > length){
          // case: glue together left and right sides and cut out the middle
          char* end_of_start = ce_utf8_iterate_to(buffer->lines[point.y], point.x);
          assert(end_of_start);
          char* beginning_of_end = ce_utf8_iterate_to(buffer->lines[point.y], point.x + length);
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
          if(point.x == 0){
               buffer->status = CE_BUFFER_STATUS_MODIFIED;
               return ce_buffer_remove_lines(buffer, point.y, 1);
          }

          // remove characters left on current line
          int64_t keep_length = (first_line_start - buffer->lines[point.y]);
          buffer->lines[point.y] = realloc(buffer->lines[point.y], keep_length + 1);
          buffer->lines[point.y][keep_length] = 0;

          // perform a join with the next line
          int64_t next_line_index = point.y + 1;
          if(next_line_index > buffer->line_count) return false;
          if(next_line_index < buffer->line_count){
               int64_t cur_line_len = strlen(buffer->lines[point.y]);
               int64_t next_line_len = strlen(buffer->lines[next_line_index]);
               int64_t new_line_len = next_line_len + cur_line_len;
               buffer->lines[point.y] = realloc(buffer->lines[point.y], new_line_len + 1);
               strncpy(buffer->lines[point.y] + cur_line_len, buffer->lines[next_line_index], next_line_len);
               buffer->lines[point.y][new_line_len] = 0;
          }

          buffer->status = CE_BUFFER_STATUS_MODIFIED;
          return ce_buffer_remove_lines(buffer, next_line_index, 1);
     }

     // case: cut the end of the initial line, N lines in the middle and N leftover characters in the final
     //       line, and join the remaining characters in the last line to the initial line
     int64_t length_left = length - length_left_on_line;
     int64_t current_line = point.y + 1;
     int64_t save_current_line = current_line;
     int64_t line_len = 0;
     int64_t last_line_offset = 0;

     // how many lines do we have to delete?
     for(; current_line < buffer->line_count; current_line++){
          line_len = ce_utf8_strlen(buffer->lines[current_line]) + 1;

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
          char* end_to_join = ce_utf8_iterate_to(buffer->lines[current_line], last_line_offset);
          int64_t join_len = strlen(end_to_join);
          int64_t new_len = point.x + join_len;
          buffer->lines[point.y] = realloc(buffer->lines[point.y], new_len + 1);
          memcpy(buffer->lines[point.y] + point.x, end_to_join, join_len);
          buffer->lines[point.y][new_len] = 0;
     }else{
          // if we aren't doing a join, then start with deleting the first line
          save_current_line--;
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
     if(buffer->line_count > 0){
          buffer->lines = realloc(buffer->lines, buffer->line_count * sizeof(*buffer->lines));
     }else{
          ce_buffer_empty(buffer);
     }

     buffer->status = CE_BUFFER_STATUS_MODIFIED;
     return buffer->lines != NULL;
}

char* ce_buffer_dupe_string(CeBuffer_t* buffer, CePoint_t point, int64_t length){
     if(!ce_buffer_point_is_valid(buffer, point)) return NULL;

     char* start = ce_utf8_iterate_to(buffer->lines[point.y], point.x);
     // NOTE: would a function that returns both utf8 len and byte len be helpful here?
     int64_t buffer_utf8_length = ce_utf8_strlen(start) + 1;
     int64_t real_length = strlen(start) + 1;

     // exit early if the whole string is just on this line
     if(buffer_utf8_length > length){
          char* end = ce_utf8_iterate_to(start, length);
          return strndup(start, end - start);
     }else if(buffer_utf8_length == length){
          char* new_string = malloc(real_length + 1);
          strncpy(new_string, start, real_length - 1);
          new_string[real_length - 1] = CE_NEWLINE;
          new_string[real_length] = 0;
          return new_string;
     }

     // calculate how big of an array we need to allocate for the dupe
     int64_t current_line = point.y + 1;

     // this means we asked for a string passed the length of the buffer, starting at the end, just return an empty string
     if(current_line >= buffer->line_count) return strdup("");

     while(true){
          int64_t line_utf8_length = ce_utf8_strlen(buffer->lines[current_line]) + 1;
          buffer_utf8_length += line_utf8_length;
          if(buffer_utf8_length > length){
               int64_t diff = buffer_utf8_length - length;
               char* end_of_dupe = ce_utf8_iterate_to(buffer->lines[current_line], line_utf8_length - diff);
               real_length += end_of_dupe - buffer->lines[current_line];
               break;
          }

          real_length += strlen(buffer->lines[current_line]) + 1;
          if(buffer_utf8_length == length) break;
          current_line++;
          if(current_line >= buffer->line_count) return NULL; // not enough length in the buffer
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
          while(copy_length < real_length){
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
               current_line++;
          }
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
     if(len > 0) return ce_buffer_dupe_string(buffer, start, len);
     return NULL;
}

bool ce_buffer_insert_string_change(CeBuffer_t* buffer, char* alloced_string, CePoint_t point, CePoint_t* cursor_before,
                                    CePoint_t cursor_after, bool chain_undo){
     if(!ce_buffer_insert_string(buffer, alloced_string, point)) return false;

     CeBufferChange_t change = {};
     change.chain = chain_undo;
     change.insertion = true;
     change.string = alloced_string;
     change.location = point;
     change.cursor_before = *cursor_before;
     change.cursor_after = cursor_after;
     ce_buffer_change(buffer, &change);

     *cursor_before = cursor_after;
     return true;
}

bool ce_buffer_insert_string_change_at_cursor(CeBuffer_t* buffer, char* alloced_string, CePoint_t* cursor, bool chain_undo){
     if(!ce_buffer_insert_string(buffer, alloced_string, *cursor)) return false;
     CePoint_t cursor_after = ce_buffer_advance_point(buffer, *cursor, strlen(alloced_string));

     CeBufferChange_t change = {};
     change.chain = chain_undo;
     change.insertion = true;
     change.string = alloced_string;
     change.location = *cursor;
     change.cursor_before = *cursor;
     change.cursor_after = cursor_after;
     ce_buffer_change(buffer, &change);

     *cursor = cursor_after;
     return true;
}

bool ce_buffer_remove_string_change(CeBuffer_t* buffer, CePoint_t point, int64_t remove_len, CePoint_t* cursor_before,
                                    CePoint_t cursor_after, bool chain_undo){
     char* remove_string = ce_buffer_dupe_string(buffer, point, remove_len);
     if(!ce_buffer_remove_string(buffer, point, remove_len)){
          free(remove_string);
          return false;
     }

     CeBufferChange_t change = {};
     change.chain = chain_undo;
     change.insertion = false;
     change.string = remove_string;
     change.location = point;
     change.cursor_before = *cursor_before;
     change.cursor_after = cursor_after;
     ce_buffer_change(buffer, &change);

     *cursor_before = cursor_after;
     return true;
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
          ce_buffer_remove_string(buffer, change->location, ce_utf8_strlen(change->string));
     }else{
          ce_buffer_insert_string(buffer, change->string, change->location);
     }

     *cursor = change->cursor_before;
     buffer->change_node = buffer->change_node->prev;

     bool success = true;
     if(change->chain) success = ce_buffer_undo(buffer, cursor);

     if(buffer->status == CE_BUFFER_STATUS_MODIFIED && buffer->change_node == buffer->save_at_change_node){
          buffer->status = CE_BUFFER_STATUS_NONE;
     }

     return success;
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
          ce_buffer_remove_string(buffer, change->location, ce_utf8_strlen(change->string));
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

     int64_t max_scroll_y = ((view->buffer->line_count - 1) + view_height);
     if(max_scroll_y < 0) max_scroll_y = 0;
     CE_CLAMP(view->scroll.y, 0, max_scroll_y);
}

void ce_view_scroll_to(CeView_t* view, CePoint_t point){
     view->scroll = point;
     if(view->scroll.x < 0) view->scroll.x = 0;
     if(view->scroll.y < 0) view->scroll.y = 0;
}

void ce_view_center(CeView_t* view){
     int64_t view_height = view->rect.bottom - view->rect.top;
     ce_view_scroll_to(view, (CePoint_t){0, view->cursor.y - (view_height / 2)});
}

int64_t ce_view_width(CeView_t* view){
     return view->rect.right - view->rect.left;
}

int64_t ce_view_height(CeView_t* view){
     return view->rect.bottom - view->rect.top;
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

char* ce_utf8_iterate_to(char* string, int64_t index){
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

char* ce_utf8_iterate_to_include_end(char* string, int64_t index){
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

          index--;

          for(int64_t i = 0; i < bytes; ++i){
               if(*string == 0){
                    if(index == 0) return string;
                    return NULL;
               }
               string++;
          }
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

int64_t ce_utf8_rune_len(CeRune_t u){
     // TODO: optimize based on st's bitmasking
     if(u < 0x80){
          return 1;
     }else if(u < 0x0800){
          return 2;
     }else if(u < 0x10000){
          return 3;
     }else if(u < 0x110000){
          return 4;
     }

     return true;
}

int64_t ce_util_count_string_lines(const char* string){
     int64_t string_length = strlen(string);
     int64_t line_count = 0;
     for(int64_t i = 0; i <= string_length; ++i){
          if(string[i] == CE_NEWLINE || string[i] == 0) line_count++;
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

char* ce_rune_string_to_char_string(const CeRune_t* int_str){
     // build length
     size_t len = 1; // account for NULL terminator
     const int* int_itr = int_str;
     while(*int_itr){
          if(isprint(*int_itr)){
               len++;
          }else{
               switch(*int_itr){
               default:
                    if(*int_itr >= 1 && *int_itr <= 31){
                         len += 3;
                    }else{
                         len++; // going to fill in with '~' for now
                    }
                    break;
               case KEY_BACKSPACE:
               case KEY_ESCAPE:
               case KEY_ENTER:
               case CE_TAB:
               case KEY_UP:
               case KEY_DOWN:
               case KEY_LEFT:
               case KEY_RIGHT:
               case '\\':
                    len += 2;
                    break;
               }
          }

          int_itr++;
     }

     char* char_str = malloc(len);
     if(!char_str) return NULL;

     char* char_itr = char_str;
     int_itr = int_str;
     while(*int_itr){
          if(isprint(*int_itr)){
               *char_itr = *int_itr;
               char_itr++;
          }else{
               switch(*int_itr){
               default:
                    if(*int_itr >= 1 && *int_itr <= 26){
                         *char_itr = '\\'; char_itr++;
                         *char_itr = '^'; char_itr++;
                         *char_itr = 'a' + (*int_itr - 1); char_itr++;
                    }else if(*int_itr == 27){
                         *char_itr = '\\'; char_itr++;
                         *char_itr = '^'; char_itr++;
                         *char_itr = '['; char_itr++;
                    }else if(*int_itr == 28){
                         *char_itr = '\\'; char_itr++;
                         *char_itr = '^'; char_itr++;
                         *char_itr = '\\'; char_itr++;
                    }else if(*int_itr == 29){
                         *char_itr = '\\'; char_itr++;
                         *char_itr = '^'; char_itr++;
                         *char_itr = ']'; char_itr++;
                    }else if(*int_itr == 30){
                         *char_itr = '\\'; char_itr++;
                         *char_itr = '^'; char_itr++;
                         *char_itr = '^'; char_itr++;
                    }else if(*int_itr == 31){
                         *char_itr = '\\'; char_itr++;
                         *char_itr = '^'; char_itr++;
                         *char_itr = '_'; char_itr++;
                    }else{
                         *char_itr = '~';
                         char_itr++;
                    }
                    break;
               case KEY_BACKSPACE:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'b'; char_itr++;
                    break;
               case KEY_ESCAPE:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'e'; char_itr++;
                    break;
               case KEY_ENTER:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'r'; char_itr++;
                    break;
               case CE_TAB:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 't'; char_itr++;
                    break;
               case KEY_UP:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'u'; char_itr++;
                    break;
               case KEY_DOWN:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'd'; char_itr++;
                    break;
               case KEY_LEFT:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'l'; char_itr++;
                    break;
               case KEY_RIGHT:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'i'; char_itr++; // NOTE: not happy with 'i'
                    break;
               case '\\':
                    *char_itr = '\\'; char_itr++;
                    *char_itr = '\\'; char_itr++;
                    break;
               }
          }

          int_itr++;
     }

     char_str[len - 1] = 0;

     return char_str;
}

CeRune_t* ce_char_string_to_rune_string(const char* char_str){
     // we can just use the strlen, and it'll be over allocated because the command string will always be
     // the same size or small than the char string
     size_t str_len = strlen(char_str);

     int* int_str = malloc((str_len + 1) * sizeof(*int_str));
     if(!int_str) return NULL;

     int* int_itr = int_str;
     const char* char_itr = char_str;
     while(*char_itr){
          if(!isprint(*char_itr)){
               free(int_str);
               return NULL;
          }

          if(*char_itr == '\\'){
               char_itr++;
               switch(*char_itr){
               default:
                    free(int_str);
                    return NULL;
               case 'b':
                    *int_itr = KEY_BACKSPACE;
                    break;
               case 'e':
                    *int_itr = KEY_ESCAPE;
                    break;
               case 'r':
                    *int_itr = KEY_ENTER;
                    break;
               case 't':
                    *int_itr = CE_TAB;
                    break;
               case 'u':
                    *int_itr = KEY_UP;
                    break;
               case 'd':
                    *int_itr = KEY_DOWN;
                    break;
               case 'l':
                    *int_itr = KEY_LEFT;
                    break;
               case 'i':
                    *int_itr = KEY_RIGHT;
                    break;
               case '\\':
                    *int_itr = '\\';
                    break;
               case '^':
                    char_itr++;
                    if(*char_itr >= 'a' && *char_itr <= 'z'){
                         *int_itr = (*char_itr - 'a') + 1;
                    }else if(*char_itr == '['){
                         *int_itr = 27;
                    }else if(*char_itr == '\\'){
                         *int_itr = 28;
                    }else if(*char_itr == ']'){
                         *int_itr = 29;
                    }else if(*char_itr == '^'){
                         *int_itr = 30;
                    }else if(*char_itr == '_'){
                         *int_itr = 31;
                    }
                    break;
               }

               char_itr++;
               int_itr++;
          }else{
               *int_itr = *char_itr;
               char_itr++;
               int_itr++;
          }
     }

     *int_itr = 0; // NULL terminate

     return int_str;
}

bool ce_range_sort(CeRange_t* range){
     if(ce_point_after(range->start, range->end)){
          CePoint_t tmp = range->start;
          range->start = range->end;
          range->end = tmp;
          return true;
     }

     return false;
}

int64_t ce_count_digits(int64_t n){
     if(n < 0) n = -n;
     if(n == 0) return 1;

     int count = 0;
     while(n > 0){
          n /= 10;
          count++;
     }

     return count;
}

int64_t ce_line_number_column_width(CeLineNumber_t line_number, int64_t buffer_line_count, int64_t view_top, int64_t view_bottom){
     if(buffer_line_count == 0) return 0;

     int64_t column_width = 0;

     if(line_number == CE_LINE_NUMBER_ABSOLUTE || line_number == CE_LINE_NUMBER_ABSOLUTE_AND_RELATIVE){
          column_width += ce_count_digits(buffer_line_count) + 1;
     }else if(line_number == CE_LINE_NUMBER_RELATIVE){
          int64_t view_height = (view_bottom - view_top) + 1;
          if(view_height > buffer_line_count){
               column_width += ce_count_digits(buffer_line_count - 1) + 1;
          }else{
               column_width += ce_count_digits(view_height - 1) + 1;
          }
     }

     return column_width;
}

CeRune_t ce_ctrl_key(char ch){
     if(isalpha(ch)) return (ch - 'a') + 1;
     return CE_UTF8_INVALID;
}
