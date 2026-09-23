#ifndef _CILKPRACE_ABLATION_H
#define _CILKPRACE_ABLATION_H

// Compile-time toggles for individual cilkprace/leb8-single optimizations, so
// each one's contribution can be measured independently.
//
// Every knob defaults to 1 (optimization enabled). A sweep driver overrides
// them by rewriting cilkprace_ablation_config.h, which is included below; that
// file is checked in holding no overrides, so a plain build is always the
// fully-optimized one. See race_detection_examples/tools/ablate.py.
//
// Turning a knob off must leave the detector *correct* -- the disabled path has
// to compute the same answer, just more slowly. The one exception is
// CILKPRACE_ABL_SEQLOCK, flagged below.

#include "cilkprace_ablation_config.h"

// P-node + write_depth guard and inline lca() in does_read_race, which lets a
// reader widen the active_reader range without taking the write lock. Off:
// reads fall through to does_read_race_slow, which redoes the same test.
#ifndef CILKPRACE_ABL_READ_WIDEN_FASTPATH
#define CILKPRACE_ABL_READ_WIDEN_FASTPATH 1
#endif

// Same-strand re-read in does_read_race: a read whose label equals
// active_reader, with write_depth showing no parallel write, returns without
// leaving the inlined fast path. READ_WIDEN_FASTPATH only covers entries that
// summarize a set of parallel readers (end_idx % 4 == 3), so without this a
// strand re-reading its own data always went out of line just for
// does_read_race_slow's is_identical to say "nothing to do" -- 42-97% of reads
// on cilksort, qsort, nqueens and strassen. Off: those reads take that slow
// path, which applies the same test.
#ifndef CILKPRACE_ABL_READ_IDENT_FASTPATH
#define CILKPRACE_ABL_READ_IDENT_FASTPATH 1
#endif

// Optimistic same-writer check at the top of does_write_race. Off: every write
// calls does_write_race_slow.
#ifndef CILKPRACE_ABL_WRITE_FASTPATH
#define CILKPRACE_ABL_WRITE_FASTPATH 1
#endif

// Seqlock synchronization on shadow_label. Off: begin_read/read_was_safe/the
// write lock all become no-ops.
//
// UNSOUND with more than one worker -- this measures the upper bound on what
// synchronization costs, and is only a valid configuration for the
// CILK_NWORKERS=1 sweep. The driver refuses to run it multi-threaded.
#ifndef CILKPRACE_ABL_SEQLOCK
#define CILKPRACE_ABL_SEQLOCK 1
#endif

// `#pragma unroll 2` on the per-granule loops in register_read/register_write.
#ifndef CILKPRACE_ABL_GRANULE_UNROLL
#define CILKPRACE_ABL_GRANULE_UNROLL 1
#endif

// Width-specialized early-outs in os_label::is_identical and os_label::lca that
// avoid looping when both labels fit in the first one or two 64-bit words.
// Off: the generic word loop handles every case.
#ifndef CILKPRACE_ABL_LABEL_CMP_FASTPATH
#define CILKPRACE_ABL_LABEL_CMP_FASTPATH 1
#endif

// __builtin_expect hints on the hot-path branches.
#ifndef CILKPRACE_ABL_BRANCH_HINTS
#define CILKPRACE_ABL_BRANCH_HINTS 1
#endif

#if CILKPRACE_ABL_BRANCH_HINTS
#define CILKPRACE_LIKELY(x) __builtin_expect(!!(x), 1)
#define CILKPRACE_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define CILKPRACE_LIKELY(x) (x)
#define CILKPRACE_UNLIKELY(x) (x)
#endif

#endif /* _CILKPRACE_ABLATION_H */
