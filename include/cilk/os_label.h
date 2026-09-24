#ifndef _OS_LABEL_H
#define _OS_LABEL_H

#pragma GCC visibility push(default)

// Toggle which label representation to use:
#if defined(USE_OS_LABEL_STRING)
#include "os_label_string.h"
#elif defined(USE_OS_LABEL_LEB8_RANGE)
#include "os_label_leb8.h"
#else
#include "os_label_leb8_single.h"
#endif

// Per-strand state for race detectors that keep more than the label about the
// current strand (leb8-ptr keeps its label's table id here). Every pedigree
// frame reserves CILKRTS_STRAND_TOOL_WORDS words immediately after its os_label,
// zeroed when the frame is created, so a detector can find them from the
// label pointer it is given. A detector registered with
// __cilkrts_set_strand_hooks is called with those words at every spawn, before
// the parent becomes stealable, and at every sync, with the syncing function's
// __cilkrts_stack_frame so it can tell nested functions' syncs apart.
#define CILKRTS_STRAND_TOOL_WORDS 3
typedef void (*__cilkrts_spawn_hook)(void **parent, void **child,
                                     const void *parent_sf);
typedef void (*__cilkrts_sync_hook)(void **frame, const void *sync_sf);
struct __cilkrts_strand_hooks_t {
  __cilkrts_spawn_hook spawn;
  __cilkrts_sync_hook sync;
};
extern "C" void __cilkrts_set_strand_hooks(__cilkrts_spawn_hook spawn,
                                           __cilkrts_sync_hook sync);

#pragma GCC visibility pop

#endif /* _OS_LABEL_H */
