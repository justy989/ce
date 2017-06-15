#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <locale.h>
#include <sys/time.h>
#include <ncurses.h>

#include "ce.h"

#define DRAW_USEC_LIMIT 16666

typedef struct{
     CeView_t* view;
     bool done;
}DrawThreadData_t;

void draw_view(CeView_t* view)
{
#if 0
     int64_t view_height = view->rect.bottom - view->rect.top;
     int64_t row_min = view->scroll.y;
     int64_t row_max = line_min + view_height;

     for(int64_t i = row_min; i <= row_max; i++){
          int64_t x = 0;
          int64_t rune_len = 0;
          const char* line = data->buffer->lines[y];
          CeRune_t rune = 1;

          while(rune > 0){
               rune = ce_utf8_decode(line, &rune_len);
               if(rune > 0) mvaddch(y, x, rune);
               line += rune_len;
               x++;
          }
     }
#endif
}

void* draw_thread(void* thread_data){
     DrawThreadData_t* data = (DrawThreadData_t*)thread_data;
     struct timeval previous_draw_time;
     struct timeval current_draw_time;
     uint64_t time_since_last_draw;

     while(!data->done){
          gettimeofday(&previous_draw_time, NULL);

          do{
               gettimeofday(&current_draw_time, NULL);
               time_since_last_draw = (current_draw_time.tv_sec - previous_draw_time.tv_sec) * 1000000LL +
                                      (current_draw_time.tv_usec - previous_draw_time.tv_usec);
          }while(time_since_last_draw < DRAW_USEC_LIMIT);

          standend();

          draw_view(data->view);
#if 0
          for(int64_t y = 0; y < data->buffer->line_count; y++){
               int64_t x = 0;
               int64_t rune_len = 0;
               const char* line = data->buffer->lines[y];
               CeRune_t rune = 1;

               while(rune > 0){
                    rune = ce_utf8_decode(line, &rune_len);
                    if(rune > 0) mvaddch(y, x, rune);
                    line += rune_len;
                    x++;
               }
          }
#endif

          refresh();
     }

     return NULL;
}

int main(int argc, char** argv){
     CeBuffer_t buffer = {};
     CeView_t view = {};
     view.buffer = &buffer;
     view.rect.left = 10;
     view.rect.right = 90;
     view.rect.top = 15;
     view.rect.right = 39;
     view.scroll.x = 2;
     view.scroll.y = 5;

     if(argc > 1) ce_buffer_load_file(&buffer, argv[1]);

     setlocale(LC_ALL, "");

     // init ncurses
     {
          initscr();
          keypad(stdscr, TRUE);
          raw();
          cbreak();
          noecho();

          if(has_colors() == FALSE){
               printf("Your terminal doesn't support colors. what year do you live in?\n");
               return -1;
          }

          start_color();
          use_default_colors();
     }

     // init draw thread
     pthread_t thread_draw;
     DrawThreadData_t* draw_thread_data = calloc(1, sizeof(*draw_thread_data));
     {
          draw_thread_data->view = &view;
          pthread_create(&thread_draw, NULL, draw_thread, draw_thread_data);
     }

     bool done = false;
     while(!done){
          int key = getch();
          switch(key){
          default:
               break;
          case 'q':
               done = true;
               break;
          }
     }

     draw_thread_data->done = true;
     pthread_cancel(thread_draw);
     pthread_join(thread_draw, NULL);

     // cleanup
     ce_buffer_free(&buffer);
     return 0;
}
