#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CE_NEWLINE '\n'
#define CE_TAB '\t'
#define CE_UTF8_INVALID -1

typedef enum{
     CE_UP = -1,
     CE_DOWN = 1
}CeDirection_t;

typedef enum{
     CE_BUFFER_STATUS_NONE,
     CE_BUFFER_STATUS_MODIFIED,
     CE_BUFFER_STATUS_READONLY,
     CE_BUFFER_STATUS_NEW_FILE,
}CeBufferStatus_t;

typedef enum{
     CE_BUFFER_FILE_TYPE_PLAIN,
     CE_BUFFER_FILE_TYPE_C,
     CE_BUFFER_FILE_TYPE_CPP,
     CE_BUFFER_FILE_TYPE_PYTHON,
     CE_BUFFER_FILE_TYPE_JAVA,
     CE_BUFFER_FILE_TYPE_BASH,
     CE_BUFFER_FILE_TYPE_CONFIG,
     CE_BUFFER_FILE_TYPE_DIFF,
     CE_BUFFER_FILE_TYPE_TERMINAL,
}CeBufferFileType_t;

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
     char** lines;
     int64_t line_count;

     char* name;
     void* user_data;

     CeBufferStatus_t status;
     CeBufferFileType_t type;

     CePoint_t cursor;
}CeBuffer_t;

typedef struct{
     CeRect_t rect;
     CePoint_t scroll;

     CePoint_t cursor;

     CeBuffer_t* buffer;

     void* user_data;
}CeView_t;

typedef int32_t CeRune_t;

bool ce_buffer_alloc(CeBuffer_t* buffer, int64_t line_count, const char* name);
void ce_buffer_free(CeBuffer_t* buffer);
bool ce_buffer_load_file(CeBuffer_t* buffer, const char* filename);
bool ce_buffer_load_string(CeBuffer_t* buffer, const char* string, const char* name);
bool ce_buffer_empty(CeBuffer_t* buffer);

//bool ce_insert_string(CeBuffer_t* buffer, CePoint_t point, const char* string);
//bool ce_remove_string(CeBuffer_t* buffer, CePoint_t point, int64_t length, bool remove_line_if_empty);

int64_t ce_buffer_range_len(CeBuffer_t* buffer, CePoint_t start, CePoint_t end);
int64_t ce_buffer_line_len(CeBuffer_t* buffer, int64_t line);
int64_t ce_buffer_move_point(CeBuffer_t* buffer, CePoint_t point, CePoint_t delta, int64_t tab_width);

int64_t ce_buffer_contains_point(CeBuffer_t* buffer, CePoint_t point);

int64_t ce_utf8_strlen(const char* string);
CeRune_t ce_utf8_decode(const char* string, int64_t* bytes_consumed);

int64_t ce_util_count_string_lines(const char* string);
