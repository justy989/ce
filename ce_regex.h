#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif
typedef void* CeRegex_t;

#define CE_REGEX_NO_MATCH -1
#define CE_REGEX_MAX_ERROR_SIZE 256

typedef struct{
     // must be free'd if populated
     char* error_message;
     // Set to CE_REGEX_NO_MATCH if no matches were found.
     int64_t match_start;
     int64_t match_length;
}CeRegexResult_t;

CeRegexResult_t ce_regex_init(const char* expression,
                              CeRegex_t* regex_handle);

CeRegexResult_t ce_regex_match(CeRegex_t regex_handle,
                               const char* string);

void ce_regex_free(CeRegex_t regex_handle);
#if defined(__cplusplus)
}
#endif
