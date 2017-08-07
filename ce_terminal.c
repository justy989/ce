#include "ce_terminal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ncurses.h>
#include <assert.h>

#define ELEM_COUNT(static_array) (sizeof(static_array) / sizeof(static_array[0]))
#define CHANGE_BIT(a, set, bit) ((set) ? ((a) |= (bit)) : ((a) &= ~(bit)))
#define BETWEEN(n, min, max) ((min <= n) && (n <= max))

static bool is_controller_c0(CeRune_t rune){
     if(BETWEEN(rune, 0, 0x1f) || rune == '\177'){
          return true;
     }

     return false;
}

static bool is_controller_c1(CeRune_t rune){
     if(BETWEEN(rune, 0x80, 0x9f)){
          return true;
     }

     return false;
}

static bool is_controller(CeRune_t rune){
     if(is_controller_c0(rune)) return true;
     if(is_controller_c1(rune)) return true;

     return false;
}

static void csi_reset(CSIEscape_t* csi){
     memset(csi, 0, sizeof(*csi));
}

static void terminal_control_code(CeTerminal_t* terminal, CeRune_t rune){
     assert(is_controller(rune));

     switch(rune){
     default:
          ce_log("unhandled control code: '%c'\n", rune);
          break;
     case '\t': // HT
          terminal_put_tab(terminal, 1);
          return;
     case '\b': // BS
          terminal_move_cursor_to(terminal, terminal->cursor.x - 1, terminal->cursor.y);
          return;
     case '\r': // CR
          terminal_move_cursor_to(terminal, 0, terminal->cursor.y);
          return;
     case '\f': // LF
     case '\v': // VT
     case '\n': // LF
          terminal_put_newline(terminal, terminal->mode & CE_TERMINAL_MODE_CRLF);
          return;
     case '\a': // BEL
          if(terminal->escape_state & CE_TERMINAL_ESCAPE_STATE_STR_END){
               str_handle(terminal);
          }
          break;
     case '\033': // ESC
          csi_reset(&terminal->csi_escape);
          terminal->escape_state &= ~(CE_TERMINAL_ESCAPE_STATE_CSI | CE_TERMINAL_ESCAPE_STATE_ALTCHARSET | CE_TERMINAL_ESCAPE_STATE_TEST);
          terminal->escape_state |= CE_TERMINAL_ESCAPE_STATE_START;
          return;
     case '\016': // SO
     case '\017': // SI
          // TODO
          break;
     case '\032': // SUB
          terminal_set_glyph(terminal, '?', &terminal->cursor.attributes, terminal->cursor.x, terminal->cursor.y);
     case '\030':
          csi_reset(&terminal->csi_escape);
          break;
     case '\005': // ENQ
     case '\000': // NULL
     case '\021': // XON
     case '\023': // XOFF
     case 0177:   // DEL
          // ignored
          return;
     case 0x80: // PAD
     case 0x81: // HOP
     case 0x82: // BPH
     case 0x83: // NBH
     case 0x84: // IND
          break;
     case 0x85: // NEL
          terminal_put_newline(terminal, true);
          break;
     case 0x86: // SSA
     case 0x87: // ESA
          break;
     case 0x88: // HTS
          terminal->tabs[terminal->cursor.x] = 1;
          break;
     case 0x89: // HTJ
     case 0x8a: // VTS
     case 0x8b: // PLD
     case 0x8c: // PLU
     case 0x8d: // RI
     case 0x8e: // SS2
     case 0x8f: // SS3
     case 0x91: // PU1
     case 0x92: // PU2
     case 0x93: // STS
     case 0x94: // CCH
     case 0x95: // MW
     case 0x96: // SPA
     case 0x97: // EPA
     case 0x98: // SOS
     case 0x99: // SGCI
          break;
     case 0x9a: // DECID
          tty_write(terminal->file_descriptor, CE_TERMINAL_VT_IDENTIFIER, sizeof(CE_TERMINAL_VT_IDENTIFIER) - 1);
          break;
     case 0x9b: // CSI
     case 0x9c: // ST
          break;
     case 0x90: // DCS
     case 0x9d: // OSC
     case 0x9e: // PM
     case 0x9f: // APC
          str_sequence(terminal, rune);
          return;
     }

     terminal->escape_state &= ~(CE_TERMINAL_ESCAPE_STATE_STR_END | CE_TERMINAL_ESCAPE_STATE_STR);
}

static void terminal_put(CeTerminal_t* terminal, Rune_t rune){
     char characters[CE_UTF8_SIZE];
     int width = 1;
     int len = 0;

     if(terminal->mode & CE_TERMINAL_MODE_UTF8){
          utf8_encode(rune, characters, CE_UTF8_SIZE, &len);
     }else{
          characters[0] = rune;
          width = 1;
     }

     if(terminal->escape_state & CE_TERMINAL_ESCAPE_STATE_STR){
          if(rune == '\a' || rune == 030 || rune == 032 || rune == 033 || is_controller_c1(rune)){
               terminal->escape_state &= ~(CE_TERMINAL_ESCAPE_STATE_START | CE_TERMINAL_ESCAPE_STATE_STR |
                                           CE_TERMINAL_ESCAPE_STATE_DCS);
               if(terminal->mode & CE_TERMINAL_MODE_SIXEL){
                    CHANGE_BIT(terminal->mode, 0, CE_TERMINAL_MODE_SIXEL);
                    return;
               }

               terminal->escape_state |= CE_TERMINAL_ESCAPE_STATE_STR_END;
          }else{
               if(terminal->mode & CE_TERMINAL_MODE_SIXEL){
                    return;
               }

               if(terminal->escape_state & CE_TERMINAL_ESCAPE_STATE_DCS && terminal->str_escape.buffer_length == 0 &&
                  rune == 'q'){
                    terminal->mode |= CE_TERMINAL_MODE_SIXEL;
               }

               if(terminal->str_escape.buffer_length + 1 >= (CE_TERMINAL_ESCAPE_BUFFER_SIZE - 1)){
                    return;
               }

               terminal->str_escape.buffer[terminal->str_escape.buffer_length] = rune;
               terminal->str_escape.buffer_length++;
               return;
          }
     }

     if(is_controller(rune)){
          terminal_control_code(terminal, rune);
          return;
     }

     if(terminal->escape_state & CE_TERMINAL_ESCAPE_STATE_START){
          if(terminal->escape_state & CE_TERMINAL_ESCAPE_STATE_CSI){
               CSIEscape_t* csi = &terminal->csi_escape;
               csi->buffer[csi->buffer_length] = rune;
               csi->buffer_length++;

               if(BETWEEN(rune, 0x40, 0x7E) || csi->buffer_length >= (CE_TERMINAL_ESCAPE_BUFFER_SIZE - 1)){
                    terminal->escape_state = 0;
                    csi_parse(csi);
                    csi_handle(terminal);
               }

               return;
          }else if(terminal->escape_state & CE_TERMINAL_ESCAPE_STATE_UTF8){
               if(rune == 'G'){
                    terminal->mode |= CE_TERMINAL_MODE_UTF8;
               }else if(rune == '@'){
                    terminal->mode &= ~CE_TERMINAL_MODE_UTF8;
               }
          }else if(terminal->escape_state & CE_TERMINAL_ESCAPE_STATE_ALTCHARSET){
               // TODO
          }else if(terminal->escape_state & CE_TERMINAL_ESCAPE_STATE_TEST){
               // TODO
          }else{
               if(!esc_handle(terminal, rune)) return;
          }

          terminal->escape_state = 0;
          return;
     }

     Glyph_t* current_glyph = terminal->lines[terminal->cursor.y] + terminal->cursor.x;
     if(terminal->mode & CE_TERMINAL_MODE_WRAP && terminal->cursor.state & CE_TERMINAL_CURSOR_STATE_WRAPNEXT){
          current_glyph->attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_WRAP;
          terminal_put_newline(terminal, true);
          current_glyph = terminal->lines[terminal->cursor.y] + terminal->cursor.x;
     }

     if(terminal->mode & CE_TERMINAL_MODE_INSERT && terminal->cursor.x + width < terminal->columns){
          memmove(current_glyph + width, current_glyph, (terminal->columns - terminal->cursor.x - width) * sizeof(*current_glyph));
     }

     if(terminal->cursor.x + width > terminal->columns){
          terminal_put_newline(terminal, true);
     }

     terminal_set_glyph(terminal, rune, &terminal->cursor.attributes, terminal->cursor.x, terminal->cursor.y);

     if(terminal->cursor.x + width < terminal->columns){
          terminal_move_cursor_to(terminal, terminal->cursor.x + width, terminal->cursor.y);
     }else{
          terminal->cursor.state |= CE_TERMINAL_CURSOR_STATE_WRAPNEXT;
     }
}

static void* tty_reader(void* data)
{
     CeTerminal_t* terminal = (CeTerminal_t*)(data);

     char buffer[BUFSIZ];
     int buffer_length = 0;
     CeRune_t decoded;
     int64_t decoded_length;

     while(true){
          int rc = read(terminal->file_descriptor, buffer, ELEM_COUNT(buffer));

          if(rc < 0){
               ce_log("%s() failed to read from tty file descriptor: '%s'\n", __FUNCTION__, strerror(errno));
               return NULL;
          }

          buffer_length = rc;

          if(buffer_length < BUFSIZ){
               buffer[buffer_length] = 0;
          }else{
               // TODO: do we ever hit this case?
               buffer[BUFSIZ - 1] = 0;
          }

          for(int i = 0; i < buffer_length; ++i){
               decoded = ce_utf8_decode(buffer + i, &decoded_length);
               if(decoded != CE_UTF8_INVALID){
                    terminal_put(terminal, decoded);
                    i += (decoded_length - 1);
               }
          }

          sleep(0);
     }

     return NULL;
}

static bool tty_create(int rows, int columns, pid_t* pid, int* tty_file_descriptor){
     int master_file_descriptor;
     int slave_file_descriptor;
     struct winsize window_size = {rows, columns, 0, 0};

     if(openpty(&master_file_descriptor, &slave_file_descriptor, NULL, NULL, &window_size) < 0){
          ce_log("openpty() failed: '%s'\n", strerror(errno));
          return false;
     }

     switch(*pid = fork()){
     case -1:
          ce_log("fork() failed\n");
          break;
     case 0:
          setsid();

          dup2(slave_file_descriptor, 0);
          dup2(slave_file_descriptor, 1);
          dup2(slave_file_descriptor, 2);

          if(ioctl(slave_file_descriptor, TIOCSCTTY, NULL)){
               ce_log("ioctl() TIOCSCTTY failed: '%s'\n", strerror(errno));
               return false;
          }

          close(slave_file_descriptor);
          close(master_file_descriptor);

          {
               const struct passwd* pw;
               char* shell = getenv("SHELL");
               if(!shell) shell = DEFAULT_SHELL;

               pw = getpwuid(getuid());
               if(pw == NULL){
                    ce_log("getpwuid() failed: '%s'\n", strerror(errno));
                    return false;
               }

               char** args = (char *[]){NULL};

               unsetenv("COLUMNS");
               unsetenv("LINES");
               unsetenv("TERMCAP");
               setenv("LOGNAME", pw->pw_name, 1);
               setenv("USER", pw->pw_name, 1);
               setenv("SHELL", shell, 1);
               setenv("HOME", pw->pw_dir, 1);
               setenv("TERM", TERM_NAME, 1);

               signal(SIGCHLD, SIG_DFL);
               signal(SIGHUP, SIG_DFL);
               signal(SIGINT, SIG_DFL);
               signal(SIGQUIT, SIG_DFL);
               signal(SIGTERM, SIG_DFL);
               signal(SIGALRM, SIG_DFL);

               execvp(shell, args);
               _exit(1);
          }
          break;
     default:
          close(slave_file_descriptor);
          *tty_file_descriptor = master_file_descriptor;
          signal(SIGCHLD, handle_signal_child);
          break;
     }

     return true;
}

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

     // allocate alternate lines
     terminal->alternate_lines = calloc(terminal->rows, sizeof(*terminal->alternate_lines));
     if(!terminal->alternate_lines) return false;

     for(int r = 0; r < terminal->rows; ++r){
          terminal->alternate_lines[r] = calloc(terminal->columns, sizeof(*terminal->alternate_lines[r]));
          if(!terminal->alternate_lines[r]) return false;
     }

     terminal->tabs = calloc(terminal->columns, sizeof(*terminal->tabs));
     terminal->dirty_lines = calloc(terminal->rows, sizeof(*terminal->dirty_lines));
     terminal_reset(terminal);

     if(!tty_create(terminal->rows, terminal->columns, &tty_pid, &tty_file_descriptor)){
          return 1;
     }

     terminal.file_descriptor = tty_file_descriptor;

     int rc = pthread_create(&tty_read_thread, NULL, tty_reader, &terminal);
     if(rc != 0){
          ce_log("pthread_create() failed: '%s'\n", strerror(errno));
          return 1;
     }

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
