#include "ce_clangd.h"
#include "ce_app.h"
#include "ce_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PLATFORM_WINDOWS)
    #include <windows.h>
#else
     #include <errno.h>
     #include <pthread.h>
     #include <unistd.h>
#endif

#define DEFAULT_MESSAGE_SIZE (16 * 1024 * 1024)
#define DEFAULT_HEADER_SIZE (1024)
#define READ_BLOCK_SIZE (4 * 1024)
#define MAX_HEADER_SIZE 128
#define MAX_PRINT_SIZE (1024 * 1024)

typedef struct{
     CeBuffer_t* buffer;
     CeSubprocess_t* proc;
}HandleOutputData_t;

typedef struct{
     char expected_header_prefix[MAX_HEADER_SIZE];
     char header[MAX_HEADER_SIZE];
     int64_t message_body_size;
     char* message_body;
}ParseResponse_t;

#if defined(PLATFORM_WINDOWS)
static bool _convert_windows_path_to_uri(char* path, int64_t max_len){
     const char prefix[] = "file:///";
     int64_t prefix_len = 8;
     int64_t path_len = strlen(path);
     int64_t new_path_len = path_len + prefix_len;
     if(new_path_len >= max_len){
          return false;
     }
     memmove(path + 8, path, path_len);
     memcpy(path, prefix, 8);
     for(int64_t i = 8; i < new_path_len; i++){
          if(path[i] == '\\'){
               path[i] = '/';
          }
     }
     return true;
}
#endif

static bool _calculate_filename_uri(const char* name, char* uri, size_t size){
#if defined(PLATFORM_WINDOWS)
     char* full_file_path = _fullpath(NULL, name, size);
     strncpy(uri, full_file_path, size);
     free(full_file_path);
     if(!_convert_windows_path_to_uri(uri, size)){
          return false;
     }
#else
     char* full_file_path = realpath(name, NULL);
     snprintf(uri, size, "file://%s", full_file_path);
     free(full_file_path);
#endif
     return true;
}

static char* _convert_string_to_json_string(const char* string){
     char* result = NULL;
     int64_t string_len = strlen(string);
     int64_t result_len = 0;
     int64_t utf8_string_len = ce_utf8_strlen(string);

     // Count how big we need to allocate the string.
     for(int64_t i = 0; i < string_len; i++){
          if(string[i] == '"' ||
             string[i] == '\\' ||
             string[i] == '/' ||
             string[i] == '\b' ||
             string[i] == '\f' ||
             string[i] == '\n' ||
             string[i] == '\r' ||
             string[i] == '\t'){
             // TODO: support \u
               result_len += 2;
          }else{
               result_len++;
          }
     }

     // Allocate and populate the new string with valid
     result = malloc(result_len + 1);
     int64_t r = 0;
     for(int64_t i = 0; i < string_len; i++){
          switch(string[i]){
          case '"':
          case '\\':
          case '/':
               result[r] = '\\';
               r++;
               result[r] = string[i];
               r++;
               break;
          case '\b':
               result[r] = '\\';
               r++;
               result[r] = 'b';
               r++;
               break;
          case '\f':
               result[r] = '\\';
               r++;
               result[r] = 'f';
               r++;
               break;
          case '\n':
               result[r] = '\\';
               r++;
               result[r] = 'n';
               r++;
               break;
          case '\r':
               result[r] = '\\';
               r++;
               result[r] = 'r';
               r++;
               break;
          case '\t':
               result[r] = '\\';
               r++;
               result[r] = 't';
               r++;
               break;
          default:
               result[r] = string[i];
               r++;
               break;
          }
     }
     // End with null terminator
     result[r] = 0;
     return result;
}

static bool _send_json_obj(CeJsonObj_t* obj, CeSubprocess_t* subprocess){
     // Build the message boyd
     char* message_body = malloc(DEFAULT_MESSAGE_SIZE);
     ce_json_obj_to_string(obj, message_body, DEFAULT_MESSAGE_SIZE - 1, 1);
     uint64_t message_len = strlen(message_body);

     // Prepend the header.
     char* header = malloc(DEFAULT_HEADER_SIZE);
     int64_t header_len = snprintf(header, DEFAULT_HEADER_SIZE, "Content-Length: %" PRId64 "\r\n\r\n",
                                             message_len);

     int64_t bytes_written = ce_subprocess_write_stdin(subprocess, header, header_len);
     free(header);
     if(bytes_written < 0){
          free(message_body);
          return false;
     }
     bytes_written = ce_subprocess_write_stdin(subprocess, message_body, message_len);
     free(message_body);
     if(bytes_written < 0){
          return false;
     }
     return true;
}

static bool _clangd_request_goto(CeClangD_t* clangd, CeBuffer_t* buffer, CePoint_t point, const char* method){
     char file_uri[MAX_PATH_LEN + 1];
     if(!_calculate_filename_uri(buffer->name, file_uri, MAX_PATH_LEN)){
          return false;
     }

     CeJsonObj_t obj = {};
     ce_json_obj_set_string(&obj, "jsonrpc", "2.0");
     ce_json_obj_set_number(&obj, "id", 1);
     ce_json_obj_set_string(&obj, "method", method);

     CeJsonObj_t param_obj = {};

     CeJsonObj_t pos_obj = {};
     ce_json_obj_set_number(&pos_obj, "line", point.y);
     ce_json_obj_set_number(&pos_obj, "character", point.x);
     ce_json_obj_set_obj(&param_obj, "position", &pos_obj);

     CeJsonObj_t text_document_obj = {};
     ce_json_obj_set_string(&text_document_obj, "uri", file_uri);
     ce_json_obj_set_obj(&param_obj, "textDocument", &text_document_obj);

     ce_json_obj_set_obj(&obj, "params", &param_obj);

     bool result = _send_json_obj(&obj, &clangd->proc);
     ce_json_obj_free(&text_document_obj);
     ce_json_obj_free(&pos_obj);
     ce_json_obj_free(&param_obj);
     ce_json_obj_free(&obj);
     return result;
}

bool parse_response_complete(ParseResponse_t* parse){
     if(parse->message_body != NULL &&
        (int64_t)(strlen(parse->message_body)) == parse->message_body_size){
          return true;
     }
     return false;
}

void parse_response_block(ParseResponse_t* parse,
                          char* block,
                          int64_t block_size){
     int64_t expected_header_prefix_len = 0;
     int64_t header_len = 0;
     int64_t message_body_len = 0;

     // If we haven't parsed the header yet.
     for(int64_t i = 0; i < block_size; i++){
          if(parse->message_body_size <= 0){
               // Calculate these values as needed.
               if(expected_header_prefix_len == 0){
                    expected_header_prefix_len = strlen(parse->expected_header_prefix);
               }
               if (header_len == 0){
                    header_len = strlen(parse->header);
               }
               // If we haven't matched the expected header prefix copy the bytes that match it.
               if(header_len < expected_header_prefix_len){
                    if(block[i] == parse->expected_header_prefix[header_len]){
                         parse->header[header_len] = block[i];
                         header_len++;
                    }else{
                         memset(parse->header, 0, MAX_HEADER_SIZE);
                         header_len = 0;
                    }
               }else{
                    // If we have copied the header prefix, continue coping until we get the \r\n\r\n pattern.
                    if(header_len >= MAX_HEADER_SIZE){
                         // Clear the header and retry. This can mean we might miss a message.
                         memset(parse->header, 0, MAX_HEADER_SIZE);
                         return;
                    }

                    parse->header[header_len] = block[i];
                    header_len++;

                    // Check for the end of the header.
                    if(parse->header[header_len - 4] == '\r' &&
                       parse->header[header_len - 3] == '\n' &&
                       parse->header[header_len - 2] == '\r' &&
                       parse->header[header_len - 1] == '\n'){
                         char* end = NULL;
                         parse->message_body_size = strtol(parse->header + expected_header_prefix_len, &end, 10);
                         if(parse->header == end){
                              memset(parse->header, 0, MAX_HEADER_SIZE);
                              return;
                         }
                         // Account for null terminator.
                         int64_t full_message_body_size = parse->message_body_size + 1;
                         parse->message_body = malloc(full_message_body_size);
                         memset(parse->message_body, 0, full_message_body_size);
                    }
               }
          }else{
               if(message_body_len == 0){
                    message_body_len = strnlen(parse->message_body, parse->message_body_size);
               }
               if(message_body_len >= parse->message_body_size){
                    return;
               }

               parse->message_body[message_body_len] = block[i];
               message_body_len++;
          }
     }
}

void parse_response_free(ParseResponse_t* parse){
     memset(parse->header, 0, MAX_HEADER_SIZE);
     if(parse->message_body){
          parse->message_body_size = 0;
          free(parse->message_body);
          parse->message_body = NULL;
     }
}

#if defined(PLATFORM_WINDOWS)
DWORD WINAPI handle_output_fn(void* user_data){
#else
static void* handle_output_fn(void* user_data){
#endif
     HandleOutputData_t* data = (HandleOutputData_t*)(user_data);
     // bool sent_definition_request = false;
     ParseResponse_t parse = {};
     strcpy(parse.expected_header_prefix, "Content-Length: ");

     char block[READ_BLOCK_SIZE];
     while(true){
          int64_t bytes_read = ce_subprocess_read_stdout(data->proc, block, (READ_BLOCK_SIZE - 1));
          if(bytes_read <= 0){
               break;
          }

          block[bytes_read] = 0;

          // Attempt to parse the messages before we sanitize and print them.
          parse_response_block(&parse, block, bytes_read);
          if(parse_response_complete(&parse)){
               CeJsonObj_t obj = {};
               if(ce_json_parse(parse.message_body, &obj, false)){
                    char* buffer = malloc(MAX_PRINT_SIZE + 1);
                    ce_json_obj_to_string(&obj, buffer, MAX_PRINT_SIZE, 1);
                    printf("%s\n", buffer);
                    ce_json_obj_free(&obj);

                    // TEST requests for reference.
                    // if(!sent_definition_request){
                    //      // NOTE: Explicitly does not have an id field.
                    //      ce_json_obj_set_string(&obj, "jsonrpc", "2.0");
                    //      ce_json_obj_set_string(&obj, "method", "textDocument/didOpen");

                    //      {
                    //           CeJsonObj_t param_obj = {};
                    //           CeJsonObj_t text_document_obj = {};
                    //           ce_json_obj_set_string(&text_document_obj, "uri", "file:///C:/Users/jtiff/source/repos/ce_config/ce/main.c");
                    //           ce_json_obj_set_string(&text_document_obj, "languageId", "c");
                    //           ce_json_obj_set_string(&text_document_obj, "text",
                    //                "#include <stdio.h>\\n"
                    //                "#include \\\"ce.h\\\"\\n"
                    //                "int main(){\\n"
                    //                "     CeBuffer_t* buffer = NULL;\\n"
                    //                "     printf(\\\"%lld\\\", (int64_t)(buffer));\\n"
                    //                "     return 0;\\n"
                    //                "}");
                    //           ce_json_obj_set_obj(&param_obj, "textDocument", &text_document_obj);

                    //           ce_json_obj_set_obj(&obj, "params", &param_obj);
                    //      }

                    //      // Print the message.
                    //      ce_json_obj_to_string(&obj, buffer, MAX_PRINT_SIZE, 1);
                    //      printf("%s\n", buffer);
                    //      _send_json_obj(&obj, data->proc);

                    //      ce_json_obj_free(&obj);

                    //      ce_json_obj_set_string(&obj, "jsonrpc", "2.0");
                    //      ce_json_obj_set_number(&obj, "id", 1);
                    //      ce_json_obj_set_string(&obj, "method", "textDocument/typeDefinition");

                    //      {
                    //           CeJsonObj_t param_obj = {};

                    //           CeJsonObj_t pos_obj = {};
                    //           ce_json_obj_set_number(&pos_obj, "line", 3);
                    //           ce_json_obj_set_number(&pos_obj, "character", 20);
                    //           ce_json_obj_set_obj(&param_obj, "position", &pos_obj);

                    //           CeJsonObj_t text_document_obj = {};
                    //           ce_json_obj_set_string(&text_document_obj, "uri", "file:///C:/Users/jtiff/source/repos/ce_config/ce/main.c");
                    //           ce_json_obj_set_obj(&param_obj, "textDocument", &text_document_obj);

                    //           ce_json_obj_set_obj(&obj, "params", &param_obj);
                    //      }

                    //      // Print the message.
                    //      ce_json_obj_to_string(&obj, buffer, MAX_PRINT_SIZE, 1);
                    //      printf("%s\n", buffer);

                    //      _send_json_obj(&obj, data->proc);
                    //      ce_json_obj_free(&obj);

                    //      sent_definition_request = true;
                    // }

                    free(buffer);
               }else{
                    printf("Failed to parse json obj\n");
               }
               parse_response_free(&parse);
          }

          // sanitize block for non-printable characters
          for(int i = 0; i < bytes_read; i++){
              if(block[i] < 32 && block[i] != '\n') block[i] = '?';
          }
          // Insert into the clangd buffer.
          CePoint_t end = ce_buffer_advance_point(data->buffer,
                                                  ce_buffer_end_point(data->buffer), 1);
          data->buffer->status = CE_BUFFER_STATUS_NONE;
          ce_buffer_insert_string(data->buffer, block, end);
          data->buffer->status = CE_BUFFER_STATUS_READONLY;
     }

     free(data);
     return 0;
}

bool ce_clangd_init(const char* executable_path,
                    CeClangD_t* clangd){
     char command[MAX_COMMAND_SIZE];
     // snprintf(command, MAX_COMMAND_SIZE, "%s --enable-config --log=verbose", executable_path);
     snprintf(command, MAX_COMMAND_SIZE, "%s --log=verbose", executable_path);
     if(!ce_subprocess_open(&clangd->proc, command, CE_PROC_COMM_STDIN | CE_PROC_COMM_STDOUT)){
          return false;
     }

     HandleOutputData_t* thread_data = malloc(sizeof(*thread_data));
     thread_data->buffer = clangd->buffer;
     thread_data->proc = &clangd->proc;

#if defined(PLATFORM_WINDOWS)
     clangd->thread_handle = CreateThread(NULL,
                                   0,
                                   handle_output_fn,
                                   thread_data,
                                   0,
                                   &clangd->thread_id);
     if(clangd->thread_handle == INVALID_HANDLE_VALUE){
          ce_log("failed to create thread to run command\n");
          return false;
     }
#else
     int rc = pthread_create(&clangd->thread, NULL, handle_output_fn, thread_data);
     if(rc != 0){
          ce_log("pthread_create() failed: '%s'\n", strerror(errno));
          return false;
     }
#endif

     // Build our initialization structure.
     CeJsonObj_t initialize_obj = {};
     ce_json_obj_set_string(&initialize_obj, "jsonrpc", "2.0");
     ce_json_obj_set_number(&initialize_obj, "id", 0);
     ce_json_obj_set_string(&initialize_obj, "method", "initialize");

     char cwd[MAX_PATH_LEN + 1];
     char cwd_uri[MAX_PATH_LEN + 1];

     CeJsonObj_t params_obj = {};

#if defined(PLATFORM_WINDOWS)
     DWORD pid = GetCurrentProcessId();
     _getcwd(cwd, MAX_PATH_LEN);
#else
     pid_t pid = getpid();
     getcwd(cwd, MAX_PATH_LEN);
#endif
     ce_json_obj_set_number(&params_obj, "processId", (double)(pid));

#if defined(PLATFORM_WINDOWS)
     strncpy(cwd_uri, cwd, MAX_PATH_LEN);
     _convert_windows_path_to_uri(cwd_uri, MAX_PATH_LEN);
     char* json_cwd = _convert_string_to_json_string(cwd);
#else
     snprintf(cwd_uri, MAX_PATH_LEN, "file://%s", cwd);
     char* json_cwd = _convert_string_to_json_string(cwd);
#endif

     ce_json_obj_set_string(&params_obj, "rootPath", json_cwd);
     ce_json_obj_set_string(&params_obj, "rootUri", cwd_uri);
     ce_json_obj_set_obj(&initialize_obj, "params", &params_obj);

     CeJsonObj_t client_info_obj = {};
     ce_json_obj_set_string(&client_info_obj, "name", "ce");
     ce_json_obj_set_string(&client_info_obj, "version", "9.8.7");
     ce_json_obj_set_obj(&initialize_obj, "ClientInfo", &client_info_obj);

     bool result = _send_json_obj(&initialize_obj, &clangd->proc);
     ce_json_obj_free(&initialize_obj);
     ce_json_obj_free(&params_obj);
     ce_json_obj_free(&client_info_obj);
     free(json_cwd);
     return result;
}

bool ce_clangd_file_open(CeClangD_t* clangd, CeBuffer_t* buffer){
     char file_uri[MAX_PATH_LEN + 1];
     if(!_calculate_filename_uri(buffer->name, file_uri, MAX_PATH_LEN)){
          return false;
     }

     char* buffer_str = ce_buffer_dupe(buffer);
     char* sanitized_buffer_str = _convert_string_to_json_string(buffer_str);

     CeJsonObj_t obj = {};
     ce_json_obj_set_string(&obj, "jsonrpc", "2.0");
     ce_json_obj_set_string(&obj, "method", "textDocument/didOpen");

     CeJsonObj_t text_document_obj = {};
     ce_json_obj_set_string(&text_document_obj, "uri", file_uri);
     ce_json_obj_set_string(&text_document_obj, "languageId", "c");
     ce_json_obj_set_string(&text_document_obj, "text", sanitized_buffer_str);
     CeJsonObj_t param_obj = {};
     ce_json_obj_set_obj(&param_obj, "textDocument", &text_document_obj);
     ce_json_obj_set_obj(&obj, "params", &param_obj);

     bool result = _send_json_obj(&obj, &clangd->proc);
     ce_json_obj_free(&param_obj);
     ce_json_obj_free(&text_document_obj);
     ce_json_obj_free(&obj);
     free(sanitized_buffer_str);
     free(buffer_str);
     return result;
}

bool ce_clangd_file_close(CeClangD_t* clangd, CeBuffer_t* buffer){
     char file_uri[MAX_PATH_LEN + 1];
     if(!_calculate_filename_uri(buffer->name, file_uri, MAX_PATH_LEN)){
          return false;
     }

     CeJsonObj_t obj = {};
     ce_json_obj_set_string(&obj, "jsonrpc", "2.0");
     ce_json_obj_set_string(&obj, "method", "textDocument/didClose");

     CeJsonObj_t text_document_obj = {};
     ce_json_obj_set_string(&text_document_obj, "uri", file_uri);
     CeJsonObj_t param_obj = {};
     ce_json_obj_set_obj(&param_obj, "textDocument", &text_document_obj);
     ce_json_obj_set_obj(&obj, "params", &param_obj);

     bool result = _send_json_obj(&obj, &clangd->proc);
     ce_json_obj_free(&param_obj);
     ce_json_obj_free(&text_document_obj);
     ce_json_obj_free(&obj);
     return result;
}

bool ce_clangd_request_goto_type_def(CeClangD_t* clangd, CeBuffer_t* buffer, CePoint_t point){
     return _clangd_request_goto(clangd, buffer, point, "textDocument/typeDefinition");
}

bool ce_clangd_request_goto_def(CeClangD_t* clangd, CeBuffer_t* buffer, CePoint_t point){
     return _clangd_request_goto(clangd, buffer, point, "textDocument/definition");
}

bool ce_clangd_request_goto_decl(CeClangD_t* clangd, CeBuffer_t* buffer, CePoint_t point){
     return _clangd_request_goto(clangd, buffer, point, "textDocument/declaration");
}

bool ce_clangd_request_goto_impl(CeClangD_t* clangd, CeBuffer_t* buffer, CePoint_t point){
     return _clangd_request_goto(clangd, buffer, point, "textDocument/implementation");
}

void ce_clangd_free(CeClangD_t* clangd){
#if defined(PLATFORM_WINDOWS)
     ce_subprocess_kill(&clangd->proc, 0);
     CloseHandle(clangd->thread_handle);
#else
     ce_subprocess_kill(&clangd->proc, SIGINT);
     pthread_join(clangd->thread, NULL);
#endif
     ce_subprocess_close(&clangd->proc);

     memset(clangd, 0, sizeof(*clangd));
}
