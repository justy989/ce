#include "ce_clangd.h"
#include "ce_app.h"
#include "ce_json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(PLATFORM_WINDOWS)
     #include <pthread.h>
     #include <unistd.h>
#endif

// #define READ_BLOCK_SIZE (4 * 1024)
#define READ_BLOCK_SIZE 32
#define MAX_HEADER_SIZE 128

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

static void* handle_output_fn(void* user_data){
     HandleOutputData_t* data = (HandleOutputData_t*)(user_data);
     bool sent_definition_request = false;
     ParseResponse_t parse = {};
     strcpy(parse.expected_header_prefix, "Content-Length: ");

     char block[READ_BLOCK_SIZE];
     while(true){
          int bytes_read = read(data->proc->stdout_fd, block, (READ_BLOCK_SIZE - 1));
          if(bytes_read > 0){
               block[bytes_read] = 0;

               // Attempt to parse the messages before we sanitize and print them.
               parse_response_block(&parse, block, bytes_read);
               if(parse_response_complete(&parse)){
                    CeJsonObj_t obj = {};
                    if(ce_json_parse(parse.message_body, &obj, false)){
                         const uint64_t MAX_PRINT_SIZE = 1024 * 1024;
                         char buffer[MAX_PRINT_SIZE];
                         ce_json_obj_to_string(&obj, buffer, MAX_PRINT_SIZE, 1);
                         printf("%s\n", buffer);
                         ce_json_obj_free(&obj);

                         // On receiving, send a request.
                         if(!sent_definition_request){
                              // NOTE: Explicitly does not have an id field.
                              ce_json_obj_set_string(&obj, "jsonrpc", "2.0");
                              ce_json_obj_set_string(&obj, "method", "textDocument/didOpen");

                              {
                                   CeJsonObj_t param_obj = {};
                                   CeJsonObj_t text_document_obj = {};
                                   ce_json_obj_set_string(&text_document_obj, "uri", "file:///home/jtiff/ce_config/ce/main.c");
                                   ce_json_obj_set_string(&text_document_obj, "languageId", "c");
                                   ce_json_obj_set_string(&text_document_obj, "text",
                                        "#include <stdio.h>\\n"
                                        "#include \\\"ce.h\\\"\\n"
                                        "int main(){\\n"
                                        "CeBuffer_t* buffer = NULL;\\n"
                                        "return 0;\\n"
                                        "}");
                                   ce_json_obj_set_obj(&param_obj, "textDocument", &text_document_obj);

                                   ce_json_obj_set_obj(&obj, "params", &param_obj);
                              }

                              // Print the message.
                              ce_json_obj_to_string(&obj, buffer, MAX_PRINT_SIZE, 1);
                              printf("%s\n", buffer);

                              // Build the message boyd
                              char message_body[BUFSIZ];
                              ce_json_obj_to_string(&obj, message_body, BUFSIZ, 1);
                              uint64_t message_len = strlen(message_body);

                              // Prepend the header.
                              char message[BUFSIZ];
                              int64_t total_bytes_to_write = snprintf(message, BUFSIZ, "Content-Length: %ld\r\n\r\n%s", message_len, message_body);

                              // Send it over the write fd.
                              int64_t total_bytes_written = 0;
                              while(total_bytes_written < total_bytes_to_write){
                                   int64_t bytes_written = write(data->proc->stdin_fd, message + total_bytes_written, total_bytes_to_write - total_bytes_written);
                                   if(bytes_written < 0){
                                        ce_log("write() failed on clangd stdin %s\n", strerror(errno));
                                        return false;
                                   }
                                   total_bytes_written += bytes_written;
                              }

                              ce_json_obj_free(&obj);

                              ce_json_obj_set_string(&obj, "jsonrpc", "2.0");
                              ce_json_obj_set_number(&obj, "id", 1);
                              ce_json_obj_set_string(&obj, "method", "textDocument/definition");

                              {
                                   CeJsonObj_t param_obj = {};

                                   CeJsonObj_t pos_obj = {};
                                   ce_json_obj_set_number(&pos_obj, "line", 4);
                                   ce_json_obj_set_number(&pos_obj, "character", 0);
                                   ce_json_obj_set_obj(&param_obj, "position", &pos_obj);

                                   CeJsonObj_t text_document_obj = {};
                                   ce_json_obj_set_string(&text_document_obj, "uri", "file:///home/jtiff/ce_config/ce/main.c");
                                   ce_json_obj_set_obj(&param_obj, "textDocument", &text_document_obj);

                                   ce_json_obj_set_obj(&obj, "params", &param_obj);
                              }

                              // Print the message.
                              ce_json_obj_to_string(&obj, buffer, MAX_PRINT_SIZE, 1);
                              printf("%s\n", buffer);

                              // Build the message boyd
                              ce_json_obj_to_string(&obj, message_body, BUFSIZ, 1);
                              message_len = strlen(message_body);

                              // Prepend the header.
                              total_bytes_to_write = snprintf(message, BUFSIZ, "Content-Length: %ld\r\n\r\n%s", message_len, message_body);

                              // Send it over the write fd.
                              total_bytes_written = 0;
                              while(total_bytes_written < total_bytes_to_write){
                                   int64_t bytes_written = write(data->proc->stdin_fd, message + total_bytes_written, total_bytes_to_write - total_bytes_written);
                                   if(bytes_written < 0){
                                        ce_log("write() failed on clangd stdin %s\n", strerror(errno));
                                        return false;
                                   }
                                   total_bytes_written += bytes_written;
                              }

          			     sent_definition_request = true;
                         }
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
          }else if(bytes_read < 0){
               ce_log("read() from clangd stdout failed.\n");
               break;
          }else{
               break;
          }
     }

     free(data);
     return NULL;
}

bool ce_clangd_init(const char* executable_path,
                    CeClangD_t* clangd){
     memset(clangd, 0, sizeof(*clangd));

     char command[MAX_COMMAND_SIZE];
     // snprintf(command, MAX_COMMAND_SIZE, "%s --enable-config --log=verbose", executable_path);
     snprintf(command, MAX_COMMAND_SIZE, "%s --log=verbose", executable_path);
     if(!ce_subprocess_open(&clangd->proc, command, CE_PROC_COMM_STDIN | CE_PROC_COMM_STDOUT)){
          return false;
     }

     clangd->buffer = new_buffer();
     ce_buffer_alloc(clangd->buffer, 1, "[clangd]");
     ce_buffer_empty(clangd->buffer);

     HandleOutputData_t* thread_data = malloc(sizeof(*thread_data));
     thread_data->buffer = clangd->buffer;
     thread_data->proc = &clangd->proc;

#if !defined(PLATFORM_WINDOWS)
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

     pid_t pid = getpid();
     char current_working_directory[BUFSIZ];
     getcwd(current_working_directory, BUFSIZ);
     char current_working_directory_uri[BUFSIZ];
     snprintf(current_working_directory_uri, BUFSIZ, "file://%s", current_working_directory);

     CeJsonObj_t params_obj = {};
     ce_json_obj_set_number(&params_obj, "processId", (double)(pid));
     ce_json_obj_set_string(&params_obj, "rootPath", current_working_directory);
     ce_json_obj_set_string(&params_obj, "rootUri", current_working_directory_uri);
     ce_json_obj_set_obj(&initialize_obj, "params", &params_obj);

     CeJsonObj_t client_info_obj = {};
     ce_json_obj_set_string(&client_info_obj, "name", "ce");
     ce_json_obj_set_string(&client_info_obj, "version", "9.8.7");
     ce_json_obj_set_obj(&initialize_obj, "ClientInfo", &client_info_obj);

     // Build the message boyd
     char message_body[BUFSIZ];
     ce_json_obj_to_string(&initialize_obj, message_body, BUFSIZ, 1);
     uint64_t message_len = strlen(message_body);

     // Prepend the header.
     char message[BUFSIZ];
     int64_t total_bytes_to_write = snprintf(message, BUFSIZ, "Content-Length: %ld\r\n\r\n%s", message_len, message_body);

     // Send it over the write fd.
     int64_t total_bytes_written = 0;
     while(total_bytes_written < total_bytes_to_write){
          int64_t bytes_written = write(clangd->proc.stdin_fd, message + total_bytes_written, total_bytes_to_write - total_bytes_written);
          if(bytes_written < 0){
               ce_log("write() failed on clangd stdin %s\n", strerror(errno));
               return false;
          }
          total_bytes_written += bytes_written;
     }
     return true;
}

void ce_clangd_free(CeClangD_t* clangd){
#if defined(PLATFORM_WINDOWS)
     ce_subprocess_kill(&clangd->proc, 0);
#else
     ce_subprocess_kill(&clangd->proc, SIGINT);
     pthread_join(clangd->thread, NULL);
#endif
     ce_subprocess_close(&clangd->proc);

     memset(clangd, 0, sizeof(*clangd));
}
