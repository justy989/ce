#include "ce_macros.h"

void ce_macros_free(CeMacros_t* macros){
     for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
          ce_rune_node_free(macros->rune_head + i);
     }
}

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

CeRune_t* ce_macros_get_register_string(CeMacros_t* macros, unsigned char reg){
     if(reg < 33 || reg >= 177) return 0; // NULL, why don't I have it defined here?
     return ce_rune_node_string(macros->rune_head[reg - '!']);
}
