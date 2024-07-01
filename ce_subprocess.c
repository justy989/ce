#include "ce_subprocess.h"

#if defined(PLATFORM_WINDOWS)
    #include <windows.h>
    // #include <handleapi.h>
    // #include <namedpipeapi.h>
    // #include <WinBase.h>

bool ce_subprocess_open(CeSubprocess_t* subprocess, const char* command) {
     // Setup a pipe so that we'll be able to read from stdout.
     subprocess->stdout_read_pipe = INVALID_HANDLE_VALUE;
     HANDLE stdout_write_pipe = INVALID_HANDLE_VALUE;

     SECURITY_ATTRIBUTES security_attributes;
     memset(&security_attributes, 0, sizeof(security_attributes));
     security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
     security_attributes.bInheritHandle = TRUE;
     security_attributes.lpSecurityDescriptor = NULL;

     if(!CreatePipe(&subprocess->stdout_read_pipe, &stdout_write_pipe, &security_attributes, 0)) {
         ce_log("CreatePipe() failed for subprocess");
         return false;
     }

     // idk, msdn said to do this. Windows is weird.
     if(!SetHandleInformation(subprocess->stdout_read_pipe, HANDLE_FLAG_INHERIT, 0)){
         ce_log("Couldn't ensure the read handle to the pipe for STDOUT is not inherited.");
         return false;
     }

     // Populate startup info and start the process.

     memset(&subprocess->process, 0, sizeof(subprocess->process));

     memset(&subprocess->startup_info, 0, sizeof(subprocess->startup_info));
     subprocess->startup_info.cb = sizeof(subprocess->startup_info);
     subprocess->startup_info.hStdError = stdout_write_pipe;
     subprocess->startup_info.hStdOutput = stdout_write_pipe;
     subprocess->startup_info.dwFlags |= STARTF_USESTDHANDLES;

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
         CloseHandle(stdout_write_pipe);
    }else{
         DWORD error_message_id = GetLastError();
         char buffer[BUFSIZ];
         DWORD rc = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
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
    TerminateProcess(subprocess->process.hProcess,
                     0);
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

     CloseHandle(subprocess->stdout_read_pipe);
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

// WINDOWS: process
// NOTE: stderr is redirected to stdout
static pid_t popen_with_stdout(const char* cmd, int* out_fd){
     int output_fds[2];

     if(pipe(output_fds) != 0) return 0;

     pid_t pid = fork();
     if(pid < 0) return 0;

     if(pid == 0){
          close(output_fds[0]);

          dup2(output_fds[1], STDOUT_FILENO);
          dup2(output_fds[1], STDERR_FILENO);

          // TODO: run user's SHELL ?
          execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
     }else{
         close(output_fds[1]);
         *out_fd = output_fds[0];
     }

     return pid;
}

bool ce_subprocess_open(CeSubprocess_t* subprocess, const char* command){
     // WINDOWS: process
     subprocess->pid = popen_with_stdout(command, &subprocess->stdout_fd);
     if(subprocess->pid == 0) return false;
     subprocess->stdout_file = fdopen(subprocess->stdout_fd, "r");
     return true;
}

void _close_file(FILE **file){
     FILE *to_close = *file;
     if(to_close == NULL) return;
     *file = NULL;
     // because fclose() is a cancellation point for pthread, I need to NULL
     // stdin prior to calling fclose() so we are guaranteed we don't do it
     // again in a cleanup handler
     fclose(to_close);
}

void ce_subprocess_close_stdin(CeSubprocess_t* subprocess){
     if(!subprocess->stdin_file) return;
     _close_file(&subprocess->stdin_file);
     subprocess->stdin_fd = 0;
}

void ce_subprocess_kill(CeSubprocess_t* subprocess, int signal){
     // WINDOWS: process
     if(subprocess->pid <= 0){
          return;
     }
     kill(subprocess->pid, signal);
}

int ce_subprocess_close(CeSubprocess_t* subprocess){
     // WINDOWS: process
     ce_subprocess_close_stdin(subprocess);

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

     _close_file(&subprocess->stdout_file);
     return status;
     return -1;
}
#endif
