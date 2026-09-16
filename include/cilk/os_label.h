#ifndef _OS_LABEL_H
#define _OS_LABEL_H

#pragma GCC visibility push(default)

#include "atomic_seqlock.h"
#include <ostream>

// Toggle which label representation to use here:
#include "os_label_leb8.h"
//#include "os_label_string.h"

struct alignas(64) shadow_label {
    // Cache line 0 (64 bytes): Read race fast path (fits entirely in 1 cache line)
    os_label last_reader_range;  // 56 bytes (offset 0..55)
    atomic_seqlock seqlock;      // 4 bytes  (offset 56..59)
    bool is_range = false;       // 1 byte   (offset 60)
    uint8_t _pad0[3] = {0};      // 3 bytes  (offset 61..63)

    // Cache line 1 (64 bytes): Write path
    os_label last_writer;        // 56 bytes (offset 64..119)
    uint8_t _pad1[8] = {0};      // 8 bytes  (offset 120..127)

  public:
    /*
     There's careful synchonization here.
     We have to consider read-read, read-write, and write-write races.
     And, to make read-read races (allowed races) fast, we should use a
     readers-writers (shared-exclusive) style of locking. However, we have to be
     careful-- we don't want a read-write race to miss.

    */

    __attribute__((noinline, cold, preserve_most, visibility("default")))
    bool does_read_race_slow(const os_label &reader);

    bool does_read_race(const os_label &reader);

    // Slow path: We have to update something and therefore check races.
    __attribute__((noinline, cold, preserve_most, visibility("default")))
    bool does_write_race_slow(const os_label &writer);

    bool does_write_race(const os_label &writer);

#ifdef ENABLE_LABEL_PRINTING
    inline friend std::ostream &operator<<(std::ostream &os,
                                           const shadow_label &l);
#endif
};

#ifdef ENABLE_LABEL_PRINTING
inline std::ostream &operator<<(std::ostream &os, const shadow_label &l) {
    os << "Last Writer: " << l.last_writer << std::endl;
    os << (l.is_range ? "Range" : "Point") << " Reader: " << l.last_reader_range
       << std::endl;
    return os;
}
#endif

static_assert(sizeof(shadow_label) == 128, "shadow_label must be 128 bytes");
static_assert(alignof(shadow_label) == 64, "shadow_label must be 64-byte cache-line aligned");
static_assert(__builtin_offsetof(shadow_label, last_reader_range) == 0,
              "last_reader_range must start at offset 0 (Cache Line 0)");
static_assert(__builtin_offsetof(shadow_label, seqlock) == 56,
              "seqlock must be at offset 56 (Cache Line 0)");
static_assert(__builtin_offsetof(shadow_label, is_range) == 60,
              "is_range must be at offset 60 (Cache Line 0)");
static_assert(__builtin_offsetof(shadow_label, last_writer) == 64,
              "last_writer must start at offset 64 (Cache Line 1)");

#pragma GCC visibility pop

#endif /* _OS_LABEL_H */
