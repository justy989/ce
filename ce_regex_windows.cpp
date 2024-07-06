// g++ -DDISPLAY_TERMINAL -Wall -Wshadow -Wextra -Wno-unused-parameter -std=c++11 -ggdb3 -c -o build/term/ce_regex_windows.o ce_regex_windows.cpp
#include "ce_regex.h"

#include <regex>

CeRegexResult_t ce_regex_init(const char* expression,
                              CeRegex_t* regex_handle) {
     CeRegexResult_t result = {};
     std::regex* obj = nullptr;
     try{
          obj = new std::regex(expression);
     }catch(const std::regex_error& error){
          result.error_message = strdup(error.what());
          return result;
     }
     *regex_handle = obj;
     return result;
}

CeRegexResult_t ce_regex_match(CeRegex_t regex_handle,
                               const char* string) {
     std::regex* obj = reinterpret_cast<std::regex*>(regex_handle);
     // TODO: Check that handle is valid.
     CeRegexResult_t result = {};
     std::cmatch match;
     if(std::regex_search(string, match, *obj)){
          if(match.size() <= 0){
               result.error_message =
                    strdup("regex_search() says match exists, but has empty matches");
          }else{
               result.match_start = match.position(0);
               result.match_length = match.length(0);
          }
     }else{
          result.match_start = CE_REGEX_NO_MATCH;
          result.match_length = CE_REGEX_NO_MATCH;
     }
     return result;
}

void ce_regex_free(CeRegex_t regex_handle) {
     if(regex_handle){
          delete reinterpret_cast<std::regex*>(regex_handle);
     }
}

