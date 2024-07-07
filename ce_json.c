#include "ce_json.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct{
     uint64_t printed;
     uint64_t remaining;
}PrintState_t;

static void _obj_free(CeJsonObj_t* a);
static void _obj_copy(CeJsonObj_t* a, CeJsonObj_t* b);
static void _obj_print(CeJsonObj_t* obj, char* string, PrintState_t* print_state, uint64_t current_indent,
                       uint64_t indent);
static void _array_free(CeJsonArray_t* a);
static void _array_print(CeJsonArray_t* obj, char* string, PrintState_t* print_state,
                         uint64_t current_indent, uint64_t indent);

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
          _print_state_update(print_state,
                              snprintf(string + print_state->printed, print_state->remaining,
                                       "\n"));
          _obj_print(&value->obj, string, print_state, current_indent, indent);
          break;
     case CE_JSON_TYPE_ARRAY:
          _print_state_update(print_state,
                              snprintf(string + print_state->printed, print_state->remaining,
                                       "\n"));
          _array_print(&value->array, string, print_state, current_indent, indent);
          break;
     case CE_JSON_TYPE_STRING:
          _print_state_update(print_state,
                              snprintf(string + print_state->printed, print_state->remaining,
                                       "%s", value->string));
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
     _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining, "%s[\n", indentation));

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
     _print_state_update(print_state, snprintf(string + print_state->printed, print_state->remaining, "%s{\n", indentation));
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
     array->values[array->count].type = CE_JSON_TYPE_OBJECT;
     _obj_copy(&array->values[array->count].obj, obj);
     array->count = new_count;
}

void ce_json_array_add_string(CeJsonArray_t* array, const char* string){
     uint64_t new_count = array->count + 1;
     array->values = realloc(array->values, new_count * sizeof(array->values[0]));
     array->values[array->count].type = CE_JSON_TYPE_STRING;
     array->values[array->count].string = strdup(string);
     array->count = new_count;
}

void ce_json_array_add_number(CeJsonArray_t* array, double number){
     uint64_t new_count = array->count + 1;
     array->values = realloc(array->values, new_count * sizeof(array->values[0]));
     array->values[array->count].type = CE_JSON_TYPE_NUMBER;
     array->values[array->count].number = number;
     array->count = new_count;
}

void ce_json_array_add_boolean(CeJsonArray_t* array, bool boolean){
     uint64_t new_count = array->count + 1;
     array->values = realloc(array->values, new_count * sizeof(array->values[0]));
     array->values[array->count].type = CE_JSON_TYPE_BOOL;
     array->values[array->count].boolean = boolean;
     array->count = new_count;
}

void ce_json_array_add_null(CeJsonArray_t* array){
     uint64_t new_count = array->count + 1;
     array->values = realloc(array->values, new_count * sizeof(array->values[0]));
     array->values[array->count].type = CE_JSON_TYPE_NULL;
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
     PARSE_STAGE_START,
     PARSE_STAGE_FIELD,
     PARSE_STAGE_FIELD_NAME_BEGIN,
     PARSE_STAGE_FIELD_NAME_END,
     PARSE_STAGE_FIELD_VALUE_BEGIN,
     PARSE_STAGE_FIELD_VALUE_END,
     PARSE_STAGE_STOP,
}ObjParseStage_t;

typedef struct{
     uint64_t x;
     uint64_t y;
}ParsePos_t;

typedef struct{
     ObjParseStage_t stage;
     char* field_name;
     ParsePos_t pos;
}ObjParseState_t;

static inline bool _is_whitespace(char ch){
     return ch == ' ' || ch == '\t';
}

static inline bool _is_newline(char ch){
     return ch == '\n' || ch == '\r';
}

static const char* _eat_whitespace(ParsePos_t* pos, const char* str){
     while(true){
          if(_is_whitespace(*str)){
               pos->x++;
          }else if(_is_newline(*str)){
               pos->y++;
               pos->x = 0;
          }else{
               break;
          }
          str++;
     }
     return str;
}

static const char* _parse_string(ParsePos_t* pos, const char* str, char** result){
     const char* begin = str;
     bool escaped = false;
     while(true){
          char ch = *str;
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
                    return str;
               }
               escaped = false;
          }else if(ch == TOKEN_BACKSLASH){
               escaped = true;
          }else if(ch == TOKEN_QUOTE){
               if(*result){
                    free(*result);
               }
               uint64_t string_length = (str - begin);
               *result = malloc(string_length + 1);
               strncpy(*result, begin, string_length);
               (*result)[string_length] = 0;
               break;
          }
          pos->x++;
          str++;
     }
     return str;
}

static const char* _parse_number(ParsePos_t* pos, const char* str, double* result, bool* success){
     char* end = NULL;
     *result = strtod(str, &end);
     if(str == end){
          *success = false;
     }else{
          *success = true;
          uint64_t string_len = end - str;
          pos->x += string_len;
     }
     return end;
}

bool _obj_parse(const char* string, ParsePos_t pos, CeJsonObj_t* obj){
     ObjParseState_t state = {};
     state.pos = pos;
     while(*string){
          switch(state.stage){
          case PARSE_STAGE_START:
               string = _eat_whitespace(&state.pos, string);
               if(*string == TOKEN_OPEN_BRACE){
                    state.stage = PARSE_STAGE_FIELD;
                    printf("%d -> %d due to %c at %ld, %ld\n", PARSE_STAGE_START, PARSE_STAGE_FIELD,
                           TOKEN_OPEN_BRACE, state.pos.x, state.pos.y);
               }else{
                    printf("Error: %ld, %ld expected first non-whitespace character is not an open brace.\n",
                           state.pos.x, state.pos.y);
                    return false;
               }
               break;
          case PARSE_STAGE_FIELD:
               string = _eat_whitespace(&state.pos, string);
               if(*string == TOKEN_QUOTE){
                    state.stage = PARSE_STAGE_FIELD_NAME_BEGIN;
                    printf("%d -> %d due to %c at %ld, %ld\n", PARSE_STAGE_FIELD, PARSE_STAGE_FIELD_NAME_BEGIN,
                           TOKEN_QUOTE, state.pos.x, state.pos.y);
               }else{
                    printf("Error: %ld, %ld expected first non-whitespace character is not an open brace.\n",
                           state.pos.x, state.pos.y);
                    return false;
               }
               break;
          case PARSE_STAGE_FIELD_NAME_BEGIN:
          {
               string = _parse_string(&state.pos, string, &state.field_name);
               if(*string == TOKEN_QUOTE){
                    state.stage = PARSE_STAGE_FIELD_NAME_END;
                    printf("%d -> %d due to %c at %ld, %ld extracting field name %s\n",
                           PARSE_STAGE_FIELD_NAME_BEGIN, PARSE_STAGE_FIELD_NAME_END,
                           TOKEN_QUOTE, state.pos.x, state.pos.y, state.field_name);
               }else{
                    printf("Error: %ld, %ld expected \" to start string.\n", state.pos.x, state.pos.y);
                    return false;
               }
          } break;
          case PARSE_STAGE_FIELD_NAME_END:
               string = _eat_whitespace(&state.pos, string);
               if(*string == TOKEN_COLON){
                    state.stage = PARSE_STAGE_FIELD_VALUE_BEGIN;
                    printf("%d -> %d due to %c at %ld, %ld\n",
                           PARSE_STAGE_FIELD_NAME_END, PARSE_STAGE_FIELD_VALUE_BEGIN,
                           TOKEN_COLON, state.pos.x, state.pos.y);
               }else{
                    printf("Error: %ld, %ld expected \" to start string.\n", state.pos.x, state.pos.y);
                    return false;
               }
               break;
          case PARSE_STAGE_FIELD_VALUE_BEGIN:
               string = _eat_whitespace(&state.pos, string);
               if(*string == TOKEN_OPEN_BRACE){
                    // TODO
               }else if(*string == TOKEN_MINUS ||
                        (*string >= '0' && *string <= '9')){
                    bool success = false;
                    double number = 0;
                    string = _parse_number(&state.pos, string, &number, &success);
                    if(success){
                         state.stage = PARSE_STAGE_FIELD_VALUE_END;
                         printf("%d -> %d due to at %ld, %ld\n",
                                PARSE_STAGE_FIELD_VALUE_BEGIN, PARSE_STAGE_FIELD_VALUE_END,
                                state.pos.x, state.pos.y);
                         ce_json_obj_set_number(obj, state.field_name, number);
                    }else{
                         printf("Error: %ld, %ld failed to parse expected number.\n",
                                state.pos.x, state.pos.y);
                         return false;
                    }
               }else if(*string == TOKEN_OPEN_BRACKET){
                    // TODO
               }else if(*string == TOKEN_QUOTE){
                    string++;
                    state.pos.x++;
                    char* field_string = NULL;
                    string = _parse_string(&state.pos, string, &field_string);
                    if(*string == TOKEN_QUOTE){
                         state.stage = PARSE_STAGE_FIELD_VALUE_END;
                         printf("%d -> %d due to %c at %ld, %ld, parsed string field value %s\n",
                                PARSE_STAGE_FIELD_VALUE_BEGIN, PARSE_STAGE_FIELD_VALUE_END,
                                TOKEN_QUOTE, state.pos.x, state.pos.y,
                                field_string);
                         ce_json_obj_set_string(obj, state.field_name, field_string);
                         free(field_string);
                    }
               }else{
                    printf("Error: %ld, %ld failed to detect value.\n", state.pos.x, state.pos.y);
                    return false;
               }
               break;
          case PARSE_STAGE_FIELD_VALUE_END:
               string = _eat_whitespace(&state.pos, string);
               if(*string == TOKEN_CLOSE_BRACE){
                    // TODO
                    state.stage = PARSE_STAGE_STOP;
                    printf("%d -> %d due to %c at %ld, %ld\n",
                           PARSE_STAGE_FIELD_VALUE_END, PARSE_STAGE_STOP,
                           TOKEN_CLOSE_BRACE, state.pos.x, state.pos.y);
               }else if(*string == TOKEN_COMMA){
                    state.stage = PARSE_STAGE_FIELD;
                    printf("%d -> %d due to %c at %ld, %ld\n",
                           PARSE_STAGE_FIELD_VALUE_END, PARSE_STAGE_FIELD_NAME_BEGIN,
                           TOKEN_COMMA, state.pos.x, state.pos.y);
               }else{
                    printf("Error: %ld, %ld failed to parse next element or end of object.\n",
                           state.pos.x, state.pos.y);
                    return false;
               }
               break;
          case PARSE_STAGE_STOP:
               printf("Done Parsing.\n");
               break;
          }

          string++;
          state.pos.x++;
     }
     return true;
}

bool ce_json_parse(const char* string, CeJsonObj_t* json){
     bool result = _obj_parse(string, (ParsePos_t){0, 0}, json);
     return result;
}
