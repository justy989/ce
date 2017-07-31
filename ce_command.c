#include "ce_command.h"
#include "ce.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char* eat_blanks(const char* string)
{
     while(*string){
          if(!isblank(*string)) break;
          string++;
     }

     return string;
}

static const char* find_end_of_arg(const char* string)
{
     bool quote = (*string == '"');

     if(quote){
          // find the next quote, ignoring backslashed quotes
          char prev = 0;
          while(*string){
               prev = *string;
               string++;
               if(*string == '"' && prev != '\\'){
                    const char* end = string + 1;
                    if(*end) return end;
                    return NULL;
               }
          }
          return NULL;
     }else{
          return strchr(string, ' ');
     }
}

static bool parse_arg(CeCommandArg_t* arg, const char* string)
{
     if(*string == 0) return false;

     bool digits_only = true;
     bool decimal = false;
     const char* itr = string;

     while(*itr){
          if(!isdigit(*itr)){
               if(*itr == '.'){
                    if(decimal) return false;
                    decimal = true;
               }else{
                    digits_only = false;
               }
          }
          itr++;
     }

     if(digits_only){
          if(decimal){
               arg->type = CE_COMMAND_ARG_DECIMAL;
               arg->decimal = atof(string);
          }else{
               arg->type = CE_COMMAND_ARG_INTEGER;
               arg->integer = atoi(string);
          }
     }else{
          // skip over the first quote if one is there
          if(string[0] == '"') string++;

          arg->type = CE_COMMAND_ARG_STRING;
          arg->string = strdup(string);
          int64_t len = strlen(arg->string);

          // overwrite last quote with null terminator
          if(arg->string[len - 1] == '"') arg->string[len - 1] = 0;
     }

     return true;
}

void ce_command_entry_log(CeCommandEntry_t* entry)
{
     // TODO: get rid of this function
     ce_log("%s %s", entry->name, entry->description);
}

bool ce_command_parse(CeCommand_t* command, const char* string)
{
     if(*string == 0) return false;

     const char* start = NULL;
     const char* end = NULL;

     int64_t arg_count = 0;
     start = eat_blanks(string);

     // count the args
     while(*start){
          end = find_end_of_arg(start);
          arg_count++;
          if(!end) break;
          start = eat_blanks(end + 1);
     }

     // account for command at the beginning
     arg_count--;

     start = eat_blanks(string);
     end = find_end_of_arg(start);

     if(!end){
          command->args = NULL;
          command->arg_count = 0;

          int64_t len = strlen(start);
          if(len >= CE_COMMAND_NAME_MAX_LEN){
               ce_log("error: in command '%s' command name is greater than max %d characters", string, CE_COMMAND_NAME_MAX_LEN);
               return false;
          }

          strcpy(command->name, start);
          return true;
     }

     // copy the command name
     int64_t len = end - start;
     if(len >= CE_COMMAND_NAME_MAX_LEN){
          ce_log("error: in command '%s' command name is greater than max %d characters", string, CE_COMMAND_NAME_MAX_LEN);
          return false;
     }

     strncpy(command->name, start, len);
     command->name[len] = 0;

     // exit early if there are no arguments
     if(arg_count == 0){
          command->args = NULL;
          command->arg_count = 0;
          return true;
     }

     start = eat_blanks(end + 1);

     // allocate the arguments
     command->args = malloc(arg_count * sizeof(*command->args));
     if(!command->args) return false;
     command->arg_count = arg_count;

     CeCommandArg_t* arg = command->args;

     // parse the individual args
     while(*start){
          end = find_end_of_arg(start);
          if(end){
               int64_t arg_len = end - start;
               char buffer[arg_len + 1];
               memset(buffer, 0, arg_len + 1);
               buffer[arg_len] = 0;

               strncpy(buffer, start, arg_len);
               if(!parse_arg(arg, buffer)){
                    ce_command_free(command);
                    return false;
               }
          }else{
               int64_t arg_len = strlen(start);
               char buffer[arg_len + 1];
               memset(buffer, 0, arg_len + 1);
               strcpy(buffer, start);
               buffer[arg_len] = 0;
               if(!parse_arg(arg, buffer)){
                    ce_command_free(command);
                    return false;
               }
               break;
          }

          start = eat_blanks(end + 1);
          arg++;
     }

     return true;
}

void ce_command_free(CeCommand_t* command)
{
     memset(command, 0, CE_COMMAND_NAME_MAX_LEN);

     for(int64_t i = 0; i < command->arg_count; ++i){
          if(command->args[i].type == CE_COMMAND_ARG_STRING){
               free(command->args[i].string);
          }
     }

     free(command->args);
     command->args = NULL;
     command->arg_count = 0;
}

void ce_command_log(CeCommand_t* command)
{
     ce_log("command: '%s', %ld args", command->name, command->arg_count);

     for(int64_t i = 0; i < command->arg_count; ++i){
          CeCommandArg_t* arg = command->args + i;
          switch(arg->type){
          default:
               break;
          case CE_COMMAND_ARG_INTEGER:
               ce_log("  %ld", arg->integer);
               break;
          case CE_COMMAND_ARG_DECIMAL:
               ce_log("  %f", arg->decimal);
               break;
          case CE_COMMAND_ARG_STRING:
               ce_log("  '%s'", arg->string);
               break;
          }
     }
}
