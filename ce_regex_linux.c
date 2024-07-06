#include "ce_regex.h"

#include <regex.h>
#include <stdlib.h>
#include <string.h>

CeRegexResult_t ce_regex_init(const char* expression,
                              CeRegex_t* regex_handle) {
     CeRegexResult_t result = {};
     regex_t* regex = (regex_t*)(malloc(sizeof(regex_t)));
     if(regex == NULL){
          result.error_message = strdup("Failed to allocate regex_t");
          return result;
     }
     int rc = regcomp(regex, expression, REG_EXTENDED);
     if (rc != 0){
          result.error_message = malloc(CE_REGEX_MAX_ERROR_SIZE);
          regerror(rc, regex, result.error_message, CE_REGEX_MAX_ERROR_SIZE);
          free(regex);
          return result;
     }
     *regex_handle = regex;
     return result;
}

CeRegexResult_t ce_regex_match(CeRegex_t regex_handle,
                               const char* string) {
     regex_t* regex = (regex_t*)(regex_handle);
     // TODO: Check that handle is valid.
     CeRegexResult_t result = {};
     const size_t match_count = 1;
     regmatch_t matches[match_count];
     int rc = regexec(regex, string, match_count, matches, 0);
     if(rc == 0){
          result.match_start = matches[0].rm_so;
          result.match_length = matches[0].rm_eo - matches[0].rm_so;
     }else if(rc == REG_NOMATCH){
          result.match_start = CE_REGEX_NO_MATCH;
          result.match_length = CE_REGEX_NO_MATCH;
     }else{
          result.error_message = malloc(CE_REGEX_MAX_ERROR_SIZE);
          regerror(rc, regex, result.error_message, CE_REGEX_MAX_ERROR_SIZE);
     }
     return result;
}

void ce_regex_free(CeRegex_t regex_handle) {
     if(regex_handle != NULL){
          free(regex_handle);
     }
}
