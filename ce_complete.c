#include "ce_complete.h"

#include <string.h>
#include <stdlib.h>

bool ce_complete_init(CeComplete_t* complete, const char** strings, int64_t string_count){
     ce_complete_free(complete);

     complete->elements = calloc(string_count, sizeof(*complete->elements));
     if(!complete->elements) return false;

     for(int64_t i = 0; i < string_count; i++){
          complete->elements[i].string = strdup(strings[i]);
          if(!complete->elements[i].string) return false;
     }

     complete->count = string_count;
     return true;
}

void ce_complete_match(CeComplete_t* complete, const char* match){
     for(int64_t i = 0; i < complete->count; i++){
          const char* str = strstr(complete->elements[i].string, match);
          complete->elements[i].match = (str != NULL);
     }

     // if our current no longer matches, find another that matches
     if(complete->current >= 0){
          if(!complete->elements[complete->current].match){
               bool found_match = false;

               for(int64_t i = complete->current + 1; i != complete->current; i++){
                    if(i > complete->count) i = 0;
                    if(complete->elements[i].match){
                         complete->current = i;
                         found_match = true;
                         break;
                    }
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

void ce_complete_next_match(CeComplete_t* complete){
     if(complete->current < 0) return;

     for(int64_t i = complete->current + 1; i != complete->current; i++){
          if(i >= complete->count) i = 0;
          if(complete->elements[i].match){
               complete->current = i;
               break;
          }
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
     }
}

void ce_complete_free(CeComplete_t* complete){
     for(int64_t i = 0; i < complete->count; i++){
          free(complete->elements[i].string);
     }

     free(complete->elements);
     free(complete->current_match);
     memset(complete, 0, sizeof(*complete));
     complete->current = -1;
}
