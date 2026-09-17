#ifndef _OS_LABEL_H
#define _OS_LABEL_H

#include "atomic_seqlock.h"
#include <ostream>

// Toggle which label representation to use here:
#include "os_label_leb8.h"
//#include "os_label_string.h"

#pragma pack(push, 2)
class alignas(64) shadow_label {
    os_label active_reader;
    uint16_t write_depth = 0;
    // Use a reader-writer lock
    // That is, hold exclusive and shared access for the labels.
    // Except, those are too big, so let's use a retry-seqlock instead.
    atomic_seqlock seqlock;

  public:
    /*
     There's careful synchonization here.
     We have to consider read-read, read-write, and write-write races.
     And, to make read-read races (allowed races) fast, we should use a
     readers-writers (shared-exclusive) style of locking. However, we have to be
     careful-- we don't want a read-write race to miss.

    */

    bool does_read_race(const os_label &reader);
    bool does_write_race(const os_label &writer);

#ifdef ENABLE_LABEL_PRINTING
    inline friend std::ostream &operator<<(std::ostream &os,
                                           const shadow_label &l);
#endif
};
#pragma pack(pop)

#ifdef ENABLE_LABEL_PRINTING
inline std::ostream &operator<<(std::ostream &os, const shadow_label &l) {
    os << "Last Writer: " << l.last_writer << std::endl;
    os << (l.is_range ? "Range" : "Point") << " Reader: " << l.last_reader_range
       << std::endl;
    return os;
}
#endif

static_assert(sizeof(os_label) == 58, "os_label must be 58 bytes");
static_assert(sizeof(shadow_label) == 64, "shadow_label must be 64 bytes");

#endif /* _OS_LABEL_H */
