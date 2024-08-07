#pragma once

// https://www.json.org/json-en.html

// TODO:
// - Convert printf() to ce_log()

#include <stdint.h>
#include <stdbool.h>

#define MAX_INDENTATION 1024

typedef struct CeJsonValue_s CeJsonValue_t;
typedef struct CeJsonField_s CeJsonField_t;
typedef struct CeJsonObj_s CeJsonObj_t;
typedef struct CeJsonArray_s CeJsonArray_t;

typedef enum{
     CE_JSON_TYPE_NONE,
     CE_JSON_TYPE_OBJECT,
     CE_JSON_TYPE_ARRAY,
     CE_JSON_TYPE_STRING,
     CE_JSON_TYPE_NUMBER,
     CE_JSON_TYPE_BOOL,
     CE_JSON_TYPE_NULL,
}CeJsonType_t;

typedef struct CeJsonObj_s{
     CeJsonField_t* fields;
     int64_t count;
}CeJsonObj_t;

typedef struct CeJsonArray_s{
     CeJsonValue_t* values;
     int64_t count;
}CeJsonArray_t;

typedef struct CeJsonValue_s{
     CeJsonType_t type;
     union{
          CeJsonObj_t obj;
          CeJsonArray_t array;
          char* string;
          double number;
          bool boolean;
     };
}CeJsonValue_t;

typedef struct CeJsonField_s{
     char* name;
     CeJsonValue_t value;
}CeJsonField_t;

typedef struct{
     CeJsonType_t type;
     int64_t index;
}CeJsonFindResult_t;

void ce_json_array_add_obj(CeJsonArray_t* array, CeJsonObj_t* obj);
void ce_json_array_add_string(CeJsonArray_t* array, const char* string);
void ce_json_array_add_number(CeJsonArray_t* array, double number);
void ce_json_array_add_boolean(CeJsonArray_t* array, bool boolean);
void ce_json_array_add_null(CeJsonArray_t* array);
void ce_json_array_free(CeJsonArray_t* array);

void ce_json_obj_set_obj(CeJsonObj_t* obj, const char* name, CeJsonObj_t* member_obj);
void ce_json_obj_set_array(CeJsonObj_t* obj, const char* name, CeJsonArray_t* array);
void ce_json_obj_set_string(CeJsonObj_t* obj, const char* name, const char* string);
void ce_json_obj_set_number(CeJsonObj_t* obj, const char* name, double number);
void ce_json_obj_set_boolean(CeJsonObj_t* obj, const char* name, bool boolean);
void ce_json_obj_set_null(CeJsonObj_t* obj, const char* name);
void ce_json_obj_to_string(CeJsonObj_t* json, char* string, uint64_t size, uint64_t indent);
void ce_json_obj_free(CeJsonObj_t* json);

CeJsonFindResult_t ce_json_obj_find(CeJsonObj_t* json, const char* name);
CeJsonObj_t* ce_json_obj_get_obj(CeJsonObj_t* json, const CeJsonFindResult_t* find);
CeJsonArray_t* ce_json_obj_get_array(CeJsonObj_t* json, const CeJsonFindResult_t* find);
const char* ce_json_obj_get_string(CeJsonObj_t* json, const CeJsonFindResult_t* find);
double* ce_json_obj_get_number(CeJsonObj_t* json, const CeJsonFindResult_t* find);
bool* ce_json_obj_get_boolean(CeJsonObj_t* json, const CeJsonFindResult_t* find);

bool ce_json_parse(const char* string, CeJsonObj_t* json, bool verbose);
