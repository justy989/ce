#pragma once

#include "ce.h"

#define CE_TERMINAL_TAB_SPACES 5 // TODO: use config_options
#define CE_TERMINAL_NAME "xterm"
#define CE_TERMINAL_DEFAULT_SHELL "/bin/bash"
#define CE_TERMINAL_ESCAPE_BUFFER_SIZE (128 * CE_UTF8_SIZE)
#define CE_TERMINAL_ESCAPE_ARGUMENT_SIZE 16
#define CE_TERMINAL_VT_IDENTIFIER "\033[?6c"

typedef enum{
     CE_TERMINAL_GLYPH_ATTRIBUTE_NONE       = 0,
     CE_TERMINAL_GLYPH_ATTRIBUTE_BOLD       = 1 << 0,
     CE_TERMINAL_GLYPH_ATTRIBUTE_FAINT      = 1 << 1,
     CE_TERMINAL_GLYPH_ATTRIBUTE_ITALIC     = 1 << 2,
     CE_TERMINAL_GLYPH_ATTRIBUTE_UNDERLINE  = 1 << 3,
     CE_TERMINAL_GLYPH_ATTRIBUTE_BLINK      = 1 << 4,
     CE_TERMINAL_GLYPH_ATTRIBUTE_REVERSE    = 1 << 5,
     CE_TERMINAL_GLYPH_ATTRIBUTE_INVISIBLE  = 1 << 6,
     CE_TERMINAL_GLYPH_ATTRIBUTE_STRUCK     = 1 << 7,
     CE_TERMINAL_GLYPH_ATTRIBUTE_WRAP       = 1 << 8,
     CE_TERMINAL_GLYPH_ATTRIBUTE_WIDE       = 1 << 9,
     CE_TERMINAL_GLYPH_ATTRIBUTE_WDUMMY     = 1 << 10,
     CE_TERMINAL_GLYPH_ATTRIBUTE_BOLD_FAINT = CE_TERMINAL_GLYPH_ATTRIBUTE_BOLD | CE_TERMINAL_GLYPH_ATTRIBUTE_FAINT,
}CeTerminalGlyphAttribute_t;

typedef enum{
     CE_TERMINAL_CURSOR_MODE_SAVE,
     CE_TERMINAL_CURSOR_MODE_LOAD,
}CeTerminalCursorMode_t;

typedef enum{
     CE_TERMINAL_CURSOR_STATE_DEFAULT = 0,
     CE_TERMINAL_CURSOR_STATE_WRAPNEXT = 1,
     CE_TERMINAL_CURSOR_STATE_ORIGIN = 2,
}CeTerminalCursorState_t;

typedef enum{
     CE_TERMINAL_MODE_WRAP        = 1 << 0,
     CE_TERMINAL_MODE_INSERT      = 1 << 1,
     CE_TERMINAL_MODE_APPKEYPAD   = 1 << 2,
     CE_TERMINAL_MODE_ALTSCREEN   = 1 << 3,
     CE_TERMINAL_MODE_CRLF        = 1 << 4,
     CE_TERMINAL_MODE_MOUSEBTN    = 1 << 5,
     CE_TERMINAL_MODE_MOUSEMOTION = 1 << 6,
     CE_TERMINAL_MODE_REVERSE     = 1 << 7,
     CE_TERMINAL_MODE_KBDLOCK     = 1 << 8,
     CE_TERMINAL_MODE_HIDE        = 1 << 9,
     CE_TERMINAL_MODE_ECHO        = 1 << 10,
     CE_TERMINAL_MODE_APPCURSOR   = 1 << 11,
     CE_TERMINAL_MODE_MOUSEGR     = 1 << 12,
     CE_TERMINAL_MODE_8BIT        = 1 << 13,
     CE_TERMINAL_MODE_BLINK       = 1 << 14,
     CE_TERMINAL_MODE_FBLINK      = 1 << 15,
     CE_TERMINAL_MODE_FOCUS       = 1 << 16,
     CE_TERMINAL_MODE_MOUSEEX10   = 1 << 17,
     CE_TERMINAL_MODE_MOUSEEMANY  = 1 << 18,
     CE_TERMINAL_MODE_BRCKTPASTE  = 1 << 19,
     CE_TERMINAL_MODE_PRINT       = 1 << 20,
     CE_TERMINAL_MODE_UTF8        = 1 << 21,
     CE_TERMINAL_MODE_SIXEL       = 1 << 22,
     CE_TERMINAL_MODE_MOUSE       = 1 << 23,
}CeTerminalMode_t;

typedef enum{
     CE_TERMINAL_ESCAPE_STATE_START      = 1 << 0,
     CE_TERMINAL_ESCAPE_STATE_CSI        = 1 << 1,
     CE_TERMINAL_ESCAPE_STATE_STR        = 1 << 2,
     CE_TERMINAL_ESCAPE_STATE_ALTCHARSET = 1 << 3,
     CE_TERMINAL_ESCAPE_STATE_STR_END    = 1 << 4,
     CE_TERMINAL_ESCAPE_STATE_TEST       = 1 << 5,
     CE_TERMINAL_ESCAPE_STATE_UTF8       = 1 << 6,
     CE_TERMINAL_ESCAPE_STATE_DCS        = 1 << 7,
}CeTerminalEscapeState_t;

typedef struct{
     uint16_t attributes;
     int32_t foreground;
     int32_t background;
}CeTerminalGlyph_t;

typedef struct{
     CeTerminalGlyph_t attributes;
     int32_t x;
     int32_t y;
     uint8_t state;
}CeTerminalCursor_t;

typedef struct{
     char buffer[CE_TERMINAL_ESCAPE_BUFFER_SIZE];
     uint32_t buffer_length;
     char private;
     int arguments[CE_TERMINAL_ESCAPE_ARGUMENT_SIZE];
     uint32_t argument_count;
     char mode[2];
}CeTerminalCSIEscape_t;

typedef struct{
     char type;
     char buffer[CE_TERMINAL_ESCAPE_BUFFER_SIZE];
     uint32_t buffer_length;
     char* arguments[CE_TERMINAL_ESCAPE_ARGUMENT_SIZE];
     uint32_t argument_count;
}CeTerminalSTREscape_t;

typedef struct{
     int file_descriptor;
     int32_t rows;
     int32_t columns;
     CeTerminalGlyph_t** lines;
     CeTerminalGlyph_t** alternate_lines;
     bool* dirty_lines;
     CeTerminalCursor_t cursor;
     CeTerminalCursor_t save_cursor[2];
     int32_t top;
     int32_t bottom;
     CeTerminalMode_t mode;
     CeTerminalEscapeState_t escape_state;
     char translation_table[4];
     int32_t charset;
     int32_t selected_charset;
     bool numlock;
     int32_t* tabs;
     CeTerminalCSIEscape_t csi_escape;
     CeTerminalSTREscape_t str_escape;
}CeTerminal_t;

bool ce_terminal_init(CeTerminal_t* terminal, int64_t width, int64_t height);
void ce_terminal_free(CeTerminal_t* terminal);
bool ce_terminal_send_key(CeTerminal_t* terminal, CeRune_t key);
