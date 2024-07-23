#include "ce_subprocess.h"
#include "ce.h"

#include <string.h>
#include <stdlib.h>

#if defined(PLATFORM_WINDOWS)
    #include <windows.h>

bool ce_subprocess_open(CeSubprocess_t* subprocess, const char* command, CeProcCommFlag_t comms, bool use_shell) {
     // Setup a pipe so that we'll be able to read from stdout.
     subprocess->stdout_read_pipe = INVALID_HANDLE_VALUE;
     subprocess->stdin_write_pipe = INVALID_HANDLE_VALUE;
     HANDLE stdout_write_pipe = INVALID_HANDLE_VALUE;
     HANDLE stdin_read_pipe = INVALID_HANDLE_VALUE;

     SECURITY_ATTRIBUTES security_attributes;
     memset(&security_attributes, 0, sizeof(security_attributes));
     security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
     security_attributes.bInheritHandle = TRUE;
     security_attributes.lpSecurityDescriptor = NULL;

     // Populate startup info and start the process.
     memset(&subprocess->process, 0, sizeof(subprocess->process));
     memset(&subprocess->startup_info, 0, sizeof(subprocess->startup_info));
     subprocess->startup_info.cb = sizeof(subprocess->startup_info);
     subprocess->startup_info.dwFlags |= STARTF_USESTDHANDLES;

     if(comms & CE_PROC_COMM_STDOUT){
         if(!CreatePipe(&subprocess->stdout_read_pipe, &stdout_write_pipe, &security_attributes, 0)) {
             ce_log("CreatePipe() failed for subprocess");
             return false;
         }

         // idk, msdn said to do this. Windows is weird.
         if(!SetHandleInformation(subprocess->stdout_read_pipe, HANDLE_FLAG_INHERIT, 0)){
             ce_log("Couldn't ensure the read handle to the pipe for STDOUT is not inherited.");
             return false;
         }
         subprocess->startup_info.hStdError = stdout_write_pipe;
         subprocess->startup_info.hStdOutput = stdout_write_pipe;
     }

     if(comms & CE_PROC_COMM_STDIN){
         if(!CreatePipe(&stdin_read_pipe, &subprocess->stdin_write_pipe, &security_attributes, 0)) {
             ce_log("CreatePipe() failed for subprocess");
             return false;
         }

         if(!SetHandleInformation(subprocess->stdin_write_pipe, HANDLE_FLAG_INHERIT, 0)){
             ce_log("Couldn't ensure the read handle to the pipe for STDIN is not inherited.");
             return false;
         }
         subprocess->startup_info.hStdInput = stdin_read_pipe;
     }

     bool success = CreateProcess(NULL,
                                  (char*)command,
                                  NULL,
                                  NULL,
                                  TRUE,
                                  0,
                                  NULL,
                                  NULL,
                                  &subprocess->startup_info,
                                  &subprocess->process);
    if(success){
         if(stdout_write_pipe != INVALID_HANDLE_VALUE){
             CloseHandle(stdout_write_pipe);
         }
         if(stdin_read_pipe != INVALID_HANDLE_VALUE){
             CloseHandle(stdin_read_pipe);
         }
    }else{
         DWORD error_message_id = GetLastError();
         char buffer[BUFSIZ];
         FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
                       NULL,
                       error_message_id,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       buffer,
                       BUFSIZ,
                       NULL);
        ce_log("CreateProcess() failed with code %d : error %s\n",
               error_message_id,
               buffer);
    }

    return success;
}

void ce_subprocess_kill(CeSubprocess_t* subprocess, int signal) {
    if (WaitForSingleObject(subprocess->process.hProcess, 0) == WAIT_TIMEOUT) {
         TerminateProcess(subprocess->process.hProcess,
                          signal);
    }
}

void ce_subprocess_close_stdin(CeSubprocess_t* subprocess){
     if(subprocess->stdin_write_pipe != INVALID_HANDLE_VALUE){
         CloseHandle(subprocess->stdin_write_pipe);
         subprocess->stdin_write_pipe = INVALID_HANDLE_VALUE;
     }
}

int ce_subprocess_close(CeSubprocess_t* subprocess) {
     if(subprocess->process.hProcess == INVALID_HANDLE_VALUE){
         return -1;
     }
     WaitForSingleObject(subprocess->process.hProcess,
                         INFINITE);
     DWORD exit_code = 0;
     if(!GetExitCodeProcess(subprocess->process.hProcess, &exit_code)){
         ce_log("GetExitCodeProcess() failed for pid: %d\n", subprocess->process.dwProcessId);
     }

     if(subprocess->stdout_read_pipe != INVALID_HANDLE_VALUE){
         CloseHandle(subprocess->stdout_read_pipe);
     }
     if(subprocess->stdin_write_pipe != INVALID_HANDLE_VALUE){
         CloseHandle(subprocess->stdin_write_pipe);
     }
     CloseHandle(subprocess->process.hProcess);
     CloseHandle(subprocess->process.hThread);
     subprocess->process.hProcess = INVALID_HANDLE_VALUE;
     return exit_code;
}

#else

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

char** _split_command_args(const char* command){
    int64_t count = 0;
    char** result = NULL;

    char* token = strtok((char*)(command), " ");

    while(token){
        int64_t new_count = count + 1;
        result = realloc(result, new_count * sizeof(result[0]));
        result[count] = strdup(token);
        count = new_count;
        token = strtok(NULL, " ");
    }

    // End with a NULL per the execv() man page.
    int64_t new_count = count + 1;
    result = realloc(result, new_count * sizeof(result[0]));
    result[count] = NULL;
    return result;
}

// NOTE: stderr is redirected to stdout
static pid_t bidirectional_popen(const char* cmd, CeProcCommFlag_t comms, int* in_fd, int* out_fd,
                                 bool use_shell){
     int input_fds[2];
     int output_fds[2];

     if(pipe(input_fds) != 0) return 0;
     if(pipe(output_fds) != 0) return 0;

     pid_t pid = fork();
     if(pid < 0) return 0;

     if(pid == 0){
          close(input_fds[1]);
          close(output_fds[0]);

          dup2(input_fds[0], STDIN_FILENO);
          dup2(output_fds[1], STDOUT_FILENO);
          dup2(output_fds[1], STDERR_FILENO);

          if(use_shell){
              // TODO: run user's SHELL ?
              execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
          }else{
              char** args = _split_command_args(cmd);
              execv(args[0], args);
              // Idk if we can actually free our memory here but whateva...
              int64_t args_index = 0;
              while(args[args_index]){
                  free(args[args_index]);
                  args_index++;
              }
              free(args);
          }
     }else{
         close(input_fds[0]);
         close(output_fds[1]);
         if(comms & CE_PROC_COMM_STDIN){
             *in_fd = input_fds[1];
         }else{
             close(input_fds[1]);
             *in_fd = -1;
         }
         if(comms & CE_PROC_COMM_STDOUT){
             *out_fd = output_fds[0];
         }else{
             close(output_fds[0]);
             *out_fd = -1;
         }
     }

     return pid;
}

static void _close_file(FILE **file){
     FILE *to_close = *file;
     if(to_close == NULL) return;
     *file = NULL;
     // because fclose() is a cancellation point for pthread, I need to NULL
     // stdin prior to calling fclose() so we are guaranteed we don't do it
     // again in a cleanup handler
     fclose(to_close);
}

bool ce_subprocess_open(CeSubprocess_t* subprocess, const char* command, CeProcCommFlag_t comms,
                        bool use_shell){
     subprocess->pid = bidirectional_popen(command, comms, &subprocess->stdin_fd,
                                           &subprocess->stdout_fd, use_shell);
     if(subprocess->pid == 0) return false;
     if(subprocess->stdin_fd >= 0){
         subprocess->stdin_file = fdopen(subprocess->stdin_fd, "w");
     }
     if(subprocess->stdout_fd >= 0){
         subprocess->stdout_file = fdopen(subprocess->stdout_fd, "r");
     }
     return true;
}

void ce_subprocess_kill(CeSubprocess_t* subprocess, int signal){
     if(subprocess->pid <= 0){
          return;
     }
     kill(subprocess->pid, signal);
}

void ce_subprocess_close_stdin(CeSubprocess_t* subprocess){
     if(subprocess->stdin_fd >= 0){
         _close_file(&subprocess->stdin_file);
         close(subprocess->stdin_fd);
         subprocess->stdin_fd = -1;
     }
}

int ce_subprocess_close(CeSubprocess_t* subprocess){
     // wait for the subprocess to complete
     int status;
     do{
          if(waitpid(subprocess->pid, &status, 0) == -1){
               switch(errno){
               case EINTR:
                    continue;
               default:
                    // TODO: not sure what to do here
                    return 0;
               }
          }
     }while(!WIFEXITED(status) && !WIFSIGNALED(status));
     assert(!(WIFCONTINUED(status)));

     if(subprocess->stdout_fd >= 0){
         _close_file(&subprocess->stdout_file);
         close(subprocess->stdout_fd);
     }
     if(subprocess->stdin_fd >= 0){
         _close_file(&subprocess->stdin_file);
         close(subprocess->stdin_fd);
     }
     return status;
}
#endif

int64_t ce_subprocess_read_stdout(CeSubprocess_t* subprocess, char* buffer, int64_t size){
#if defined(PLATFORM_WINDOWS)
     if(subprocess->stdout_read_pipe == INVALID_HANDLE_VALUE){
          ce_log("subprocess created without enabling reading stdout\n");
          return -1;
     }
     DWORD bytes_read = 0;
     ReadFile(subprocess->stdout_read_pipe, buffer, size, &bytes_read, NULL);
     if(bytes_read < 0){
          ce_log("read() from clangd stdout failed.\n");
          return -1;
     }
     return bytes_read;

#else
     if(subprocess->stdout_fd < 0){
          ce_log("subprocess created without enabling reading stdout\n");
          return -1;
     }
     int bytes_read = read(subprocess->stdout_fd, buffer, size);
     if(bytes_read < 0){
          if(errno != EINTR && errno != EAGAIN){
              ce_log("read() failed on subprocess pid %d stdin %s\n", strerror(errno));
          }
          return bytes_read;
     }
     return bytes_read;
#endif
}

int64_t ce_subprocess_write_stdin(CeSubprocess_t* subprocess, char* buffer, int64_t size){
     int64_t total_bytes_written = 0;
     while(total_bytes_written < size){
#if defined(PLATFORM_WINDOWS)
          if(subprocess->stdin_write_pipe == INVALID_HANDLE_VALUE){
               ce_log("subprocess created without enabling reading stdout\n");
               return -1;
          }
          DWORD bytes_written = 0;
          bool success = WriteFile(subprocess->stdin_write_pipe, buffer + total_bytes_written,
                                   size - total_bytes_written,
                                   &bytes_written, NULL);
          if(!success || bytes_written < 0){
               ce_log("write() failed on clangd stdin\n");
               return -1;
          }
#else
          if(subprocess->stdin_fd < 0){
               ce_log("subprocess created without enabling reading stdout\n");
               return -1;
          }
          int64_t bytes_written = write(subprocess->stdin_fd, buffer + total_bytes_written,
                                        size - total_bytes_written);
          if(bytes_written < 0){
               if(errno != EINTR && errno != EAGAIN){
                   ce_log("write() failed on clangd stdin %s\n", strerror(errno));
               }
               return bytes_written;
          }

#endif
          total_bytes_written += bytes_written;
     }
     return total_bytes_written;
}
