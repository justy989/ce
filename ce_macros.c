#include "ce_macros.h"

bool ce_macros_begin_recording(CeMacros_t* macros, unsigned char reg){
     if(reg < 33 || reg >= 177) return false; // ascii printable character range
     macros->recording = reg;
     ce_rune_node_free(macros->rune_head + (reg - '!'));
     return true;
}

void ce_macros_record_key(CeMacros_t* macros, CeRune_t key){
     if(macros->recording < 33 || macros->recording >= 177) return;
     ce_rune_node_insert(macros->rune_head + (macros->recording - '!'), key);
}

void ce_macros_end_recording(CeMacros_t* macros){
     macros->recording = 0;
}

bool ce_macros_is_recording(CeMacros_t* macros){
     return (macros->recording >= 33 && macros->recording < 177);
}
