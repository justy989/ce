#pragma once

#include <stdbool.h>
#include <stdint.h>

// NOTE: assumes all entries are unique

typedef struct{
     char* string;
     bool match;
}CeCompleteElement_t;

typedef struct{
     CeCompleteElement_t* elements;
     int64_t count;
     char* current_match;
     int64_t current;
}CeComplete_t;

bool ce_complete_init(CeComplete_t* complete, const char** strings, int64_t string_count);
void ce_complete_reset(CeComplete_t* complete); // reset as if no matching has been done
void ce_complete_match(CeComplete_t* complete, const char* match);
void ce_complete_next_match(CeComplete_t* complete);
void ce_complete_previous_match(CeComplete_t* complete);
void ce_complete_free(CeComplete_t* complete);
