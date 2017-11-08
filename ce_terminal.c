#include "ce_terminal.h"
#include "ce_app.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ncurses.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>

#define ELEM_COUNT(static_array) (sizeof(static_array) / sizeof(static_array[0]))
#define DEFAULT(a, value) (a = (a == 0) ? value : a)
#define BETWEEN(n, min, max) ((min <= n) && (n <= max))
#define CHANGE_BIT(a, set, bit) ((set) ? ((a) |= (bit)) : ((a) &= ~(bit)))

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

static void csi_reset(CeTerminalCSIEscape_t* csi){
     memset(csi, 0, sizeof(*csi));
}

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
     CE_CLAMP(top, -terminal->start_line, terminal->rows - 1);
     CE_CLAMP(bottom, -terminal->start_line, terminal->rows - 1);
     int width = (right - left) + 1;

     for(int y = top + terminal->start_line; y <= bottom + terminal->start_line; ++y){
          for(int x = left; x <= right; ++x){
               CeTerminalGlyph_t* glyph = terminal->lines[y] + x;
               glyph->foreground = terminal->cursor.attributes.foreground;
               glyph->background = terminal->cursor.attributes.background;
               glyph->attributes = 0;
          }

          char* start = ce_utf8_iterate_to(terminal->buffer->lines[y], left);
          char* end = ce_utf8_iterate_to(terminal->buffer->lines[y], right);
          int64_t length = ce_utf8_strlen_between(start, end);
          if(length > width){
               char* shift_src = start + length;
               char* shift_dst = start + width;
               int64_t shift_len = strlen(shift_src) + 1; // include null terminator
               memmove(shift_dst, shift_src, shift_len);
          }
          memset(start, ' ', width);
     }
}

static void terminal_scroll_down(CeTerminal_t* terminal, int original, int n){
     CeTerminalGlyph_t* temp_line;
     char* temp_buffer_line;

     CE_CLAMP(n, 0, terminal->bottom - original + 1);

     terminal_clear_region(terminal, 0, terminal->bottom - n + 1, terminal->columns - 1, terminal->bottom);

     for (int i = terminal->bottom; i >= original + n; i--) {
          int cur = terminal->start_line + i;
          int next = terminal->start_line + (i - n);

          temp_line = terminal->lines[cur];
          terminal->lines[cur] = terminal->lines[next];
          terminal->lines[next] = temp_line;

          temp_buffer_line = terminal->buffer->lines[cur];
          terminal->buffer->lines[cur] = terminal->buffer->lines[next];
          terminal->buffer->lines[next] = temp_buffer_line;
     }

     // update the last goto destination based on scrolling
     CeAppBufferData_t* buffer_data = terminal->buffer->app_data;
     buffer_data->last_goto_destination += n;
     if(buffer_data->last_goto_destination >= terminal->buffer->line_count){
          buffer_data->last_goto_destination = terminal->buffer->line_count - 1;
     }
}

static void terminal_scroll_up(CeTerminal_t* terminal, int original, int n){
     CeTerminalGlyph_t* temp_line = NULL;
     char* temp_buffer_line = NULL;

     CE_CLAMP(n, 0, terminal->bottom - original + 1);

     // clear the original line plus the scroll
     terminal_clear_region(terminal, 0, original, terminal->columns - 1, original + n - 1);

     // swap lines to move them all up
     // the cleared lines will end up at the bottom
     for(int i = original; i <= terminal->bottom - n; ++i){
          int cur = terminal->start_line + i;
          int next = terminal->start_line + (i + n);

          temp_line = terminal->lines[cur];
          terminal->lines[cur] = terminal->lines[next];
          terminal->lines[next] = temp_line;

          temp_buffer_line = terminal->buffer->lines[cur];
          terminal->buffer->lines[cur] = terminal->buffer->lines[next];
          terminal->buffer->lines[next] = temp_buffer_line;
     }

     // update the last goto destination based on scrolling
     CeAppBufferData_t* buffer_data = terminal->buffer->app_data;
     buffer_data->last_goto_destination -= n;
     if(buffer_data->last_goto_destination < 0){
          buffer_data->last_goto_destination = 0;
     }
}

static void terminal_set_scroll(CeTerminal_t* terminal, int top, int bottom){
     CE_CLAMP(top, 0, terminal->rows - 1);
     CE_CLAMP(bottom, 0, terminal->rows - 1);

     if(top > bottom){
          int temp = top;
          top = bottom;
          bottom = temp;
     }

     terminal->top = top;
     terminal->bottom = bottom;
}

static void terminal_insert_blank_line(CeTerminal_t* terminal, int n){
     if(BETWEEN(terminal->cursor.y, terminal->top, terminal->bottom)){
          terminal_scroll_down(terminal, terminal->cursor.y, n);
     }
}

static void terminal_insert_blank(CeTerminal_t* terminal, int n){
     int dst, src, size;
     CeTerminalGlyph_t* line;

     CE_CLAMP(n, 0, terminal->columns - terminal->cursor.x);

     dst = terminal->cursor.x + n;
     src = terminal->cursor.x;
     size = terminal->columns - dst;
     line = terminal->lines[terminal->cursor.y];

     memmove(&line[dst], &line[src], size * sizeof(*line));

     // figure out our start and end
     char* line_dst = ce_utf8_iterate_to(terminal->buffer->lines[terminal->cursor.y], dst);
     char* line_src = ce_utf8_iterate_to(terminal->buffer->lines[terminal->cursor.y], src);

     // copy into tmp array
     CeRune_t runes[size];
     for(int i = 0; i < size; i++){
          int64_t rune_len = 0;
          runes[i] = ce_utf8_decode(line_src, &rune_len);
          line_src += rune_len;
     }

     // overwrite dst
     char* itr_dst = line_dst;
     for(int i = 0; i < size; i++){
          int64_t rune_len = 0;
          ce_utf8_encode(runes[i], itr_dst, strlen(itr_dst), &rune_len);
          itr_dst += rune_len;
     }

     terminal_clear_region(terminal, src, terminal->cursor.y, dst - 1, terminal->cursor.y);
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
static void terminal_move_cursor_to_absolute(CeTerminal_t* terminal, int x, int y){
     terminal_move_cursor_to(terminal, x, y + ((terminal->cursor.state & CE_TERMINAL_CURSOR_STATE_ORIGIN) ? terminal->top : 0));
}

static void terminal_put_tab(CeTerminal_t* terminal, int n){
     int32_t new_x = terminal->cursor.x;

     if(n > 0){
          while(new_x < terminal->columns && n--){
               new_x++;
               while(new_x < terminal->columns && !terminal->tabs[new_x]){
                    new_x++;
               }
          }
     }else if(n < 0){
          while(new_x > 0 && n++){
               new_x--;
               while(new_x > 0 && !terminal->tabs[new_x]){
                    new_x--;
               }
          }
     }

     terminal->cursor.x = CE_CLAMP(new_x, 0, terminal->columns - 1);
}

static void terminal_put_newline(CeTerminal_t* terminal, bool first_column){
     int y = terminal->cursor.y;

     if(y == terminal->bottom){
          terminal_scroll_up(terminal, -terminal->start_line, 1);
     }else{
          y++;
     }

     terminal_move_cursor_to(terminal, first_column ? 0 : terminal->cursor.x, y);
}

static void terminal_set_glyph(CeTerminal_t* terminal, CeRune_t rune, CeTerminalGlyph_t* attributes, int x, int y){
     assert(x >= 0 && x < terminal->columns);
     assert(y >= 0 && y < terminal->rows);
     y += terminal->start_line;
     terminal->lines[y][x] = *attributes;
     char* str = ce_utf8_iterate_to(terminal->buffer->lines[y], x);
     assert(str);
     int64_t rune_len = ce_utf8_rune_len(rune);
     int64_t replacing_len = 0;
     ce_utf8_decode(str, &replacing_len);

     // shift over line if necessary
     if(rune_len == replacing_len){
          // pass
     }else if(rune_len < replacing_len){
          // the current rune takes up more space than we need, so shift left
          int64_t diff = replacing_len - rune_len;
          char* dst = str;
          char* src = str + diff;
          memmove(dst, src, strlen(src) + 1); // include null terminator
     }else if(rune_len > replacing_len){
          // the current rune is smaller than we need, so shift right
          int64_t diff = rune_len - replacing_len;
          char* src = str;
          char* dst = str + diff;
          memmove(dst, src, strlen(src) + 1); // include null terminator
     }

     ce_utf8_encode(rune, str, strlen(str), &rune_len);
}

static void terminal_cursor_save(CeTerminal_t* terminal){
     int alt = terminal->mode & CE_TERMINAL_MODE_ALTSCREEN;
     terminal->save_cursor[alt] = terminal->cursor;
}

static void terminal_cursor_load(CeTerminal_t* terminal){
     int alt = terminal->mode & CE_TERMINAL_MODE_ALTSCREEN;
     terminal->cursor = terminal->save_cursor[alt];
     terminal_move_cursor_to(terminal, terminal->save_cursor[alt].x, terminal->save_cursor[alt].y);
}

static void terminal_swap_screen(CeTerminal_t* terminal){
     CeTerminalGlyph_t** tmp_lines = terminal->lines;

     terminal->lines = terminal->alternate_lines;
     terminal->alternate_lines = tmp_lines;

     if(terminal->buffer == terminal->lines_buffer){
          terminal->buffer = terminal->alternate_lines_buffer;
     }else{
          terminal->buffer = terminal->lines_buffer;
     }

     terminal->mode ^= CE_TERMINAL_MODE_ALTSCREEN;
}

static void terminal_delete_line(CeTerminal_t* terminal, int n){
     if(BETWEEN(terminal->cursor.y, terminal->top, terminal->bottom)){
          terminal_scroll_up(terminal, terminal->cursor.y, n);
     }
}

static void terminal_delete_char(CeTerminal_t* terminal, int n){
     int dst, src, size;
     CeTerminalGlyph_t* line;

     CE_CLAMP(n, 0, terminal->columns - terminal->cursor.x);
     int cursor_line = terminal->cursor.y + terminal->start_line;

     dst = terminal->cursor.x;
     src = terminal->cursor.x + n;
     size = terminal->columns - src;
     line = terminal->lines[cursor_line];

     memmove(line + dst, line + src, size * sizeof(*line));

     // figure out our start and end
     char* line_dst = ce_utf8_iterate_to(terminal->buffer->lines[cursor_line], dst);
     char* line_src = ce_utf8_iterate_to(terminal->buffer->lines[cursor_line], src);

     // copy into tmp array
     CeRune_t runes[size];
     for(int i = 0; i < size; i++){
          int64_t rune_len = 0;
          runes[i] = ce_utf8_decode(line_src, &rune_len);
          line_src += rune_len;
     }

     // overwrite dst
     char* itr_dst = line_dst;
     for(int i = 0; i < size; i++){
          int64_t rune_len = 0;
          ce_utf8_encode(runes[i], itr_dst, strlen(itr_dst), &rune_len);
          itr_dst += rune_len;
     }

     terminal_clear_region(terminal, terminal->columns - n, terminal->cursor.y, terminal->columns - 1, terminal->cursor.y);
}

static bool tty_write(int file_descriptor, const char* string, size_t len){
     ssize_t written = 0;

     while((size_t)(written) < len){
          ssize_t rc = write(file_descriptor, string, len - written);

          if(rc < 0){
               ce_log("%s() write() to terminal failed: %s", __FUNCTION__, strerror(errno));
               return false;
          }

          written += rc;
          string += rc;
     }

     return true;
}

static void str_parse(CeTerminal_t* terminal){
     CeTerminalSTREscape_t* str = &terminal->str_escape;
     int c;
     char *p = str->buffer;

     str->argument_count = 0;
     str->buffer[str->buffer_length] = 0;

     if(*p == 0) return;

     while(str->argument_count < CE_TERMINAL_ESCAPE_ARGUMENT_SIZE){
          str->arguments[str->argument_count] = p;
          str->argument_count++;

          while((c = *p) != ';' && c != 0){
               ++p;
          }

          if(c == 0) return;
          *p++ = 0;
     }
}

static void str_sequence(CeTerminal_t* terminal, CeRune_t rune){
     memset(&terminal->str_escape, 0, sizeof(terminal->str_escape));

     switch(rune){
     default:
          break;
     case 0x90:
          rune = 'P';
          terminal->escape_state |= CE_TERMINAL_ESCAPE_STATE_DCS;
          break;
     case 0x9f:
          rune = '_';
          break;
     case 0x9e:
          rune = '^';
          break;
     case 0x9d:
          rune = ']';
          break;
     }

     terminal->str_escape.type = rune;
     terminal->escape_state |= CE_TERMINAL_ESCAPE_STATE_STR;
}

static void str_handle(CeTerminal_t* terminal){
     CeTerminalSTREscape_t* str = &terminal->str_escape;

     terminal->escape_state &= ~(CE_TERMINAL_ESCAPE_STATE_STR_END | CE_TERMINAL_ESCAPE_STATE_STR);
     str_parse(terminal);
     int argument_count = str->argument_count;
     int param = argument_count ? atoi(str->arguments[0]) : 0;

     switch(str->type){
     default:
          break;
     case ']':
          switch(param){
          default:
               break;
          case 0:
          case 1:
          case 2:
               break;
          case 52:
               break;
          case 4:
          case 104:
               break;
          }
          break;
     case 'k':
          break;
     case 'P':
          terminal->mode |= CE_TERMINAL_ESCAPE_STATE_DCS;
          break;
     case '_':
          break;
     case '^':
          break;
     }
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

static void terminal_set_mode(CeTerminal_t* terminal, bool set){
     CeTerminalCSIEscape_t* csi = &terminal->csi_escape;
     int* arg;
     int* last_arg = csi->arguments + csi->argument_count;
     //int mode;
     int alt;

     for(arg = csi->arguments; arg <= last_arg; ++arg){
          if(csi->private){
               switch(*arg){
               default:
                    break;
               case 1:
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_APPCURSOR);
                    break;
               case 5:
                    //mode = terminal->mode;
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_REVERSE);
                    // TODO if(mode != terminal->mode) redraw();
                    break;
               case 6:
                    CHANGE_BIT(terminal->cursor.state, set, CE_TERMINAL_CURSOR_STATE_ORIGIN);
                    terminal_move_cursor_to_absolute(terminal, 0, 0);
                    break;
               case 7:
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_WRAP);
                    break;
               case 0:
               case 2:
               case 3:
               case 4:
               case 8:
               case 18:
               case 19:
               case 42:
               case 12:
                    // ignored
                    break;
               case 25:
                    CHANGE_BIT(terminal->mode, !set, CE_TERMINAL_MODE_HIDE);
                    break;
               case 9:
                    // TODO: xsetpointermotion(0); ?
                    CHANGE_BIT(terminal->mode, 0, CE_TERMINAL_MODE_MOUSE);
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_MOUSEEX10);
                    break;
               case 1000:
                    // TODO: xsetpointermotion(0); ?
                    CHANGE_BIT(terminal->mode, 0, CE_TERMINAL_MODE_MOUSE);
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_MOUSEBTN);
                    break;
               case 1002:
                    // TODO: xsetpointermotion(0); ?
                    CHANGE_BIT(terminal->mode, 0, CE_TERMINAL_MODE_MOUSE);
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_MOUSEMOTION);
                    break;
               case 1003:
                    // TODO: xsetpointermotion(0); ?
                    CHANGE_BIT(terminal->mode, 0, CE_TERMINAL_MODE_MOUSE);
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_MOUSEEMANY);
                    break;
               case 1004:
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_FOCUS);
                    break;
               case 1006:
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_MOUSEGR);
                    break;
               case 1034:
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_8BIT);
                    break;
               case 1049:
                    if(set){
                         terminal_cursor_save(terminal);
                    }else{
                         terminal_cursor_load(terminal);
                    }
                    // fallthrough
               case 47:
               case 1047:
                    // f this layout
                    alt = terminal->mode & CE_TERMINAL_MODE_ALTSCREEN;
                    if(alt) terminal_clear_region(terminal, 0, 0, terminal->columns - 1, terminal->rows - 1);
                    if(set ^ alt) terminal_swap_screen(terminal);
                    if(*arg != 1049) break;
                    // fallthrough
               case 1048:
                    if(set){
                         terminal_cursor_save(terminal);
                    }else{
                         terminal_cursor_load(terminal);
                    }
                    break;
               case 2004:
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_BRCKTPASTE);
                    break;
               case 1001:
               case 1005:
               case 1015:
                    // ignored
                    break;
               }
          }else{
               switch(*arg){
               default:
                    break;
               case 2:
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_KBDLOCK);
                    break;
               case 4:
                    CHANGE_BIT(terminal->mode, set, CE_TERMINAL_MODE_INSERT);
                    break;
               case 12:
                    CHANGE_BIT(terminal->mode, !set, CE_TERMINAL_MODE_ECHO);
                    break;
               case 20:
                    CHANGE_BIT(terminal->mode, !set, CE_TERMINAL_MODE_CRLF);
                    break;
               }
          }
     }
}

static void terminal_set_attributes(CeTerminal_t* terminal){
     CeTerminalCSIEscape_t* csi = &terminal->csi_escape;

     for(uint32_t i = 0; i < csi->argument_count; ++i){
          switch(csi->arguments[i]){
          default:
               break;
          case 0:
               terminal->cursor.attributes.attributes &= ~(CE_TERMINAL_GLYPH_ATTRIBUTE_BOLD | CE_TERMINAL_GLYPH_ATTRIBUTE_FAINT |
                                                           CE_TERMINAL_GLYPH_ATTRIBUTE_ITALIC | CE_TERMINAL_GLYPH_ATTRIBUTE_UNDERLINE |
                                                           CE_TERMINAL_GLYPH_ATTRIBUTE_BLINK | CE_TERMINAL_GLYPH_ATTRIBUTE_REVERSE |
                                                           CE_TERMINAL_GLYPH_ATTRIBUTE_INVISIBLE | CE_TERMINAL_GLYPH_ATTRIBUTE_STRUCK);
               terminal->cursor.attributes.foreground = COLOR_DEFAULT;
               terminal->cursor.attributes.background = COLOR_DEFAULT;
               break;
          case 1:
               terminal->cursor.attributes.attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_BOLD;
               break;
          case 2:
               terminal->cursor.attributes.attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_FAINT;
               break;
          case 3:
               terminal->cursor.attributes.attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_ITALIC;
               break;
          case 4:
               terminal->cursor.attributes.attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_UNDERLINE;
               break;
          case 5: // fallthrough
          case 6:
               terminal->cursor.attributes.attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_BLINK;
               break;
          case 7:
               terminal->cursor.attributes.attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_REVERSE;
               break;
          case 8:
               terminal->cursor.attributes.attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_INVISIBLE;
               break;
          case 9:
               terminal->cursor.attributes.attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_STRUCK;
               break;
          case 21:
               terminal->cursor.attributes.attributes &= ~CE_TERMINAL_GLYPH_ATTRIBUTE_BOLD;
               break;
          case 22:
               terminal->cursor.attributes.attributes &= ~(CE_TERMINAL_GLYPH_ATTRIBUTE_BOLD | CE_TERMINAL_GLYPH_ATTRIBUTE_FAINT);
               break;
          case 23:
               terminal->cursor.attributes.attributes &= ~CE_TERMINAL_GLYPH_ATTRIBUTE_ITALIC;
               break;
          case 24:
               terminal->cursor.attributes.attributes &= ~CE_TERMINAL_GLYPH_ATTRIBUTE_UNDERLINE;
               break;
          case 25:
               terminal->cursor.attributes.attributes &= ~CE_TERMINAL_GLYPH_ATTRIBUTE_BLINK;
               break;
          case 27:
               terminal->cursor.attributes.attributes &= ~CE_TERMINAL_GLYPH_ATTRIBUTE_REVERSE;
               break;
          case 28:
               terminal->cursor.attributes.attributes &= ~CE_TERMINAL_GLYPH_ATTRIBUTE_INVISIBLE;
               break;
          case 29:
               terminal->cursor.attributes.attributes &= ~CE_TERMINAL_GLYPH_ATTRIBUTE_STRUCK;
               break;
          case 30:
               terminal->cursor.attributes.foreground = COLOR_BLACK;
               break;
          case 31:
               terminal->cursor.attributes.foreground = COLOR_RED;
               break;
          case 32:
               terminal->cursor.attributes.foreground = COLOR_GREEN;
               break;
          case 33:
               terminal->cursor.attributes.foreground = COLOR_YELLOW;
               break;
          case 34:
               terminal->cursor.attributes.foreground = COLOR_BLUE;
               break;
          case 35:
               terminal->cursor.attributes.foreground = COLOR_MAGENTA;
               break;
          case 36:
               terminal->cursor.attributes.foreground = COLOR_CYAN;
               break;
          case 37:
               terminal->cursor.attributes.foreground = COLOR_WHITE;
               break;
          case 38: // TODO: reserved fg color
               break;
          case 39:
               terminal->cursor.attributes.foreground = COLOR_DEFAULT;
               break;
          case 40:
               terminal->cursor.attributes.background = COLOR_BLACK;
               break;
          case 41:
               terminal->cursor.attributes.background = COLOR_RED;
               break;
          case 42:
               terminal->cursor.attributes.background = COLOR_GREEN;
               break;
          case 43:
               terminal->cursor.attributes.background = COLOR_YELLOW;
               break;
          case 44:
               terminal->cursor.attributes.background = COLOR_BLUE;
               break;
          case 45:
               terminal->cursor.attributes.background = COLOR_MAGENTA;
               break;
          case 46:
               terminal->cursor.attributes.background = COLOR_CYAN;
               break;
          case 47:
               terminal->cursor.attributes.background = COLOR_WHITE;
               break;
          case 48: // TODO: reserved bg color
               break;
          case 49:
               terminal->cursor.attributes.background = COLOR_DEFAULT;
               break;
          case 90:
               terminal->cursor.attributes.foreground = COLOR_BRIGHT_BLACK;
               break;
          case 91:
               terminal->cursor.attributes.foreground = COLOR_BRIGHT_RED;
               break;
          case 92:
               terminal->cursor.attributes.foreground = COLOR_BRIGHT_GREEN;
               break;
          case 93:
               terminal->cursor.attributes.foreground = COLOR_BRIGHT_YELLOW;
               break;
          case 94:
               terminal->cursor.attributes.foreground = COLOR_BRIGHT_BLUE;
               break;
          case 95:
               terminal->cursor.attributes.foreground = COLOR_BRIGHT_MAGENTA;
               break;
          case 96:
               terminal->cursor.attributes.foreground = COLOR_BRIGHT_CYAN;
               break;
          case 97:
               terminal->cursor.attributes.foreground = COLOR_BRIGHT_WHITE;
               break;
          case 100:
               terminal->cursor.attributes.background = COLOR_BRIGHT_BLACK;
               break;
          case 101:
               terminal->cursor.attributes.background = COLOR_BRIGHT_RED;
               break;
          case 102:
               terminal->cursor.attributes.background = COLOR_BRIGHT_GREEN;
               break;
          case 103:
               terminal->cursor.attributes.background = COLOR_BRIGHT_YELLOW;
               break;
          case 104:
               terminal->cursor.attributes.background = COLOR_BRIGHT_BLUE;
               break;
          case 105:
               terminal->cursor.attributes.background = COLOR_BRIGHT_MAGENTA;
               break;
          case 106:
               terminal->cursor.attributes.background = COLOR_BRIGHT_CYAN;
               break;
          case 107:
               terminal->cursor.attributes.background = COLOR_BRIGHT_WHITE;
               break;
          }
     }
}

static void csi_parse(CeTerminalCSIEscape_t* csi){
     char* str = csi->buffer;
     char* end = NULL;
     long int value = 0;

     csi->argument_count = 0;

     if(*str == '?'){
          csi->private = 1;
          str++;
     }

     csi->buffer[csi->buffer_length] = 0;
     while(str < (csi->buffer + csi->buffer_length)){
          end = NULL;
          value = strtol(str, &end, 10);

          if(end == str) value = 0;
          else if(value == LONG_MAX || value == LONG_MIN) value = -1;

          csi->arguments[csi->argument_count] = value;
          csi->argument_count++;

          str = end;

          if(*str != ';' || csi->argument_count == CE_TERMINAL_ESCAPE_ARGUMENT_SIZE) break;

          str++;
     }

     csi->mode[0] = *str++;
     csi->mode[1] = (str < (csi->buffer + csi->buffer_length)) ? *str : 0;
}

static void csi_handle(CeTerminal_t* terminal){
     CeTerminalCSIEscape_t* csi = &terminal->csi_escape;

     switch(csi->mode[0]){
     default:
          ce_log("unhandled csi: '%c' in sequence: '%s'\n", csi->mode[0], csi->buffer);
          break;
     case '@':
          DEFAULT(csi->arguments[0], 1);
          terminal_insert_blank(terminal, csi->arguments[0]);
          break;
     case 'A':
          DEFAULT(csi->arguments[0], 1);
          terminal_move_cursor_to(terminal, terminal->cursor.x, terminal->cursor.y - csi->arguments[0]);
          break;
     case 'B':
     case 'e':
          DEFAULT(csi->arguments[0], 1);
          terminal_move_cursor_to(terminal, terminal->cursor.x, terminal->cursor.y + csi->arguments[0]);
          break;
     case 'i':
          // TODO
          switch(csi->arguments[0]){
          default:
               break;
          case 0:
               break;
          case 1:
               break;
          case 2:
               break;
          case 4:
               break;
          case 5:
               break;
          }
          break;
     case 'c':
          if (csi->arguments[0] == 0) tty_write(terminal->file_descriptor, CE_TERMINAL_VT_IDENTIFIER, sizeof(CE_TERMINAL_VT_IDENTIFIER) - 1);
          break;
     case 'C':
     case 'a':
          DEFAULT(csi->arguments[0], 1);
          terminal_move_cursor_to(terminal, terminal->cursor.x + csi->arguments[0], terminal->cursor.y);
          break;
     case 'D':
          DEFAULT(csi->arguments[0], 1);
          terminal_move_cursor_to(terminal, terminal->cursor.x - csi->arguments[0], terminal->cursor.y);
          break;
     case 'E':
          DEFAULT(csi->arguments[0], 1);
          terminal_move_cursor_to(terminal, 0, terminal->cursor.y + csi->arguments[0]);
          break;
     case 'F':
          DEFAULT(csi->arguments[0], 1);
          terminal_move_cursor_to(terminal, 0, terminal->cursor.y - csi->arguments[0]);
          break;
     case 'g':
          switch(csi->arguments[0]){
          default:
               break;
          case 0: // clear tab stop
               terminal->tabs[terminal->cursor.x] = 0;
               break;
          case 3: // clear all tabs
               memset(terminal->tabs, 0, terminal->columns * sizeof(*terminal->tabs));
               break;
          }
          break;
     case 'G':
     case '`':
          DEFAULT(csi->arguments[0], 1);
          terminal_move_cursor_to(terminal, csi->arguments[0] - 1, terminal->cursor.y);
          break;
     case 'H':
     case 'f':
          DEFAULT(csi->arguments[0], 1);
          DEFAULT(csi->arguments[1], 1);
          terminal_move_cursor_to_absolute(terminal, csi->arguments[1] - 1, csi->arguments[0] - 1);
          break;
     case 'I':
          DEFAULT(csi->arguments[0], 1);
          terminal_put_tab(terminal, csi->arguments[0]);
          break;
     case 'J': // clear region in relation to cursor
          switch(csi->arguments[0]){
          default:
               break;
          case 0: // below
               terminal_clear_region(terminal, terminal->cursor.x, terminal->cursor.y, terminal->columns - 1, terminal->cursor.y);
               if(terminal->cursor.y < (terminal->rows - 1)){
                    terminal_clear_region(terminal, 0, terminal->cursor.y + 1, terminal->columns - 1, terminal->rows - 1);
               }
               break;
          case 1: // above
               if(terminal->cursor.y > 1){
                    terminal_clear_region(terminal, 0, 0, terminal->columns - 1, terminal->cursor.y - 1);
               }
               terminal_clear_region(terminal, 0, terminal->cursor.y, terminal->cursor.x, terminal->cursor.y);
               break;
          case 2: // all
               terminal_clear_region(terminal, 0, 0, terminal->columns - 1, terminal->rows - 1);
               break;
          }
          break;
     case 'K': // clear line
          switch(csi->arguments[0]){
          default:
               break;
          case 0: // right of cursor
               terminal_clear_region(terminal, terminal->cursor.x, terminal->cursor.y, terminal->columns - 1, terminal->cursor.y);
               break;
          case 1: // left of cursor
               terminal_clear_region(terminal, 0, terminal->cursor.y, terminal->cursor.x, terminal->cursor.y);
               break;
          case 2: // all
               terminal_clear_region(terminal, 0, terminal->cursor.y, terminal->columns - 1, terminal->cursor.y);
               break;
          }
          break;
     case 'S':
          DEFAULT(csi->arguments[0], 1);
          terminal_scroll_up(terminal, terminal->top, csi->arguments[0]);
          break;
     case 'T':
          DEFAULT(csi->arguments[0], 1);
          terminal_scroll_down(terminal, terminal->top, csi->arguments[0]);
          break;
     case 'L':
          DEFAULT(csi->arguments[0], 1);
          terminal_insert_blank_line(terminal, csi->arguments[0]);
          break;
     case 'l':
          terminal_set_mode(terminal, false);
          break;
     case 'M':
          DEFAULT(csi->arguments[0], 1);
          terminal_delete_line(terminal, csi->arguments[0]);
          break;
     case 'X':
          DEFAULT(csi->arguments[0], 1);
          terminal_clear_region(terminal, terminal->cursor.x, terminal->cursor.y, terminal->cursor.x + csi->arguments[0] - 1, terminal->cursor.y);
          break;
     case 'P':
          DEFAULT(csi->arguments[0], 1);
          terminal_delete_char(terminal, csi->arguments[0]);
          break;
     case 'Z':
          DEFAULT(csi->arguments[0], 1);
          terminal_put_tab(terminal, -csi->arguments[0]);
          break;
     case 'd':
          DEFAULT(csi->arguments[0], 1);
          terminal_move_cursor_to_absolute(terminal, terminal->cursor.x, csi->arguments[0] - 1);
          break;
     case 'h':
          terminal_set_mode(terminal, true);
          break;
     case 'm':
          terminal_set_attributes(terminal);
          break;
     case 'n':
          if(csi->arguments[0] == 6){
               char buffer[64];
               int len = snprintf(buffer, 64, "\033[%i;%iR",
                              terminal->cursor.x, terminal->cursor.y);
               tty_write(terminal->file_descriptor, buffer, len);
          }
          break;
     case 'r':
          if(!csi->private){
               DEFAULT(csi->arguments[0], 1);
               DEFAULT(csi->arguments[1], terminal->rows);
               terminal_set_scroll(terminal, csi->arguments[0] - 1, csi->arguments[1] - 1);
               terminal_move_cursor_to_absolute(terminal, 0, 0);
          }
          break;
     case 's':
          terminal_cursor_save(terminal);
          break;
     case 'u':
          terminal_cursor_load(terminal);
          break;
     case ' ': // cursor style
          break;
     }
}

static void terminal_reset(CeTerminal_t* terminal){
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
     terminal_clear_region(terminal, 0, -terminal->start_line, terminal->columns - 1, terminal->rows - 1);
     terminal_swap_screen(terminal);

     terminal_move_cursor_to(terminal, 0, 0);
     terminal_cursor_save(terminal);
     terminal_clear_region(terminal, 0, -terminal->start_line, terminal->columns - 1, terminal->rows - 1);
     terminal_swap_screen(terminal);

     terminal->cursor.attributes.attributes = CE_TERMINAL_GLYPH_ATTRIBUTE_NONE;
     terminal->cursor.attributes.foreground = COLOR_DEFAULT;
     terminal->cursor.attributes.background = COLOR_DEFAULT;
     terminal->cursor.state = CE_TERMINAL_CURSOR_STATE_DEFAULT;
     terminal->cursor.x = 0;
     terminal->cursor.y = 0;
}

static bool esc_handle(CeTerminal_t* terminal, CeRune_t rune){
     switch(rune) {
     case '[':
          terminal->escape_state |= CE_TERMINAL_ESCAPE_STATE_CSI;
          return false;
     case '#':
          terminal->escape_state |= CE_TERMINAL_ESCAPE_STATE_TEST;
          return false;
     case '%':
          terminal->escape_state |= CE_TERMINAL_ESCAPE_STATE_UTF8;
          return false;
     case 'P': // DCS -- Device Control String
     case '_': // APC -- Application Program Command
     case '^': // PM -- Privacy Message
     case ']': // OSC -- Operating System Command
     case 'k': // old title set compatibility
          str_sequence(terminal, rune);
          return false;
     case 'n': // LS2 -- Locking shift 2
     case 'o': // LS3 -- Locking shift 3
          // TODO
          //term.charset = 2 + (ascii - 'n');
          break;
     case '(': // GZD4 -- set primary charset G0
     case ')': // G1D4 -- set secondary charset G1
     case '*': // G2D4 -- set tertiary charset G2
     case '+': // G3D4 -- set quaternary charset G3
          // TODO
          //term.icharset = ascii - '(';
          //term.esc |= ESC_ALTCHARSET;
          return false;
     case 'D': // IND -- Linefeed
          if(terminal->cursor.y == terminal->bottom){
               terminal_scroll_up(terminal, terminal->top, 1);
          }else{
               terminal_move_cursor_to(terminal, terminal->cursor.x, terminal->cursor.y + 1);
          }
          break;
     case 'E': // NEL -- Next line
          terminal_put_newline(terminal, true); // always go to first col
          break;
     case 'H': // HTS -- Horizontal tab stop
          terminal->tabs[terminal->cursor.x] = 1;
          break;
     case 'M': // RI -- Reverse index
          if(terminal->cursor.y == terminal->top){
               terminal_scroll_down(terminal, terminal->top, 1);
          }else{
               terminal_move_cursor_to(terminal, terminal->cursor.x, terminal->cursor.y - 1);
          }
          break;
     case 'Z': // DECID -- Identify Terminal
          tty_write(terminal->file_descriptor, CE_TERMINAL_VT_IDENTIFIER, sizeof(CE_TERMINAL_VT_IDENTIFIER) - 1);
          break;
     case 'c': // RIS -- Reset to inital state
          terminal_reset(terminal);
          //resettitle();
          //xloadcols();
          break;
     case '=': // DECPAM -- Application keypad
          terminal->mode |= CE_TERMINAL_MODE_APPKEYPAD;
          break;
     case '>': // DECPNM -- Normal keypad
          terminal->mode &= ~CE_TERMINAL_MODE_APPKEYPAD;
          break;
     case '7': // DECSC -- Save Cursor
          terminal_cursor_save(terminal);
          break;
     case '8': // DECRC -- Restore Cursor
          terminal_cursor_load(terminal);
          break;
     case '\\': // ST -- String Terminator
          if(terminal->escape_state & CE_TERMINAL_ESCAPE_STATE_STR_END) str_handle(terminal);
          break;
     default:
          ce_log("%s(): unknown sequence ESC 0x%02X '%c' in sequence: '%s'\n", __FUNCTION__, (unsigned char)rune,
                 isprint(rune) ? rune : '.', terminal->csi_escape.buffer);
          break;
     }

     return true;
}

static void terminal_put(CeTerminal_t* terminal, CeRune_t rune){
     char characters[CE_UTF8_SIZE];
     int width = 1;
     int64_t len = 0;

     if(terminal->mode & CE_TERMINAL_MODE_UTF8){
          ce_utf8_encode(rune, characters, CE_UTF8_SIZE, &len);
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
               CeTerminalCSIEscape_t* csi = &terminal->csi_escape;
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

     CeTerminalGlyph_t* current_glyph = terminal->lines[terminal->cursor.y] + terminal->cursor.x;
     if(terminal->mode & CE_TERMINAL_MODE_WRAP && terminal->cursor.state & CE_TERMINAL_CURSOR_STATE_WRAPNEXT){
          current_glyph->attributes |= CE_TERMINAL_GLYPH_ATTRIBUTE_WRAP;
          terminal_put_newline(terminal, true);
          current_glyph = terminal->lines[terminal->cursor.y] + terminal->cursor.x;
     }

     if(terminal->mode & CE_TERMINAL_MODE_INSERT && (terminal->cursor.x + width) < terminal->columns){
          int size = (terminal->columns - terminal->cursor.x - width);
          memmove(current_glyph + width, current_glyph, size * sizeof(*current_glyph));

          // TODO: compress with similar code above
          // figure out our start and end
          char* line_src = ce_utf8_iterate_to(terminal->buffer->lines[terminal->cursor.y], terminal->cursor.x);
          char* line_dst = line_src + width;

          // copy into tmp array
          CeRune_t runes[size];
          for(int i = 0; i < size; i++){
               int64_t rune_len = 0;
               runes[i] = ce_utf8_decode(line_src, &rune_len);
               line_src += rune_len;
          }

          // overwrite dst
          char* itr_dst = line_dst;
          for(int i = 0; i < size; i++){
               int64_t rune_len = 0;
               ce_utf8_encode(runes[i], itr_dst, strlen(itr_dst), &rune_len);
               itr_dst += rune_len;
          }
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
          }else if(rc > 0){
               buffer_length = rc;

               for(int i = 0; i < buffer_length; ++i){
                    decoded = ce_utf8_decode(buffer + i, &decoded_length);
                    terminal_put(terminal, decoded);
                    i += (decoded_length - 1);
               }

               terminal->ready_to_draw = true;
          }

          sleep(0);
     }

     return NULL;
}

static void handle_signal_child(int signal){
     ce_log("%s(%d)\n", __FUNCTION__, signal);
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
               if(!shell) shell = CE_TERMINAL_DEFAULT_SHELL;

               pw = getpwuid(getuid());
               if(pw == NULL){
                    ce_log("getpwuid() failed: '%s'\n", strerror(errno));
                    return false;
               }

               char** args = (char *[]){shell, NULL};

               unsetenv("COLUMNS");
               unsetenv("LINES");
               unsetenv("TERMCAP");
               setenv("LOGNAME", pw->pw_name, 1);
               setenv("USER", pw->pw_name, 1);
               setenv("SHELL", shell, 1);
               setenv("HOME", pw->pw_dir, 1);
               setenv("TERM", CE_TERMINAL_NAME, 1);

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

static void terminal_echo(CeTerminal_t* terminal, CeRune_t rune){
     if(is_controller(rune)){
          if(rune & 0x80){
               rune &= 0x7f;
               terminal_put(terminal, '^');
               terminal_put(terminal, '[');
          }else if(rune != '\n' && rune != '\r' && rune != '\t'){
               rune ^= 0x40;
               terminal_put(terminal, '^');
          }
     }

     terminal_put(terminal, rune);
}

bool ce_terminal_init(CeTerminal_t* terminal, int64_t width, int64_t height, int64_t line_count, const char* buffer_name){
     terminal->columns = width;
     terminal->rows = height;
     terminal->top = 0;
     terminal->bottom = height - 1;
     terminal->line_count = line_count;
     terminal->start_line = line_count - height;

     // allocate lines and alternate lines
     terminal->lines = calloc(line_count, sizeof(*terminal->lines));
     terminal->alternate_lines = calloc(line_count, sizeof(*terminal->alternate_lines));

     // allocate buffers
     terminal->lines_buffer = calloc(1, sizeof(*terminal->lines_buffer));
     terminal->alternate_lines_buffer = calloc(1, sizeof(*terminal->alternate_lines_buffer));
     ce_buffer_alloc(terminal->lines_buffer, line_count, buffer_name);
     ce_buffer_alloc(terminal->alternate_lines_buffer, line_count, buffer_name);
     terminal->lines_buffer->status = CE_BUFFER_STATUS_READONLY;
     terminal->alternate_lines_buffer->status = CE_BUFFER_STATUS_READONLY;
     terminal->buffer = terminal->lines_buffer;

     for(int r = 0; r < line_count; ++r){
          terminal->lines[r] = malloc(terminal->columns * sizeof(*terminal->lines[r]));
          terminal->alternate_lines[r] = malloc(terminal->columns * sizeof(*terminal->alternate_lines[r]));

          // default fg and bg
          for(int g = 0; g < terminal->columns; ++g){
               terminal->lines[r][g].foreground = -1;
               terminal->lines[r][g].background = -1;
          }

          // alloc buffer lines, accounting for the fact that all characters could be in max UTF8 size
          size_t bytes = (terminal->columns + 1) * CE_UTF8_SIZE;
          terminal->lines_buffer->lines[r] = realloc(terminal->lines_buffer->lines[r], bytes);
          terminal->alternate_lines_buffer->lines[r] = realloc(terminal->alternate_lines_buffer->lines[r], bytes);

          // set them to blank
          memset(terminal->lines_buffer->lines[r], ' ', terminal->columns);
          memset(terminal->alternate_lines_buffer->lines[r], ' ', terminal->columns);

          // null terminate end of current string to end of entire string
          int64_t rest_of_the_bytes = bytes - terminal->columns;
          memset(terminal->lines_buffer->lines[r] + terminal->columns, 0, rest_of_the_bytes);
          memset(terminal->alternate_lines_buffer->lines[r] + terminal->columns, 0, rest_of_the_bytes);
     }

     terminal->tabs = calloc(terminal->columns, sizeof(*terminal->tabs));
     terminal_reset(terminal);

     if(!tty_create(terminal->rows, terminal->columns, &terminal->pid, &terminal->file_descriptor)){
          return false;
     }

     int rc = pthread_create(&terminal->thread, NULL, tty_reader, terminal);
     if(rc != 0){
          ce_log("pthread_create() failed: '%s'\n", strerror(errno));
          return false;
     }

     return true;
}

void ce_terminal_resize(CeTerminal_t* terminal, int64_t width, int64_t height){
     // TODO: realloc lines
     if(height > terminal->line_count){

     }

     if(terminal->columns > width){
          for(int64_t i = 0; i < terminal->line_count; i++){
               terminal->lines[i] = realloc(terminal->lines[i], width * sizeof(*terminal->lines[0]));
               terminal->alternate_lines[i] = realloc(terminal->alternate_lines[i], width * sizeof(*terminal->alternate_lines[0]));

               // realloc buffer lines so they are smaller
               size_t bytes = (width + 1) * CE_UTF8_SIZE;
               terminal->lines_buffer->lines[i] = realloc(terminal->lines_buffer->lines[i], bytes);
               terminal->alternate_lines_buffer->lines[i] = realloc(terminal->alternate_lines_buffer->lines[i], bytes);

               // find the end and null terminal it
               char* end = ce_utf8_iterate_to(terminal->lines_buffer->lines[i], width);
               if(end) *end = 0;
               end = ce_utf8_iterate_to(terminal->alternate_lines_buffer->lines[i], width);
               if(end) *end = 0;
          }
     }else if(terminal->columns < width){
          for(int64_t i = 0; i < terminal->line_count; i++){
               terminal->lines[i] = realloc(terminal->lines[i], width * sizeof(*terminal->lines[0]));
               terminal->alternate_lines[i] = realloc(terminal->alternate_lines[i], width * sizeof(*terminal->alternate_lines[0]));

               // realloc buffer lines so they are smaller
               size_t bytes = (width + 1) * CE_UTF8_SIZE;
               terminal->lines_buffer->lines[i] = realloc(terminal->lines_buffer->lines[i], bytes);
               terminal->alternate_lines_buffer->lines[i] = realloc(terminal->alternate_lines_buffer->lines[i], bytes);

               // append spaces and null terminator with the new columns
               int64_t diff = width - terminal->columns;
               char* end = ce_utf8_iterate_to_include_end(terminal->lines_buffer->lines[i], terminal->columns);
               memset(end, ' ', diff);
               end += diff;
               *end = 0;

               end = ce_utf8_iterate_to_include_end(terminal->alternate_lines_buffer->lines[i], terminal->columns);
               memset(end, ' ', diff);
               end += diff;
               *end = 0;
          }
     }

     terminal->columns = width;
     terminal->rows = height;
     terminal->top = 0;
     terminal->bottom = height - 1;
     terminal->start_line = terminal->line_count - height;

     // clamp cursor onto terminal
     if(terminal->cursor.y > terminal->line_count){
          terminal->cursor.y = terminal->line_count - 1;
     }

     if(terminal->cursor.y > width){
          terminal->cursor.x = width - 1;
     }

     struct winsize window_size = {};

     window_size.ws_row = height;
     window_size.ws_col = width;

     if(ioctl(terminal->file_descriptor, TIOCSWINSZ, &window_size) < 0){
          ce_log("%s() ioctl() failed %s", __FUNCTION__, strerror(errno));
     }
}

void ce_terminal_free(CeTerminal_t* terminal){
     pthread_cancel(terminal->thread);
     pthread_join(terminal->thread, NULL);

     for(int r = 0; r < terminal->line_count; ++r){
          free(terminal->lines[r]);
     }
     free(terminal->lines);
     terminal->lines = NULL;

     for(int r = 0; r < terminal->line_count; ++r){
          free(terminal->alternate_lines[r]);
     }
     free(terminal->alternate_lines);
     terminal->alternate_lines = NULL;

     free(terminal->tabs);
     terminal->tabs = NULL;

     free(terminal->lines_buffer->app_data);
     ce_buffer_free(terminal->lines_buffer);
     free(terminal->lines_buffer);

     free(terminal->alternate_lines_buffer->app_data);
     ce_buffer_free(terminal->alternate_lines_buffer);
     free(terminal->alternate_lines_buffer);

     terminal->lines_buffer = NULL;
     terminal->alternate_lines_buffer = NULL;
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
          case -1:
               character = 3;
               break;
          case 339:
               character = 4;
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

     if(terminal->mode & CE_TERMINAL_MODE_ECHO){
          for(size_t i = 0; i < len; i++){
               terminal_echo(terminal, string[i]);
          }
     }

     if(free_string) free(string);
     return true;
}

char* ce_terminal_get_current_directory(CeTerminal_t* terminal){
     char cwd_file[BUFSIZ];
     snprintf(cwd_file, BUFSIZ, "/proc/%d/cwd", terminal->pid);
     return realpath(cwd_file, NULL);
}
