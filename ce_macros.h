#include "ce.h"

typedef struct{
     CeRuneNode_t* rune_head[CE_ASCII_PRINTABLE_CHARACTERS];
     unsigned char recording;
}CeMacros_t;

void ce_macros_free(CeMacros_t* macros);
bool ce_macros_begin_recording(CeMacros_t* macros, unsigned char reg);
void ce_macros_record_key(CeMacros_t* macros, CeRune_t key);
void ce_macros_end_recording(CeMacros_t* macros);
bool ce_macros_is_recording(CeMacros_t* macros);
CeRune_t* ce_macros_get_register_string(CeMacros_t* macros, unsigned char reg);
