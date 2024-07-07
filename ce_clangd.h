#pragma once

#include "ce_subprocess.h"
#include "ce.h"

#define MAX_COMMAND_SIZE 1024

typedef struct{
     const char* executable_path;
     CeSubprocess_t proc;
     CeBuffer_t* buffer;
#if !defined(PLATFORM_WINDOWS)
     pthread_t thread;
#endif
}CeClangD_t;

bool ce_clangd_init(const char* executable_path,
                    CeClangD_t* clangd);

void ce_clangd_free(CeClangD_t* clangd);
