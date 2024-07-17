// gcc -Wall -Werror -std=gnu11 -ggdb3 ce_json.c test_ce_json.c -o test_ce_json
#include "ce_json.h"

#include <stdio.h>
#include <stdlib.h>

#define PRINT_LEN (1024 * 1024)

void print_obj(CeJsonObj_t* obj){
     char* printed_obj = malloc(PRINT_LEN);
     memset(printed_obj, 0, (PRINT_LEN - 1));
     ce_json_obj_to_string(obj, printed_obj, PRINT_LEN, 1);
     free(printed_obj);
}

int main(int argc, char** argv){
     bool verbose = false;

     // Build object manually.
     {
          CeJsonObj_t person = {};
          ce_json_obj_set_string(&person, "first_name", "Justin");
          ce_json_obj_set_string(&person, "last_name", "Gratis");
          ce_json_obj_set_number(&person, "age", 53.2);
          ce_json_obj_set_boolean(&person, "is_cool", false);
          ce_json_obj_set_null(&person, "has_fun");

          CeJsonArray_t projects = {};
          ce_json_array_add_string(&projects, "reader");
          ce_json_array_add_string(&projects, "writer");
          ce_json_array_add_string(&projects, "syncer");
          ce_json_array_add_string(&projects, "commercial");

          CeJsonObj_t experience = {};
          ce_json_obj_set_array(&experience, "projects", &projects);
          ce_json_obj_set_number(&experience, "years", 18.0);
          ce_json_obj_set_obj(&person, "experience", &experience);

          print_obj(&person);

          ce_json_obj_free(&experience);
          ce_json_array_free(&projects);
          ce_json_obj_free(&person);
     }

     // Parse from string
     {
          const char* json_str =
               "{\n"
               "  \"first_name\" : \"Avacado\",\n"
               "  \"last_name\" : \"Vardaro\",\n"
               "  \"age\" : 83.5,\n"
               "  \"experience\" :\n"
               "  {\n"
               "    \"projects\" :\n"
               "    [\n"
               "      \"recorder\",\n"
               "      \"sim\",\n"
               "      \"build\"\n"
               "    ],\n"
               "    \"years\" : 72.4\n"
               "  },\n"
               "  \"is_cool\" : true,"
               "  \"has_fun\" : null"
               "}\n";

          printf("json:\n%s\n", json_str);
          CeJsonObj_t obj = {};
          if(ce_json_parse(json_str, &obj, verbose)){
               print_obj(&obj);
               ce_json_obj_free(&obj);
          }
     }

     // Parse internet string.
     {
          const char* json_str =
               "{\n"
               "  \"jsonrpc\": \"2.0\",\n"
               "  \"id\": 0,\n"
               "  \"method\": \"initialize\",\n"
               "  \"params\": {\n"
               "    \"processId\": 3877617,\n"
               "    \"rootPath\": \"/home/malintha/Documents/wso2/experimental/projects/ballerina/error-constructor\",\n"
               "    \"rootUri\": \"file:///home/malintha/Documents/wso2/experimental/projects/ballerina/error-constructor\",\n"
               "    \"initializationOptions\": {\n"
               "      \"enableSemanticHighlighting\": true\n"
               "    },\n"
               "    \"capabilities\": {\n"
               "      \"workspace\": {\n"
               "        \"applyEdit\": true,\n"
               "        \"workspaceEdit\": {\n"
               "          \"documentChanges\": true,\n"
               "          \"resourceOperations\": [\n"
               "            \"create\",\n"
               "            \"rename\",\n"
               "            \"delete\"\n"
               "          ],\n"
               "          \"failureHandling\": \"textOnlyTransactional\"\n"
               "        },\n"
               "        \"didChangeConfiguration\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"didChangeWatchedFiles\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"symbol\": {\n"
               "          \"symbolKind\": {\n"
               "            \"valueSet\": [\n"
               "              1,\n"
               "              2,\n"
               "              3,\n"
               "              4,\n"
               "              5,\n"
               "              6,\n"
               "              7,\n"
               "              8,\n"
               "              9,\n"
               "              10,\n"
               "              11,\n"
               "              12,\n"
               "              13,\n"
               "              14,\n"
               "              15,\n"
               "              16,\n"
               "              17,\n"
               "              18,\n"
               "              19,\n"
               "              20,\n"
               "              21,\n"
               "              22,\n"
               "              23,\n"
               "              24,\n"
               "              25,\n"
               "              26\n"
               "            ]\n"
               "          },\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"executeCommand\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"workspaceFolders\": true,\n"
               "        \"configuration\": true\n"
               "      },\n"
               "      \"textDocument\": {\n"
               "        \"synchronization\": {\n"
               "          \"willSave\": true,\n"
               "          \"willSaveWaitUntil\": true,\n"
               "          \"didSave\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"completion\": {\n"
               "          \"completionItem\": {\n"
               "            \"snippetSupport\": true,\n"
               "            \"commitCharactersSupport\": true,\n"
               "            \"documentationFormat\": [\n"
               "              \"markdown\",\n"
               "              \"plaintext\"\n"
               "            ],\n"
               "            \"deprecatedSupport\": true,\n"
               "            \"preselectSupport\": true,\n"
               "            \"tagSupport\": {\n"
               "              \"valueSet\": [\n"
               "                1\n"
               "              ]\n"
               "            }\n"
               "          },\n"
               "          \"completionItemKind\": {\n"
               "            \"valueSet\": [\n"
               "              1,\n"
               "              2,\n"
               "              3,\n"
               "              4,\n"
               "              5,\n"
               "              6,\n"
               "              7,\n"
               "              8,\n"
               "              9,\n"
               "              10,\n"
               "              11,\n"
               "              12,\n"
               "              13,\n"
               "              14,\n"
               "              15,\n"
               "              16,\n"
               "              17,\n"
               "              18,\n"
               "              19,\n"
               "              20,\n"
               "              21,\n"
               "              22,\n"
               "              23,\n"
               "              24,\n"
               "              25\n"
               "            ]\n"
               "          },\n"
               "          \"contextSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"hover\": {\n"
               "          \"contentFormat\": [\n"
               "            \"markdown\",\n"
               "            \"plaintext\"\n"
               "          ],\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"signatureHelp\": {\n"
               "          \"signatureInformation\": {\n"
               "            \"documentationFormat\": [\n"
               "              \"markdown\",\n"
               "              \"plaintext\"\n"
               "            ],\n"
               "            \"parameterInformation\": {\n"
               "              \"labelOffsetSupport\": true\n"
               "            }\n"
               "          },\n"
               "          \"contextSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"references\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"documentHighlight\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"documentSymbol\": {\n"
               "          \"symbolKind\": {\n"
               "            \"valueSet\": [\n"
               "              1,\n"
               "              2,\n"
               "              3,\n"
               "              4,\n"
               "              5,\n"
               "              6,\n"
               "              7,\n"
               "              8,\n"
               "              9,\n"
               "              10,\n"
               "              11,\n"
               "              12,\n"
               "              13,\n"
               "              14,\n"
               "              15,\n"
               "              16,\n"
               "              17,\n"
               "              18,\n"
               "              19,\n"
               "              20,\n"
               "              21,\n"
               "              22,\n"
               "              23,\n"
               "              24,\n"
               "              25,\n"
               "              26\n"
               "            ]\n"
               "          },\n"
               "          \"hierarchicalDocumentSymbolSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"formatting\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"rangeFormatting\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"onTypeFormatting\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"declaration\": {\n"
               "          \"linkSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"definition\": {\n"
               "          \"linkSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"typeDefinition\": {\n"
               "          \"linkSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"implementation\": {\n"
               "          \"linkSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"codeAction\": {\n"
               "          \"codeActionLiteralSupport\": {\n"
               "            \"codeActionKind\": {\n"
               "              \"valueSet\": [\n"
               "                \"\",\n"
               "                \"quickfix\",\n"
               "                \"refactor\",\n"
               "                \"refactor.extract\",\n"
               "                \"refactor.inline\",\n"
               "                \"refactor.rewrite\",\n"
               "                \"source\",\n"
               "                \"source.organizeImports\"\n"
               "              ]\n"
               "            }\n"
               "          },\n"
               "          \"isPreferredSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"codeLens\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"documentLink\": {\n"
               "          \"tooltipSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"colorProvider\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"rename\": {\n"
               "          \"prepareSupport\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"publishDiagnostics\": {\n"
               "          \"relatedInformation\": true,\n"
               "          \"tagSupport\": {\n"
               "            \"valueSet\": [\n"
               "              1,\n"
               "              2\n"
               "            ]\n"
               "          },\n"
               "          \"versionSupport\": false\n"
               "        },\n"
               "        \"foldingRange\": {\n"
               "          \"rangeLimit\": 5000,\n"
               "          \"lineFoldingOnly\": true,\n"
               "          \"dynamicRegistration\": true\n"
               "        },\n"
               "        \"selectionRange\": {\n"
               "          \"dynamicRegistration\": true\n"
               "        }\n"
               "      },\n"
               "      \"window\": {\n"
               "        \"workDoneProgress\": true\n"
               "      }\n"
               "    },\n"
               "    \"clientInfo\": {\n"
               "      \"name\": \"vscode\",\n"
               "      \"version\": \"1.61.0\"\n"
               "    },\n"
               "    \"trace\": \"verbose\",\n"
               "    \"workspaceFolders\": [\n"
               "      {\n"
               "        \"uri\": \"file:///home/malintha/Documents/wso2/experimental/projects/ballerina/error-constructor\",\n"
               "        \"name\": \"error-constructor\"\n"
               "      }\n"
               "    ]\n"
               "  }\n"
               "}\n";

          printf("internet json:\n%s\n\n", json_str);
          CeJsonObj_t obj = {};
          if(ce_json_parse(json_str, &obj, verbose)){
               printf("parsed json:\n");
               print_obj(&obj);
               ce_json_obj_free(&obj);
          }
     }

     return 0;
}
