#include "ce_json.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct ParsePos_s ParsePos_t;

typedef struct{
     uint64_t printed;
     uint64_t remaining;
}PrintState_t;

static void _obj_copy(CeJsonObj_t* a, CeJsonObj_t* b);
static void _obj_print(CeJsonObj_t* obj, char* string, PrintState_t* print_state, uint64_t current_indent,
                       uint64_t indent);
static bool _obj_parse(ParsePos_t* pos, CeJsonObj_t* obj, bool verbose);
static void _obj_free(CeJsonObj_t* a);

static void _array_print(CeJsonArray_t* obj, char* string, PrintState_t* print_state,
                         uint64_t current_indent, uint64_t indent);
static void _array_free(CeJsonArray_t* a);

static void _value_free(CeJsonValue_t* value){
     switch(value->type){
     case CE_JSON_TYPE_OBJECT:
          _obj_free(&value->obj);
          break;
     case CE_JSON_TYPE_ARRAY:
          _array_free(&value->array);
          break;
     case CE_JSON_TYPE_STRING:
          free(value->string);
          break;
     default:
          break;
     }
     value->type = CE_JSON_TYPE_NONE;
}

static void _field_free(CeJsonField_t* field){
     _value_free(&field->value);
     free(field->name);
     field->name = NULL;
}

static void _array_copy(CeJsonArray_t* a, CeJsonArray_t* b){
     _array_free(a);
     for(uint64_t i = 0; i < b->count; i++){
          CeJsonValue_t* value = b->values + i;
          switch(value->type){
          case CE_JSON_TYPE_OBJECT:
               ce_json_array_add_obj(a, &value->obj);
               break;
          case CE_JSON_TYPE_ARRAY:
               assert(!"array of arrays not yet supported.");
               break;
          case CE_JSON_TYPE_STRING:
               ce_json_array_add_string(a, value->string);
               break;
          case CE_JSON_TYPE_NUMBER:
               ce_json_array_add_number(a, value->number);
               break;
          case CE_JSON_TYPE_BOOL:
               ce_json_array_add_boolean(a, value->boolean);
               break;
          case CE_JSON_TYPE_NULL:
               ce_json_array_add_null(a);
               break;
          default:
               break;
          }
     }
}

static void _array_free(CeJsonArray_t* a){
     for(uint64_t i = 0; i < a->count; i++){
          CeJsonValue_t* value = a->values + i;
          _value_free(value);
     }
     free(a->values);
     a->values = NULL;
}

static void _obj_copy(CeJsonObj_t* a, CeJsonObj_t* b){
     _obj_free(a);

     for(uint64_t i = 0; i < b->count; i++){
          CeJsonField_t* field = b->fields + i;
          switch(field->value.type){
          case CE_JSON_TYPE_OBJECT:
               ce_json_obj_set_obj(a, field->name, &field->value.obj);
               break;
          case CE_JSON_TYPE_ARRAY:
               ce_json_obj_set_array(a, field->name, &field->value.array);
               break;
          case CE_JSON_TYPE_STRING:
               ce_json_obj_set_string(a, field->name, field->value.string);
               break;
          case CE_JSON_TYPE_NUMBER:
               ce_json_obj_set_number(a, field->name, field->value.number);
               break;
          case CE_JSON_TYPE_BOOL:
               ce_json_obj_set_boolean(a, field->name, field->value.boolean);
               break;
          case CE_JSON_TYPE_NULL:
               ce_json_obj_set_null(a, field->name);
               break;
          default:
               break;
          }
     }
}

static void _obj_free(CeJsonObj_t* obj){
     for(uint64_t i = 0; i < obj->count; i++){
          CeJsonField_t* field = obj->fields + i;
          _field_free(field);
     }
     free(obj->fields);
     obj->fields = NULL;
     obj->count = 0;
}

static void _obj_free_field_of_name(CeJsonObj_t* obj, const char* name){
     for(uint64_t i = 0; i < obj->count; i++){
          CeJsonField_t* field = obj->fields + i;
          if (strcmp(field->name, name) != 0){
               continue;
          }
          _field_free(field);
     }
}

static void _print_state_update(PrintState_t* print_state, uint64_t count){
     print_state->printed += count;
     print_state->remaining -= count;
}

static void _make_indentation(char* indentation, uint64_t max_size, uint64_t current_indent){
     uint64_t size = (current_indent < (max_size - 1)) ? current_indent : (max_size - 1);
     for(uint64_t i = 0; i < size; i++){
          indentation[i] = ' ';
     }
     indentation[size] = 0;
}

static void _value_print(CeJsonValue_t* value, char* string, PrintState_t* print_state,
                         uint64_t current_indent, uint64_t indent){
     switch(value->type){
     default:
          break;
     case CE_JSON_TYPE_OBJECT:
          _obj_print(&value->obj, string, print_state, current_indent, indent);
          break;
     case CE_JSON_TYPE_ARRAY:
          _array_print(&value->array, string, print_state, current_indent, indent);
          break;
     case CE_JSON_TYPE_STRING:
          _print_state_update(print_state,
                              snprintf(string + print_state->printed, print_state->remaining,
                                       "\"%s\"", value->string));
          break;
     case CE_JSON_TYPE_NUMBER:
          _print_state_update(print_state,
                              snprintf(string + print_state->printed, print_state->remaining,
                                       "%.4f", value->number));
          break;
     case CE_JSON_TYPE_BOOL:
          if(value->boolean){
               _print_state_update(print_state,
                                   snprintf(string + print_state->printed, print_state->remaining, "true"));
          }else{
               _print_state_update(print_state,
                                   snprintf(string + print_state->printed, print_state->remaining, "false"));
          }
          break;
     case CE_JSON_TYPE_NULL:
          _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining, "null"));
          break;
     }
}

static void _field_print(CeJsonField_t* field, char* string, PrintState_t* print_state,
                         uint64_t current_indent, uint64_t indent){
     char indentation[MAX_INDENTATION];
     _make_indentation(indentation, MAX_INDENTATION, current_indent);
     _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining,
                                               "%s\"%s\" : ", indentation, field->name));
     _value_print(&field->value, string, print_state, current_indent, indent);
}

static void _array_print(CeJsonArray_t* array, char* string, PrintState_t* print_state,
                         uint64_t current_indent, uint64_t indent){
     char indentation[MAX_INDENTATION];
     _make_indentation(indentation, MAX_INDENTATION, current_indent);
     _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining, "[\n"));

     char field_indentation[MAX_INDENTATION];
     _make_indentation(field_indentation, MAX_INDENTATION, current_indent + indent);
     // This is fine to wrap arround because we won't go into the loop.
     uint64_t last_index = (array->count - 1);
     for(uint64_t i = 0; i < array->count; i++){
          CeJsonValue_t* value = array->values + i;
          _print_state_update(print_state, snprintf(string + print_state->printed,
                                                    print_state->remaining, "%s",
                                                    field_indentation));
          _value_print(value, string, print_state, current_indent + indent, indent);
          if(i != last_index){
               _print_state_update(print_state, snprintf(string + print_state->printed,
                                                         print_state->remaining, ",\n"));
          }else{
               _print_state_update(print_state, snprintf(string + print_state->printed,
                                                         print_state->remaining, "\n"));
          }
     }
     _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining, "%s]", indentation));
}

static void _obj_print(CeJsonObj_t* obj, char* string, PrintState_t* print_state, uint64_t current_indent,
                           uint64_t indent){
     char indentation[MAX_INDENTATION];
     _make_indentation(indentation, MAX_INDENTATION, current_indent);
     _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining, "{\n"));
     // This is fine to wrap arround because we won't go into the loop.
     uint64_t last_index = (obj->count - 1);
     for(uint64_t i = 0; i < obj->count; i++){
          CeJsonField_t* field = obj->fields + i;
          _field_print(field, string, print_state, current_indent + indent, indent);
          if(i != last_index){
               _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining, ",\n"));
          }else{
               _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining, "\n"));
          }
     }
     _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining, "%s}", indentation));
}

void ce_json_array_add_obj(CeJsonArray_t* array, CeJsonObj_t* obj){
     uint64_t new_count = array->count + 1;
     array->values = realloc(array->values, new_count * sizeof(array->values[0]));
     CeJsonValue_t* new_value = array->values + array->count;
     memset(new_value, 0, sizeof(*new_value));
     new_value->type = CE_JSON_TYPE_OBJECT;
     _obj_copy(&new_value->obj, obj);
     array->count = new_count;
}

void ce_json_array_add_string(CeJsonArray_t* array, const char* string){
     uint64_t new_count = array->count + 1;
     array->values = realloc(array->values, new_count * sizeof(array->values[0]));
     CeJsonValue_t* new_value = array->values + array->count;
     memset(new_value, 0, sizeof(*new_value));
     new_value->type = CE_JSON_TYPE_STRING;
     new_value->string = strdup(string);
     array->count = new_count;
}

void ce_json_array_add_number(CeJsonArray_t* array, double number){
     uint64_t new_count = array->count + 1;
     array->values = realloc(array->values, new_count * sizeof(array->values[0]));
     CeJsonValue_t* new_value = array->values + array->count;
     memset(new_value, 0, sizeof(*new_value));
     new_value->type = CE_JSON_TYPE_NUMBER;
     new_value->number = number;
     array->count = new_count;
}

void ce_json_array_add_boolean(CeJsonArray_t* array, bool boolean){
     uint64_t new_count = array->count + 1;
     array->values = realloc(array->values, new_count * sizeof(array->values[0]));
     CeJsonValue_t* new_value = array->values + array->count;
     memset(new_value, 0, sizeof(*new_value));
     new_value->type = CE_JSON_TYPE_BOOL;
     new_value->boolean = boolean;
     array->count = new_count;
}

void ce_json_array_add_null(CeJsonArray_t* array){
     uint64_t new_count = array->count + 1;
     array->values = realloc(array->values, new_count * sizeof(array->values[0]));
     CeJsonValue_t* new_value = array->values + array->count;
     memset(new_value, 0, sizeof(*new_value));
     new_value->type = CE_JSON_TYPE_NULL;
     array->count = new_count;
}

void ce_json_array_free(CeJsonArray_t* array){
     _array_free(array);
}

void ce_json_obj_set_obj(CeJsonObj_t* obj, const char* name, CeJsonObj_t* member_obj){
     _obj_free_field_of_name(obj, name);
     uint64_t new_count = obj->count + 1;
     obj->fields = realloc(obj->fields, new_count * sizeof(obj->fields[0]));
     CeJsonField_t* new_field = obj->fields + obj->count;
     memset(new_field, 0, sizeof(*new_field));
     new_field->name = strdup(name);
     new_field->value.type = CE_JSON_TYPE_OBJECT;
     _obj_copy(&new_field->value.obj, member_obj);
     obj->count = new_count;
}

void ce_json_obj_set_array(CeJsonObj_t* obj, const char* name, CeJsonArray_t* array){
     uint64_t new_count = obj->count + 1;
     obj->fields = realloc(obj->fields, new_count * sizeof(obj->fields[0]));
     CeJsonField_t* new_field = obj->fields + obj->count;
     memset(new_field, 0, sizeof(*new_field));
     new_field->name = strdup(name);
     new_field->value.type = CE_JSON_TYPE_ARRAY;
     _array_copy(&new_field->value.array, array);
     obj->count = new_count;
}

void ce_json_obj_set_string(CeJsonObj_t* obj, const char* name, const char* string){
     _obj_free_field_of_name(obj, name);
     uint64_t new_count = obj->count + 1;
     obj->fields = realloc(obj->fields, new_count * sizeof(obj->fields[0]));
     CeJsonField_t* new_field = obj->fields + obj->count;
     memset(new_field, 0, sizeof(*new_field));
     new_field->name = strdup(name);
     new_field->value.type = CE_JSON_TYPE_STRING;
     new_field->value.string = strdup(string);
     obj->count = new_count;
}

void ce_json_obj_set_number(CeJsonObj_t* obj, const char* name, double number){
     _obj_free_field_of_name(obj, name);
     uint64_t new_count = obj->count + 1;
     obj->fields = realloc(obj->fields, new_count * sizeof(obj->fields[0]));
     CeJsonField_t* new_field = obj->fields + obj->count;
     memset(new_field, 0, sizeof(*new_field));
     new_field->name = strdup(name);
     new_field->value.type = CE_JSON_TYPE_NUMBER;
     new_field->value.number = number;
     obj->count = new_count;
}

void ce_json_obj_set_boolean(CeJsonObj_t* obj, const char* name, bool boolean){
     _obj_free_field_of_name(obj, name);
     uint64_t new_count = obj->count + 1;
     obj->fields = realloc(obj->fields, new_count * sizeof(obj->fields[0]));
     CeJsonField_t* new_field = obj->fields + obj->count;
     memset(new_field, 0, sizeof(*new_field));
     new_field->name = strdup(name);
     new_field->value.type = CE_JSON_TYPE_BOOL;
     new_field->value.boolean = boolean;
     obj->count = new_count;
}

void ce_json_obj_set_null(CeJsonObj_t* obj, const char* name){
     _obj_free_field_of_name(obj, name);
     uint64_t new_count = obj->count + 1;
     obj->fields = realloc(obj->fields, new_count * sizeof(obj->fields[0]));
     CeJsonField_t* new_field = obj->fields + obj->count;
     memset(new_field, 0, sizeof(*new_field));
     new_field->name = strdup(name);
     new_field->value.type = CE_JSON_TYPE_NULL;
     obj->count = new_count;
}

void ce_json_obj_free(CeJsonObj_t* json){
     _obj_free(json);
}

void ce_json_obj_to_string(CeJsonObj_t* json, char* string, uint64_t size, uint64_t indent){
     PrintState_t print_state = {};
     print_state.remaining = size;
     _obj_print(json, string, &print_state, 0, indent);
     _print_state_update(&print_state, snprintf(string + print_state.printed, print_state.remaining, "\n"));
}

#define TOKEN_OPEN_BRACE '{'
#define TOKEN_CLOSE_BRACE '}'
#define TOKEN_OPEN_BRACKET '['
#define TOKEN_CLOSE_BRACKET ']'
#define TOKEN_QUOTE '"'
#define TOKEN_COLON ':'
#define TOKEN_COMMA ','
#define TOKEN_BACKSLASH '\\'
#define TOKEN_FORWARDSLASH '/'
#define TOKEN_MINUS '-'

typedef enum{
     ARR_PARSE_STAGE_START,
     ARR_PARSE_STAGE_VALUE_BEGIN,
     ARR_PARSE_STAGE_CHECK_NEXT_VALUE,
}ArrayParseStage_t;

typedef enum{
     OBJ_PARSE_STAGE_START,
     OBJ_PARSE_STAGE_FIELD,
     OBJ_PARSE_STAGE_FIELD_NAME_BEGIN,
     OBJ_PARSE_STAGE_FIELD_NAME_END,
     OBJ_PARSE_STAGE_FIELD_VALUE_BEGIN,
     OBJ_PARSE_STAGE_FIELD_VALUE_END,
}ObjParseStage_t;

typedef struct ParsePos_s{
     const char* str;
     uint64_t x;
     uint64_t y;
}ParsePos_t;

typedef struct ObjParseState_s{
     ObjParseStage_t stage;
     char* field_name;
}ObjParseState_t;

static inline void advance_parse_pos(ParsePos_t* pos){
     pos->x++;
     pos->str++;
}

static inline void advance_parse_pos_len(ParsePos_t* pos, uint64_t len){
     pos->x += len;
     pos->str += len;
}

static inline void advance_newline_parse_pos(ParsePos_t* pos){
     pos->y++;
     pos->x = 1;
     pos->str++;
}

static inline bool _is_whitespace(char ch){
     return ch == ' ' || ch == '\t';
}

static inline bool _is_newline(char ch){
     return ch == '\n' || ch == '\r';
}

static void _eat_whitespace(ParsePos_t* pos){
     while(true){
          if(_is_whitespace(*pos->str)){
               advance_parse_pos(pos);
          }else if(_is_newline(*pos->str)){
               advance_newline_parse_pos(pos);
          }else{
               break;
          }
     }
}

static bool _parse_string(ParsePos_t* pos, char** result){
     const char* begin = pos->str;
     bool escaped = false;
     while(true){
          char ch = *pos->str;
          if(_is_newline(ch)){
               return false;
          }else if(escaped){
               if(ch != TOKEN_QUOTE &&
                  ch != TOKEN_BACKSLASH &&
                  ch != TOKEN_FORWARDSLASH &&
                  ch != 'b' &&
                  ch != 'f' &&
                  ch != 'n' &&
                  ch != 'r' &&
                  ch != 't' &&
                  ch != 'u'){
                    // TODO: Handle unicode numbers after the \u
                    return pos->str;
               }
               escaped = false;
          }else if(ch == TOKEN_BACKSLASH){
               escaped = true;
          }else if(ch == TOKEN_QUOTE){
               if(*result){
                    free(*result);
               }
               uint64_t string_length = (pos->str - begin);
               *result = malloc(string_length + 1);
               strncpy(*result, begin, string_length);
               (*result)[string_length] = 0;
               break;
          }
          advance_parse_pos(pos);
     }
     return true;
}

static bool _starts_number(char ch){
     return ch == TOKEN_MINUS || (ch >= '0' && ch <= '9');
}

static bool _parse_number(ParsePos_t* pos, double* result){
     char* end = NULL;
     *result = strtod(pos->str, &end);
     if(pos->str >= end){
          return false;
     }
     uint64_t string_len = (end - pos->str);
     // Go to the last number in the string, so that when we advance in the state machine, we can
     // consume the next token.
     advance_parse_pos_len(pos, string_len - 1);
     return true;
}

static bool _parse_boolean(ParsePos_t* pos, bool* value){
     if(strncmp(pos->str, "true", 4) == 0){
          *value = true;
          advance_parse_pos_len(pos, 3);
          return true;
     }
     if(strncmp(pos->str, "false", 5) == 0){
          *value = false;
          advance_parse_pos_len(pos, 4);
          return true;
     }
     return false;
}

static bool _parse_null(ParsePos_t* pos){
     if(strncmp(pos->str, "null", 4) == 0){
          advance_parse_pos_len(pos, 3);
          return true;
     }
     return false;
}

static void _transition_array_stage(ArrayParseStage_t* current_stage, ArrayParseStage_t new_stage,
                                    ParsePos_t* pos, bool verbose){
     if(verbose){
          printf("array parse stage change: %d -> %d at %ld, %ld at %c\n",
                 *current_stage, new_stage, pos->x, pos->y, *pos->str);
     }
     *current_stage = new_stage;
}

static void _transition_obj_stage(ObjParseStage_t* current_stage, ObjParseStage_t new_stage,
                                  ParsePos_t* pos, bool verbose){
     if(verbose){
          printf("obj parse stage change: %d -> %d at %ld, %ld at %c\n",
                 *current_stage, new_stage, pos->x, pos->y, *pos->str);
     }
     *current_stage = new_stage;
}

static bool _array_parse(ParsePos_t* pos, CeJsonArray_t* array, bool verbose){
     ArrayParseStage_t stage = ARR_PARSE_STAGE_START;
     while(*pos->str){
          switch(stage){
          case ARR_PARSE_STAGE_START:
               _eat_whitespace(pos);
               if(*pos->str == TOKEN_OPEN_BRACKET){
                    _transition_array_stage(&stage, ARR_PARSE_STAGE_VALUE_BEGIN, pos, verbose);
               }else{
                    printf("Error: %ld, %ld expected first non-whitespace character is not an open brace.\n",
                           pos->x, pos->y);
                    return false;
               }
               break;
          case ARR_PARSE_STAGE_VALUE_BEGIN:
          {
               bool boolean = false;
               _eat_whitespace(pos);
               if(*pos->str == TOKEN_OPEN_BRACE){
                    CeJsonObj_t obj = {};
                    bool success = _obj_parse(pos, &obj, verbose);
                    if(success){
                         _transition_array_stage(&stage, ARR_PARSE_STAGE_CHECK_NEXT_VALUE, pos, verbose);
                         if(verbose) printf("adding array obj element\n");
                         ce_json_array_add_obj(array, &obj);
                         ce_json_obj_free(&obj);
                    }else{
                         printf("Error: %ld, %ld failed to parse object.", pos->x, pos->y);
                         return false;
                    }
               }else if(*pos->str == TOKEN_QUOTE){
                    advance_parse_pos(pos);
                    char* field_string = NULL;
                    bool success = _parse_string(pos, &field_string);
                    if(success){
                         _transition_array_stage(&stage, ARR_PARSE_STAGE_CHECK_NEXT_VALUE, pos, verbose);
                         if(verbose) printf("adding array string element\n");
                         ce_json_array_add_string(array, field_string);
                         free(field_string);
                    }else{
                         printf("Error: %ld, %ld failed to parse string.", pos->x, pos->y);
                         return false;
                    }
               }else if(_starts_number(*pos->str)){
                    double number = 0;
                    bool success = _parse_number(pos, &number);
                    if(success){
                         _transition_array_stage(&stage, ARR_PARSE_STAGE_CHECK_NEXT_VALUE, pos, verbose);
                         if(verbose) printf("adding array number element\n");
                         ce_json_array_add_number(array, number);
                    }else{
                         printf("Error: %ld, %ld failed to parse expected number.\n",
                                pos->x, pos->y);
                         return false;
                    }
               }else if(_parse_boolean(pos, &boolean)){
                    _transition_array_stage(&stage, ARR_PARSE_STAGE_CHECK_NEXT_VALUE, pos, verbose);
                    if(verbose) printf("adding array bool element\n");
                    ce_json_array_add_boolean(array, boolean);
               }else if(_parse_null(pos)){
                    _transition_array_stage(&stage, ARR_PARSE_STAGE_CHECK_NEXT_VALUE, pos, verbose);
                    if(verbose) printf("adding array null element\n");
                    ce_json_array_add_null(array);
               }else if(*pos->str == TOKEN_CLOSE_BRACKET){
                    if(verbose) printf("done parsing array\n");
                    return true;
               }else{
                    printf("Error: %ld, %ld expected value in array.\n",
                           pos->x, pos->y);
                    return false;
               }
          } break;
          case ARR_PARSE_STAGE_CHECK_NEXT_VALUE:
               _eat_whitespace(pos);
               if(*pos->str == TOKEN_COMMA){
                    _transition_array_stage(&stage, ARR_PARSE_STAGE_VALUE_BEGIN, pos, verbose);
               }else if(*pos->str == TOKEN_CLOSE_BRACKET){
                    if(verbose) printf("done parsing array\n");
                    return true;
               }
               break;
          default:
               printf("Error: reached unknown state %d\n", stage);
               return false;
          }

          if(_is_newline(*pos->str)){
               advance_newline_parse_pos(pos);
          }else{
               advance_parse_pos(pos);
          }
     }
     return true;
}

static bool _obj_parse(ParsePos_t* pos, CeJsonObj_t* obj, bool verbose){
     ObjParseState_t state = {};
     while(*pos->str){
          switch(state.stage){
          case OBJ_PARSE_STAGE_START:
               _eat_whitespace(pos);
               if(*pos->str == TOKEN_OPEN_BRACE){
                    _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD, pos, verbose);
               }else{
                    printf("Error: %ld, %ld expected first non-whitespace character is not an open brace.\n",
                           pos->x, pos->y);
                    return false;
               }
               break;
          case OBJ_PARSE_STAGE_FIELD:
               _eat_whitespace(pos);
               if(*pos->str == TOKEN_QUOTE){
                    _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD_NAME_BEGIN, pos, verbose);
               }else if(*pos->str == TOKEN_CLOSE_BRACE){
                    if(verbose) printf("Done parsing obj.\n");
                    return true;
               }else{
                    printf("Error: %ld, %ld expected field declaration.\n",
                           pos->x, pos->y);
                    return false;
               }
               break;
          case OBJ_PARSE_STAGE_FIELD_NAME_BEGIN:
          {
               _parse_string(pos, &state.field_name);
               if(*pos->str == TOKEN_QUOTE){
                    _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD_NAME_END, pos, verbose);
               }else{
                    printf("Error: %ld, %ld expected \" to start string.\n", pos->x, pos->y);
                    return false;
               }
          } break;
          case OBJ_PARSE_STAGE_FIELD_NAME_END:
               _eat_whitespace(pos);
               if(*pos->str == TOKEN_COLON){
                    _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD_VALUE_BEGIN, pos, verbose);
               }else{
                    printf("Error: %ld, %ld expected \" to start string.\n", pos->x, pos->y);
                    return false;
               }
               break;
          case OBJ_PARSE_STAGE_FIELD_VALUE_BEGIN:
          {
               bool boolean = false;
               _eat_whitespace(pos);
               if(*pos->str == TOKEN_OPEN_BRACE){
                    CeJsonObj_t member_obj = {};
                    if(!_obj_parse(pos, &member_obj, verbose)){
                         return false;
                    }
                    if(verbose) printf("adding obj obj field %s\n", state.field_name);
                    ce_json_obj_set_obj(obj, state.field_name, &member_obj);
                    ce_json_obj_free(&member_obj);
                    _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD_VALUE_END, pos, verbose);
               }else if(_starts_number(*pos->str)){
                    double number = 0;
                    bool success = _parse_number(pos, &number);
                    if(success){
                         _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD_VALUE_END, pos, verbose);
                         if(verbose) printf("adding obj number field %s\n", state.field_name);
                         ce_json_obj_set_number(obj, state.field_name, number);
                    }else{
                         printf("Error: %ld, %ld failed to parse expected number.\n",
                                pos->x, pos->y);
                         return false;
                    }
               }else if(*pos->str == TOKEN_OPEN_BRACKET){
                    CeJsonArray_t array = {};
                    bool success = _array_parse(pos, &array, verbose);
                    if(success){
                         _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD_VALUE_END, pos, verbose);
                         if(verbose) printf("adding obj array field %s\n", state.field_name);
                         ce_json_obj_set_array(obj, state.field_name, &array);
                         ce_json_array_free(&array);
                    }else{
                         printf("Error: %ld, %ld failed to parse array.\n", pos->x, pos->y);
                         return false;
                    }
               }else if(*pos->str == TOKEN_QUOTE){
                    advance_parse_pos(pos);
                    char* field_string = NULL;
                    bool success = _parse_string(pos, &field_string);
                    if(success){
                         _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD_VALUE_END, pos, verbose);
                         if(verbose) printf("adding obj string field %s\n", state.field_name);
                         ce_json_obj_set_string(obj, state.field_name, field_string);
                         free(field_string);
                    }else{
                         printf("Error: %ld, %ld failed to parse string.", pos->x, pos->y);
                    }
               }else if(_parse_boolean(pos, &boolean)){
                    _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD_VALUE_END, pos, verbose);
                    if(verbose) printf("adding obj boolean field %s\n", state.field_name);
                    ce_json_obj_set_boolean(obj, state.field_name, boolean);
               }else if(_parse_null(pos)){
                    _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD_VALUE_END, pos, verbose);
                    if(verbose) printf("adding obj null field %s\n", state.field_name);
                    ce_json_obj_set_null(obj, state.field_name);
               }else{
                    printf("Error: %ld, %ld failed to detect value.\n", pos->x, pos->y);
                    return false;
               }
          } break;
          case OBJ_PARSE_STAGE_FIELD_VALUE_END:
               _eat_whitespace(pos);
               if(*pos->str == TOKEN_CLOSE_BRACE){
                    if(verbose) printf("done parsing obj.\n");
                    return true;
               }else if(*pos->str == TOKEN_COMMA){
                    _transition_obj_stage(&state.stage, OBJ_PARSE_STAGE_FIELD, pos, verbose);
               }else{
                    printf("Error: %ld, %ld failed to parse next element or end of object with %c.\n",
                           pos->x, pos->y, *pos->str);
                    return false;
               }
               break;
          default:
               printf("Error: reached unknown state %d\n", state.stage);
               return false;
          }

          if(_is_newline(*pos->str)){
               advance_newline_parse_pos(pos);
          }else{
               advance_parse_pos(pos);
          }
     }
     return true;
}

bool ce_json_parse(const char* string, CeJsonObj_t* json, bool verbose){
     ParsePos_t parse_pos = {};
     parse_pos.x = 1;
     parse_pos.y = 1;
     parse_pos.str = string;
     bool result = _obj_parse(&parse_pos, json, verbose);
     if(!result){
          ce_json_obj_free(json);
     }
     return result;
}
