#pragma once

#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <regex.h>
#include <dirent.h>

#define CE_NEWLINE '\n'
#define CE_TAB '\t'
#define CE_UTF8_INVALID -1
#define CE_UTF8_SIZE 4
#define CE_ASCII_PRINTABLE_CHARACTERS (127 - 32)

#define CE_CLAMP(a, min, max) (a = (a < min) ? min : (a > max) ? max : a);

#define COLOR_DEFAULT -1
#define COLOR_BRIGHT_BLACK 8
#define COLOR_BRIGHT_RED 9
#define COLOR_BRIGHT_GREEN 10
#define COLOR_BRIGHT_YELLOW 11
#define COLOR_BRIGHT_BLUE 12
#define COLOR_BRIGHT_MAGENTA 13
#define COLOR_BRIGHT_CYAN 14
#define COLOR_BRIGHT_WHITE 15

#define KEY_ESCAPE 27

typedef int32_t CeRune_t;

typedef enum{
     CE_UP = -1,
     CE_DOWN = 1
}CeDirection_t;

typedef enum{
     CE_CLAMP_X_NONE,
     CE_CLAMP_X_ON, // allows point at line length
     CE_CLAMP_X_INSIDE, // only allows point at (line length - 1)
}CeClampX_t;

typedef enum{
     CE_BUFFER_STATUS_NONE,
     CE_BUFFER_STATUS_MODIFIED,
     CE_BUFFER_STATUS_READONLY,
     CE_BUFFER_STATUS_NEW_FILE,
}CeBufferStatus_t;

typedef enum {
     CE_LINE_NUMBER_NONE,
     CE_LINE_NUMBER_ABSOLUTE,
     CE_LINE_NUMBER_RELATIVE,
     CE_LINE_NUMBER_ABSOLUTE_AND_RELATIVE,
}CeLineNumber_t;

typedef struct{
     int64_t x;
     int64_t y;
}CePoint_t;

typedef struct{
     int64_t left;
     int64_t right;
     int64_t top;
     int64_t bottom;
}CeRect_t;

typedef struct{
     bool chain;

     bool insertion; // opposite is deletion
     char* string;
     CePoint_t location;
     CePoint_t cursor_before;
     CePoint_t cursor_after;
}CeBufferChange_t;

typedef struct CeBufferChangeNode_t{
     CeBufferChange_t change;
     struct CeBufferChangeNode_t* next;
     struct CeBufferChangeNode_t* prev;
}CeBufferChangeNode_t;

typedef struct{
     char** lines;
     int64_t line_count;

     char* name;

     CeBufferStatus_t status;

     CePoint_t cursor_save;
     CePoint_t scroll_save;

     CeBufferChangeNode_t* change_node;
     CeBufferChangeNode_t* save_at_change_node;

     bool no_line_numbers;
     bool no_highlight_current_line;

     void* app_data; // TODO: this doesn't need to be a void*
     void* syntax_data;

     // NOTE: if we decide to do a buffer init hook, add config_data for user configs
}CeBuffer_t;

typedef struct{
     CeRect_t rect;
     CePoint_t scroll;

     CePoint_t cursor;

     CeBuffer_t* buffer;

     void* user_data;
}CeView_t;

typedef enum{
     CE_VISUAL_LINE_DISPLAY_TYPE_FULL_LINE, // emacs style
     CE_VISUAL_LINE_DISPLAY_TYPE_INCLUDE_NEWLINE, // vim style
     CE_VISUAL_LINE_DISPLAY_TYPE_EXCLUDE_NEWLINE, // worst style
}CeVisualLineDisplayType_t;

typedef struct{
     CeLineNumber_t line_number;
     int64_t tab_width;
     int64_t horizontal_scroll_off;
     int64_t vertical_scroll_off;
     int64_t terminal_scroll_back;
     bool insert_spaces_on_tab;
     CeVisualLineDisplayType_t visual_line_display_type;
     int ui_fg_color;
     int ui_bg_color;
     int64_t completion_line_limit;
     uint64_t message_display_time_usec;
     int message_fg_color;
     int message_bg_color;
     int apply_completion_key;
     int cycle_next_completion_key;
     int cycle_prev_completion_key;
}CeConfigOptions_t;

typedef struct CeRuneNode_t{
     CeRune_t rune;
     struct CeRuneNode_t* next;
}CeRuneNode_t;

typedef struct{
     CePoint_t point;
     int64_t length;
}CeRegexSearchResult_t;

typedef struct{
     CePoint_t point;
     char filepath[PATH_MAX];
}CeDestination_t;

typedef struct{
     CePoint_t start;
     CePoint_t end;
}CeRange_t;

bool ce_log_init(const char* filename);
void ce_log(const char* fmt, ...);
// lol we should have a ce_log_free() but honestly, whatever

bool ce_buffer_alloc(CeBuffer_t* buffer, int64_t line_count, const char* name);
void ce_buffer_free(CeBuffer_t* buffer);
bool ce_buffer_load_file(CeBuffer_t* buffer, const char* filename);
bool ce_buffer_load_string(CeBuffer_t* buffer, const char* string, const char* name);
bool ce_buffer_save(CeBuffer_t* buffer);
bool ce_buffer_empty(CeBuffer_t* buffer);

CeRune_t ce_buffer_get_rune(CeBuffer_t* buffer, CePoint_t point); // TODO: unittest
int64_t ce_buffer_range_len(CeBuffer_t* buffer, CePoint_t start, CePoint_t end); // inclusive
int64_t ce_buffer_line_len(CeBuffer_t* buffer, int64_t line);
CePoint_t ce_buffer_move_point(CeBuffer_t* buffer, CePoint_t point, CePoint_t delta, int64_t tab_width, CeClampX_t clamp_x); // TODO: unittest
CePoint_t ce_buffer_advance_point(CeBuffer_t* buffer, CePoint_t point, int64_t delta); // TODO: unittest
CePoint_t ce_buffer_clamp_point(CeBuffer_t* buffer, CePoint_t point, CeClampX_t clamp_x); // TODO: unittest
CePoint_t ce_buffer_end_point(CeBuffer_t* buffer);
bool ce_buffer_contains_point(CeBuffer_t* buffer, CePoint_t point);
int64_t ce_buffer_point_is_valid(CeBuffer_t* buffer, CePoint_t point); // like ce_buffer_contains_point(), but includes end of line as valid // TODO: unittest
CePoint_t ce_buffer_search_forward(CeBuffer_t* buffer, CePoint_t start, const char* pattern);
CePoint_t ce_buffer_search_backward(CeBuffer_t* buffer, CePoint_t start, const char* pattern);
CeRegexSearchResult_t ce_buffer_regex_search_forward(CeBuffer_t* buffer, CePoint_t start, const regex_t* regex);
CeRegexSearchResult_t ce_buffer_regex_search_backward(CeBuffer_t* buffer, CePoint_t start, const regex_t* regex);

char* ce_buffer_dupe_string(CeBuffer_t* buffer, CePoint_t point, int64_t length);
char* ce_buffer_dupe(CeBuffer_t* buffer);

bool ce_buffer_insert_string(CeBuffer_t* buffer, const char* string, CePoint_t point);
bool ce_buffer_insert_rune(CeBuffer_t* buffer, CeRune_t rune, CePoint_t point); // TODO: unittest
bool ce_buffer_remove_string(CeBuffer_t* buffer, CePoint_t point, int64_t length);
bool ce_buffer_remove_lines(CeBuffer_t* buffer, int64_t line_start, int64_t lines_to_remove); // TODO: remove from view?

// helper functions for common things I do
bool ce_buffer_insert_string_change(CeBuffer_t* buffer, char* alloced_string, CePoint_t point, CePoint_t* cursor_before,
                                    CePoint_t cursor_after, bool chain_undo);
bool ce_buffer_insert_string_change_at_cursor(CeBuffer_t* buffer, char* alloced_string, CePoint_t* cursor, bool chain_undo);
bool ce_buffer_remove_string_change(CeBuffer_t* buffer, CePoint_t point, int64_t remove_len, CePoint_t* cursor_before,
                                    CePoint_t cursor_after, bool chain_undo);

bool ce_buffer_change(CeBuffer_t* buffer, CeBufferChange_t* change); // TODO: unittest
bool ce_buffer_undo(CeBuffer_t* buffer, CePoint_t* cursor); // TODO: unittest
bool ce_buffer_redo(CeBuffer_t* buffer, CePoint_t* cursor); // TODO: unittest

void ce_view_follow_cursor(CeView_t* view, int64_t horizontal_scroll_off, int64_t vertical_scroll_off, int64_t tab_width);
void ce_view_scroll_to(CeView_t* view, CePoint_t point);
void ce_view_center(CeView_t* view);
int64_t ce_view_width(CeView_t* view);
int64_t ce_view_height(CeView_t* view);

int64_t ce_utf8_strlen(const char* string);
int64_t ce_utf8_strlen_between(const char* start, const char* end); // inclusive
int64_t ce_utf8_last_index(const char* string);
char* ce_utf8_iterate_to(char* string, int64_t index);
char* ce_utf8_iterate_to_include_end(char* string, int64_t index);
CeRune_t ce_utf8_decode(const char* string, int64_t* bytes_consumed);
CeRune_t ce_utf8_decode_reverse(const char* string, const char* string_start, int64_t* bytes_consumed);
bool ce_utf8_encode(CeRune_t u, char* string, int64_t string_len, int64_t* bytes_written);
int64_t ce_utf8_rune_len(CeRune_t u);

int64_t ce_util_count_string_lines(const char* string);
int64_t ce_util_string_index_to_visible_index(const char* string, int64_t character, int64_t tab_width);
int64_t ce_util_visible_index_to_string_index(const char* string, int64_t character, int64_t tab_width);

bool ce_point_after(CePoint_t a, CePoint_t b);
bool ce_points_equal(CePoint_t a, CePoint_t b);
bool ce_point_in_rect(CePoint_t a, CeRect_t r);

bool ce_rune_node_insert(CeRuneNode_t** head, CeRune_t rune);
CeRune_t* ce_rune_node_string(CeRuneNode_t* head);
void ce_rune_node_free(CeRuneNode_t** head);

char* ce_rune_string_to_char_string(const CeRune_t* int_str);
CeRune_t* ce_char_string_to_rune_string(const char* char_str);

bool ce_range_sort(CeRange_t* range);

int64_t ce_line_number_column_width(CeLineNumber_t line_number, int64_t buffer_line_count, int64_t view_top, int64_t view_bottom);
int64_t ce_count_digits(int64_t n);

CeRune_t ce_ctrl_key(char ch);

extern FILE* g_ce_log;
extern CeBuffer_t* g_ce_log_buffer;
