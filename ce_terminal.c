#include "ce_terminal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ncurses.h>
#include <assert.h>

static void terminal_set_dirt(CeTerminal_t* terminal, int top, int bottom){
     assert(top <= bottom);

     CE_CLAMP(top, 0, terminal->rows - 1);
     CE_CLAMP(bottom, 0, terminal->rows - 1);

     for(int i = top; i <= bottom; ++i){
          terminal->dirty_lines[i] = true;
     }
}

static void terminal_all_dirty(CeTerminal_t* terminal){
     terminal_set_dirt(terminal, 0, terminal->rows - 1);
}

static void terminal_move_cursor_to(CeTerminal_t* terminal, int x, int y){
     int min_y;
     int max_y;

     if(terminal->cursor.state & CE_TERMINAL_CURSOR_STATE_ORIGIN){
          min_y = terminal->top;
          max_y = terminal->bottom;
     }else{
          min_y = 0;
          max_y = terminal->rows - 1;
     }

     terminal->cursor.state &= ~CE_TERMINAL_CURSOR_STATE_WRAPNEXT;
     terminal->cursor.x = CE_CLAMP(x, 0, terminal->columns - 1);
     terminal->cursor.y = CE_CLAMP(y, min_y, max_y);
}

#if 0
static void terminal_move_cursor_to_absolute(CeTerminal_t* terminal, int x, int y){
     terminal_move_cursor_to(terminal, x, y + ((terminal->cursor.state & CURSOR_STATE_ORIGIN) ? terminal->top : 0));
}
#endif

static void terminal_cursor_save(CeTerminal_t* terminal){
     int alt = terminal->mode & CE_TERMINAL_MODE_ALTSCREEN;
     terminal->save_cursor[alt] = terminal->cursor;
}

#if 0
static void terminal_cursor_load(CeTerminal_t* terminal){
     int alt = terminal->mode & CE_TERMINAL_MODE_ALTSCREEN;
     terminal->cursor = terminal->save_cursor[alt];
     terminal_move_cursor_to(terminal, terminal->save_cursor[alt].x, terminal->save_cursor[alt].y);
}
#endif

static void terminal_clear_region(CeTerminal_t* terminal, int left, int top, int right, int bottom){
     // probably going to assert since we are going to trust external data
     if(left > right){
          int tmp = left;
          left = right;
          right = tmp;
     }

     if(top > bottom){
          int tmp = top;
          top = bottom;
          bottom = tmp;
     }

     CE_CLAMP(left, 0, terminal->columns - 1);
     CE_CLAMP(right, 0, terminal->columns - 1);
     CE_CLAMP(top, 0, terminal->rows - 1);
     CE_CLAMP(bottom, 0, terminal->rows - 1);

     for(int y = top; y <= bottom; ++y){
          terminal->dirty_lines[y] = true;

          for(int x = left; x <= right; ++x){
               CeTerminalGlyph_t* glyph = terminal->lines[y] + x;
               glyph->foreground = terminal->cursor.attributes.foreground;
               glyph->background = terminal->cursor.attributes.background;
               glyph->attributes = 0;
               // TODO: set rune in buffer
          }
     }
}

static void terminal_swap_screen(CeTerminal_t* terminal){
     CeTerminalGlyph_t** tmp_lines = terminal->lines;

     terminal->lines = terminal->alternate_lines;
     terminal->alternate_lines = tmp_lines;
     terminal->mode ^= CE_TERMINAL_MODE_ALTSCREEN;
     terminal_all_dirty(terminal);
}

static void terminal_reset(CeTerminal_t* terminal){
     terminal->cursor.attributes.attributes = CE_TERMINAL_GLYPH_ATTRIBUTE_NONE;
     terminal->cursor.attributes.foreground = COLOR_DEFAULT;
     terminal->cursor.attributes.background = COLOR_DEFAULT;
     terminal->cursor.x = 0;
     terminal->cursor.y = 0;
     terminal->cursor.state = CE_TERMINAL_CURSOR_STATE_DEFAULT;

     memset(terminal->tabs, 0, terminal->columns * sizeof(*terminal->tabs));
     for(int i = CE_TERMINAL_TAB_SPACES; i < terminal->columns; ++i) terminal->tabs[i] = 1;
     terminal->top = 0;
     terminal->bottom = terminal->rows - 1;
     terminal->mode = CE_TERMINAL_MODE_WRAP | CE_TERMINAL_MODE_UTF8;

     //TODO: clear character translation table
     terminal->charset = 0;
     terminal->escape_state = 0;

     terminal_move_cursor_to(terminal, 0, 0);
     terminal_cursor_save(terminal);
     terminal_clear_region(terminal, 0, 0, terminal->columns - 1, terminal->rows - 1);
     terminal_swap_screen(terminal);

     terminal_move_cursor_to(terminal, 0, 0);
     terminal_cursor_save(terminal);
     terminal_clear_region(terminal, 0, 0, terminal->columns - 1, terminal->rows - 1);
     terminal_swap_screen(terminal);
}

bool ce_terminal_init(CeTerminal_t* terminal, int64_t width, int64_t height){
     terminal->columns = width;
     terminal->rows = height;
     terminal->bottom = terminal->rows - 1;

     // allocate lines
     terminal->lines = calloc(terminal->rows, sizeof(*terminal->lines));
     if(!terminal->lines) return false;

     for(int r = 0; r < terminal->rows; ++r){
          terminal->lines[r] = calloc(terminal->columns, sizeof(*terminal->lines[r]));
          if(!terminal->lines[r]) return false;

          // default fg and bg
          for(int g = 0; g < terminal->columns; ++g){
               terminal->lines[r][g].foreground = -1;
               terminal->lines[r][g].background = -1;
          }
     }

     terminal->alternate_lines = calloc(terminal->rows, sizeof(*terminal->alternate_lines));
     if(!terminal->alternate_lines) return false;

     for(int r = 0; r < terminal->rows; ++r){
          terminal->alternate_lines[r] = calloc(terminal->columns, sizeof(*terminal->alternate_lines[r]));
          if(!terminal->alternate_lines[r]) return false;
     }

     terminal->tabs = calloc(terminal->columns, sizeof(*terminal->tabs));
     terminal->dirty_lines = calloc(terminal->rows, sizeof(*terminal->dirty_lines));
     terminal_reset(terminal);
     return true;
}

void ce_terminal_free(CeTerminal_t* terminal){
     for(int r = 0; r < terminal->rows; ++r){
          free(terminal->lines[r]);
     }
     free(terminal->lines);
     terminal->lines = NULL;

     for(int r = 0; r < terminal->rows; ++r){
          free(terminal->alternate_lines[r]);
     }
     free(terminal->alternate_lines);
     terminal->alternate_lines = NULL;

     free(terminal->dirty_lines);
     terminal->dirty_lines = NULL;
}

bool ce_terminal_send_key(CeTerminal_t* terminal, CeRune_t key){
     int rc = 0;
     char character = 0;
     char* string = NULL;
     size_t len = 0;
     bool free_string = false;

     string = keybound(key, 0);

     if(!string){
          free_string = false;
          len = 1;

          switch(key){
          default:
               character = key;
               break;
          // damnit curses
          case 10:
               character = 13;
               break;
          }

          string = (char*)(&character);
     }else{
          free_string = true;
          len = strlen(string);
     }

     rc = write(terminal->file_descriptor, string, len);
     if(rc < 0){
          ce_log("%s() write() to terminal failed: %s", __FUNCTION__, strerror(errno));
          return false;
     }

#if 0
     if(terminal->mode & CE_TERMINAL_MODE_ECHO){
          for(int i = 0; i < len; i++){
               terminal_echo(terminal, string[i]);
          }
     }
#endif

     if(free_string) free(string);
     return true;
}
