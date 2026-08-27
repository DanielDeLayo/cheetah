#ifndef _OS_LABEL_H
#define _OS_LABEL_H

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

    __attribute__((visibility("default")))
    bool does_read_race_slow(const os_label &reader);

    bool does_read_race(const os_label &reader) {
        uint32_t seq;
        bool is_same_reader = false;

        // Fastpath check: if reader is identical to last_reader_range or within the
        // parallel LCA range, do not acquire write lock and do not touch write register.
        do {
            seq = seqlock.begin_read();
            if (__builtin_expect(!is_range, 1)) {
                is_same_reader = reader.is_identical(last_reader_range);
            } else {
                range_check rel = reader.range_relation(last_reader_range, true);
                is_same_reader = (rel == within || rel == identical);
            }
        } while (!seqlock.read_was_safe(seq));

        if (__builtin_expect(is_same_reader, 1)) {
            return false;
        }

        return does_read_race_slow(reader);
    }

    // Slow path: We have to update something and therefore check races.
    __attribute__((visibility("default")))
    bool does_write_race_slow(const os_label &writer);

    bool does_write_race(const os_label &writer) {
        // Optimistically read the last_writer:
        // If the writer hasn't changed, then we can simply leave.
        // After all, any intervening reader already checked against this writer.
        uint32_t seq;
        bool is_same_writer = false;

        do {
            seq = seqlock.begin_read();
            is_same_writer = writer.is_identical(last_writer);
        } while (!seqlock.read_was_safe(seq));

        if (__builtin_expect(is_same_writer, 1)) {
            return false;
        }

        return does_write_race_slow(writer);
    }

    inline friend std::ostream &operator<<(std::ostream &os,
                                           const shadow_label &l);
};

inline std::ostream &operator<<(std::ostream &os, const shadow_label &l) {
    os << "Last Writer: " << l.last_writer << std::endl;
    os << (l.is_range ? "Range" : "Point") << " Reader: " << l.last_reader_range
       << std::endl;
    return os;
}

static_assert(sizeof(shadow_label) == 128, "shadow_label must be 128 bytes");

#endif /* _OS_LABEL_H */
