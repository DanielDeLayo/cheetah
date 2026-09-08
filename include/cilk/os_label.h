#ifndef _OS_LABEL_H
#define _OS_LABEL_H

#pragma GCC visibility push(default)

#include "atomic_seqlock.h"
#include <ostream>

// Toggle which label representation to use here:
#include "os_label_leb8.h"
//#include "os_label_string.h"

class alignas(64) shadow_label {
    os_label last_writer;
    os_label last_reader_range;
    // Use a reader-writer lock
    // That is, hold exclusive and shared access for the labels.
    // Except, those are too big, so let's use a retry-seqlock instead.
    atomic_seqlock seqlock;
    bool is_range = false;

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

#pragma GCC visibility pop

#endif /* _OS_LABEL_H */
