#pragma once

// TODO
// - Locked Queue
// + Convert buffer to valid json string
// - Timeout for requests ?
// - Document management
//   + DidOpen
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
// - Move executable path and args into config.
// - Only enable clangd when command line option is used, find all references and check this.
//

#include "ce_subprocess.h"
#include "ce_json.h"
#include "ce.h"

#if defined(PLATFORM_WINDOWS)
	#include <handleapi.h>
#endif

#define MAX_COMMAND_SIZE 1024

typedef struct{
     int64_t request_id;
     CeJsonObj_t obj;
}CeClangDResponse_t;

typedef struct{
#if defined(PLATFORM_WINDOWS)
     HANDLE thread_handle;
     DWORD thread_id;
     HANDLE queue_mutex_handle;
#else
     pthread_t thread;
     // TODO: queue mutex
#endif
     CeSubprocess_t proc;

     // Buffer for stdout that user can inspect.
     // TODO: Can this grow too big ?
     CeBuffer_t* buffer;

     int64_t current_request_id;

     CeClangDResponse_t* response_queue;
}CeClangD_t;

bool ce_clangd_init(const char* executable_path,
                    CeClangD_t* clangd);

bool ce_clangd_file_open(CeClangD_t* clangd, CeBuffer_t* buffer);
bool ce_clangd_file_close(CeClangD_t* clangd, CeBuffer_t* buffer);

void ce_clangd_free(CeClangD_t* clangd);
