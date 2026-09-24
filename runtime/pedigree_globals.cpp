#include "pedigree-internal.h"

// This variable needs to be accessed both from the external pedigree library
// and the pedigree-extension code in the core runtime library.
uint64_t *__pedigree_dprng_m_array = nullptr;

// Strand hooks for race detectors; see __cilkrts_set_strand_hooks in
// os_label.h. Defined here because, like the array above, it is used both by
// the pedigree-extension code compiled into programs and by the runtime.
__cilkrts_strand_hooks_t __cilkrts_strand_hooks = {nullptr, nullptr};

extern "C" void __cilkrts_set_strand_hooks(__cilkrts_spawn_hook spawn,
                                           __cilkrts_sync_hook sync) {
    __cilkrts_strand_hooks.spawn = spawn;
    __cilkrts_strand_hooks.sync = sync;
}
