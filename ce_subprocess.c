#include "ce_subprocess.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

// NOTE: stderr is redirected to stdout
static pid_t bidirectional_popen(const char* cmd, int* in_fd, int* out_fd){
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

          // TODO: run user's SHELL ?
          execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
     }else{
         close(input_fds[0]);
         close(output_fds[1]);

         *in_fd = input_fds[1];
         *out_fd = output_fds[0];
     }

     return pid;
}

bool ce_subprocess_open(CeSubprocess_t* subprocess, const char* command){
     subprocess->pid = bidirectional_popen(command, &subprocess->stdin_fd, &subprocess->stdout_fd);
     if(subprocess->pid == 0) return false;
     subprocess->stdin = fdopen(subprocess->stdin_fd, "w");
     subprocess->stdout = fdopen(subprocess->stdout_fd, "r");
     return true;
}

void ce_subprocess_close_stdin(CeSubprocess_t* subprocess){
     if(!subprocess->stdin) return;

     FILE *in = subprocess->stdin;
     subprocess->stdin = NULL;
     // because fclose() is a cancellation point for pthread, I need to NULL
     // stdout prior to calling fclose so we are guaranteed we don't call it
     // again in a cleanup handler's call to ce_subprocess_close
     fclose(in);
     subprocess->stdin_fd = 0;
}

void ce_subprocess_kill(CeSubprocess_t* subprocess, int signal){
     if(subprocess->pid <= 0){
          return;
     }

     kill(subprocess->pid, signal);
}

int ce_subprocess_close(CeSubprocess_t* subprocess){
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

     FILE *out = subprocess->stdout;
     subprocess->stdout = NULL;
     // because fclose() will be a cancellation point for pthread, I need to NULL
     // stdout prior to calling fclose so we are guaranteed we don't call it
     // again in a cleanup handler's call to ce_subprocess_close
     if(out) fclose(out);

     return status;
}
