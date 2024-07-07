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

bool ce_json_parse(const char* string, CeJsonObj_t* json){
     return true;
}

void ce_json_to_string(CeJsonObj_t* json, char* string, uint64_t size, uint64_t indent){
     PrintState_t print_state = {};
     print_state.remaining = size;
     _obj_print(json, string, &print_state, 0, indent);
     _print_state_update(&print_state, snprintf(string + print_state.printed, print_state.remaining, "\n"));
}
