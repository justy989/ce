#pragma once

#include <stdio.h>
#include <stdbool.h>

#if defined(PLATFORM_WINDOWS)
	#include <processthreadsapi.h>
#else
    #include <sys/wait.h>
#endif

typedef enum {
    CE_PROC_COMM_NONE = 0,
    CE_PROC_COMM_STDIN = 1,
    CE_PROC_COMM_STDOUT = 2,
}CeProcCommFlag_t;

typedef struct{
#if defined(PLATFORM_WINDOWS)
     STARTUPINFOA startup_info;
     PROCESS_INFORMATION process;
     HANDLE stdout_read_pipe;
     HANDLE stdin_write_pipe;
#else
     pid_t pid;
     // You should either use the file descriptor, or the FILE pointer.
     // do not mix and match.
     // stdout will also contain stderr.
     int stdout_fd;
     FILE *stdout_file;
     int stdin_fd;
     FILE *stdin_file;
#endif
}CeSubprocess_t;

// run the provided shell command as a subprocess. Only stdout is provided.
bool ce_subprocess_open(CeSubprocess_t* subprocess, const char* command, CeProcCommFlag_t comms);
// send the specified signal to the subprocess
void ce_subprocess_kill(CeSubprocess_t* subprocess, int signal);
// close all subprocess fds and fps and wait for the subprocess to complete
// returns the exit status from the waitpid() call
int ce_subprocess_close(CeSubprocess_t* subprocess);
