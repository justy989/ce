#pragma once

#include "ce.h"

#define APP_MAX_KEY_COUNT 16

typedef struct BufferNode_t{
     CeBuffer_t* buffer;
     struct BufferNode_t* next;
}BufferNode_t;

bool buffer_node_insert(BufferNode_t** head, CeBuffer_t* buffer);
bool buffer_node_delete(BufferNode_t** head, CeBuffer_t* buffer);
void buffer_node_free(BufferNode_t** head);

typedef struct StringNode_t{
     char* string;
     struct StringNode_t* next;
     struct StringNode_t* prev;
}StringNode_t;

StringNode_t* string_node_insert(StringNode_t** head, const char* string);
void string_node_free(StringNode_t** head);

typedef struct{
     StringNode_t* head;
     StringNode_t* current;
}History_t;

bool history_insert(History_t* history, const char* string);
char* history_previous(History_t* history);
char* history_next(History_t* history);
