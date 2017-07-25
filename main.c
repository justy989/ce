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

void build_buffer_list(CeBuffer_t* buffer, BufferNode_t* head){
     int64_t index = 1;
     char line[256];
     ce_buffer_empty(buffer);
     while(head){
          snprintf(line, 256, "%ld %s %ld", index, head->buffer->name, head->buffer->line_count);
          ce_buffer_append_on_new_line(buffer, line);
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

typedef struct{
     CeView_t* view;
     CeVim_t* vim;
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
