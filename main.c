#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <ncurses.h>
#include <unistd.h>
#include <ctype.h>

#include "ce.h"

// 60 fps
#define DRAW_USEC_LIMIT 16666

typedef struct{
     CeView_t* view;
     int64_t tab_width;
     CePoint_t scroll;
     volatile bool ready_to_draw;
     bool done;
}DrawThreadData_t;

void draw_view(CeView_t* view, int64_t tab_width){
     pthread_mutex_lock(&view->buffer->lock);

     int64_t view_height = view->rect.bottom - view->rect.top;
     int64_t view_width = view->rect.right - view->rect.left;
     int64_t row_min = view->scroll.y;
     int64_t col_min = view->scroll.x;
     int64_t col_max = col_min + view_width;

     char tab_str[tab_width + 1];
     memset(tab_str, ' ', tab_width);
     tab_str[tab_width] = 0;

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

     int64_t visible_cursor_x = 0;
     if(ce_buffer_point_is_valid(view->buffer, view->cursor)){
          visible_cursor_x = ce_util_string_index_to_visible_index(view->buffer->lines[view->cursor.y],
                                                                   view->cursor.x, tab_width);
     }

     mvprintw(view->rect.bottom, 0, "%ld, %ld", view->cursor.x, view->cursor.y);
     move(view->cursor.y - view->scroll.y + view->rect.top,
          visible_cursor_x - view->scroll.x + view->rect.left);
     pthread_mutex_unlock(&view->buffer->lock);
}

void* draw_thread(void* thread_data){
     DrawThreadData_t* data = (DrawThreadData_t*)thread_data;
     struct timeval previous_draw_time;
     struct timeval current_draw_time;
     uint64_t time_since_last_draw = 0;

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
          draw_view(data->view, data->tab_width);
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

     int64_t tab_width = 8;
     int64_t horizontal_scroll_off = 10;
     int64_t vertical_scroll_off = 5;

     int terminal_width;
     int terminal_height;

     CeBuffer_t buffer = {};
     CeView_t view = {};
     view.buffer = &buffer;
     view.scroll.x = 0;
     view.scroll.y = 0;

     if(argc > 1) ce_buffer_load_file(&buffer, argv[1]);

     // init draw thread
     pthread_t thread_draw;
     DrawThreadData_t* draw_thread_data = calloc(1, sizeof(*draw_thread_data));
     {
          draw_thread_data->view = &view;
          draw_thread_data->tab_width = tab_width;
          pthread_create(&thread_draw, NULL, draw_thread, draw_thread_data);
          draw_thread_data->ready_to_draw = true;
     }

     bool chain_undo = false;

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
               if(isprint(key) || key == CE_NEWLINE){
                    if(ce_buffer_insert_char(&buffer, key, view.cursor)){
                         const char str[2] = {key, 0};
                         CePoint_t new_cursor = ce_buffer_advance_point(&buffer, view.cursor, 1);

                         CeBufferChange_t change = {};
                         change.chain = chain_undo;
                         change.insertion = true;
                         change.remove_line_if_empty = true;
                         change.string = strdup(str);
                         change.location = view.cursor;
                         change.cursor_before = view.cursor;
                         change.cursor_after = new_cursor;
                         ce_buffer_change(&buffer, &change);

                         view.cursor = new_cursor;
                         chain_undo = true;
                    }
               }
               break;
          case KEY_CLOSE:
               done = true;
               break;
          case 23: // Ctrl + w
               ce_buffer_save(&buffer);
               break;
          case 14: // Ctrl + n
               ce_buffer_undo(&buffer, &view.cursor);
               break;
          case KEY_REDO:
               ce_buffer_redo(&buffer, &view.cursor);
               break;
          case KEY_BACKSPACE:
               if(!ce_points_equal(view.cursor, (CePoint_t){0, 0})){
                    CePoint_t remove_point = ce_buffer_advance_point(&buffer, view.cursor, -1);
                    char* removed_string = ce_buffer_dupe_string(&buffer, remove_point, 1);
                    if(ce_buffer_remove_string(&buffer, remove_point, 1, true)){
                         CeBufferChange_t change = {};
                         change.chain = chain_undo;
                         change.insertion = false;
                         change.remove_line_if_empty = true;
                         change.string = removed_string;
                         change.location = remove_point;
                         change.cursor_before = view.cursor;
                         change.cursor_after = remove_point;
                         ce_buffer_change(&buffer, &change);

                         view.cursor = remove_point;
                         chain_undo = true;
                    }
               }
               break;
          case KEY_LEFT:
               view.cursor = ce_buffer_move_point(&buffer, view.cursor, (CePoint_t){-1, 0}, tab_width, true);
               draw_thread_data->scroll = ce_view_follow_cursor(&view, horizontal_scroll_off, vertical_scroll_off, tab_width);
               chain_undo = false;
               break;
          case KEY_DOWN:
               view.cursor = ce_buffer_move_point(&buffer, view.cursor, (CePoint_t){0, 1}, tab_width, true);
               draw_thread_data->scroll = ce_view_follow_cursor(&view, horizontal_scroll_off, vertical_scroll_off, tab_width);
               chain_undo = false;
               break;
          case KEY_UP:
               view.cursor = ce_buffer_move_point(&buffer, view.cursor, (CePoint_t){0, -1}, tab_width, true);
               draw_thread_data->scroll = ce_view_follow_cursor(&view, horizontal_scroll_off, vertical_scroll_off, tab_width);
               chain_undo = false;
               break;
          case KEY_RIGHT:
               view.cursor = ce_buffer_move_point(&buffer, view.cursor, (CePoint_t){1, 0}, tab_width, true);
               draw_thread_data->scroll = ce_view_follow_cursor(&view, horizontal_scroll_off, vertical_scroll_off, tab_width);
               chain_undo = false;
               break;
          }

          draw_thread_data->ready_to_draw = true;
     }

     draw_thread_data->done = true;
     pthread_cancel(thread_draw);
     pthread_join(thread_draw, NULL);

     // cleanup
     ce_buffer_free(&buffer);
     endwin();
     return 0;
}
