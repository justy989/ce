#pragma once

// TODO
// - Convert filename to uri
// - Convert buffer to valid json string
// - Timeout for requests
// - Locked Queue
// - Document management
//   - DidOpen
//   - DidClose
//   - DidChange: Save for after gotos
//     - Maybe initial implementation is to just send the whole file each time ?
// - Requests
//   - Goto definiton
//   - Goto declaration
//   - Goto type definition
//   - Goto implementaion
//   - Find references
//     - Display in ui
//   - Completion
//     - Display in ui
//   - Prepare Call Hierarchy
//     - Display in ui
// - Diagnostics
//   - Check for latest diagnostics
// - More initialize request capabilities ?
// - initialize response

#include "ce_subprocess.h"
#include "ce.h"

#if defined(PLATFORM_WINDOWS)
	#include <handleapi.h>
#endif

#define MAX_COMMAND_SIZE 1024

typedef struct{
#if defined(PLATFORM_WINDOWS)
     HANDLE thread_handle;
     DWORD thread_id;
#else
     pthread_t thread;
#endif
     const char* executable_path;
     CeSubprocess_t proc;
     CeBuffer_t* buffer;
}CeClangD_t;

bool ce_clangd_init(const char* executable_path,
                    CeClangD_t* clangd);

void ce_clangd_free(CeClangD_t* clangd);
