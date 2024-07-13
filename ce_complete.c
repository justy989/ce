#include "ce_complete.h"

#include <string.h>
#include <stdlib.h>

bool ce_complete_init(CeComplete_t* complete, const char** strings, const char** descriptions, int64_t string_count){
     ce_complete_free(complete);

     complete->elements = calloc(string_count, sizeof(*complete->elements));
     if(!complete->elements) return false;

     for(int64_t i = 0; i < string_count; i++){
          complete->elements[i].string = strdup(strings[i]);
          if(descriptions && descriptions[i]){
               complete->elements[i].description = strdup(descriptions[i]);
          }
          if(!complete->elements[i].string) return false;
          complete->elements[i].match = true;
     }

     complete->count = string_count;
     return true;
}

void ce_complete_reset(CeComplete_t* complete){
     free(complete->current_match);
     complete->current_match = NULL;
     complete->current = 0;

     for(int64_t i = 0; i < complete->count; i++){
          complete->elements[i].match = true;
     }
}

void ce_complete_match(CeComplete_t* complete, const char* match){
     if(complete->count == 0) return;
     if(strlen(match)){
          for(int64_t i = 0; i < complete->count; i++){
               const char* str = strstr(complete->elements[i].string, match);
               complete->elements[i].match = (str != NULL);

               // any options that are an identical match, override the current selection
               if(strcmp(match, complete->elements[i].string) == 0) complete->current = i;
          }
     }else{
          for(int64_t i = 0; i < complete->count; i++){
               complete->elements[i].match = true;
          }
     }

     // if our current no longer matches, find another that matches
     if(complete->current >= 0){
          if(!complete->elements[complete->current].match){
               bool found_match = false;

               for(int64_t i = complete->current + 1; i != complete->current; i++){
                    if(i >= complete->count) i = 0;
                    if(complete->elements[i].match){
                         complete->current = i;
                         found_match = true;
                         break;
                    }

                    if(i == 0 && complete->current == 0) break;
               }

               // if no matches, use invalid index
               if(!found_match) complete->current = -1;
          }
     }else{
          for(int64_t i = 0; i < complete->count; i++){
               if(complete->elements[i].match){
                    complete->current = i;
                    break;
               }
          }
     }

     if(complete->current >= 0){
          free(complete->current_match);
          complete->current_match = strdup(match);
     }
}

int64_t ce_complete_current_match(CeComplete_t* complete){
     int64_t result = 0;
     for(int64_t i = 0; i < complete->count; i++){
          if(i == complete->current){
               return result;
          }
          if(complete->elements[i].match){
               result++;
          }
     }
     return 0;
}

void ce_complete_next_match(CeComplete_t* complete){
     if(complete->current < 0) return;

     for(int64_t i = complete->current + 1; i != complete->current; i++){
          if(i >= complete->count) i = 0;
          if(complete->elements[i].match){
               complete->current = i;
               break;
          }

          if(i == 0 && complete->current == 0) break;
     }
}

void ce_complete_previous_match(CeComplete_t* complete){
     if(complete->current < 0) return;

     for(int64_t i = complete->current - 1; i != complete->current; i--){
          if(i < 0) i = complete->count - 1;
          if(complete->elements[i].match){
               complete->current = i;
               break;
          }

          if(i == complete->count - 1 && complete->current == complete->count - 1) break;
     }
}

void ce_complete_free(CeComplete_t* complete){
     for(int64_t i = 0; i < complete->count; i++){
          free(complete->elements[i].string);
          free(complete->elements[i].description);
     }

     free(complete->elements);
     free(complete->current_match);
     memset(complete, 0, sizeof(*complete));
}
