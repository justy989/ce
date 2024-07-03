#pragma once

#include <inttypes.h>
#include <stdbool.h>

#define CE_COMMAND_NAME_MAX_LEN 512
#define CE_COMMAND_ARG_MAX_LEN 1024

typedef enum{
     CE_COMMAND_SUCCESS,
     CE_COMMAND_FAILURE,
     CE_COMMAND_PRINT_HELP,
     CE_COMMAND_NO_ACTION, // not handled, but also not a failure
}CeCommandStatus_t;

typedef enum{
     CE_COMMAND_ARG_INTEGER,
     CE_COMMAND_ARG_DECIMAL,
     CE_COMMAND_ARG_STRING,
     CE_COMMAND_ARG_COUNT
}CeCommandArgType_t;

typedef struct{
     CeCommandArgType_t type;

     union{
          int64_t integer;
          double decimal;
          char* string;
     };
}CeCommandArg_t;

typedef struct{
     char name[CE_COMMAND_NAME_MAX_LEN];
     CeCommandArg_t* args;
     int64_t arg_count;
}CeCommand_t;

typedef CeCommandStatus_t CeCommandFunc_t (CeCommand_t*, void*);

typedef struct{
     CeCommandFunc_t* func;
     const char* name;
     const char* description;
}CeCommandEntry_t;

bool ce_command_parse(CeCommand_t* command, const char* string);
void ce_command_free(CeCommand_t* command);
void ce_command_log(CeCommand_t* command);
