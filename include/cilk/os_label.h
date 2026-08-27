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

    __attribute__((noinline))
    bool does_read_race_slow(const os_label &reader) {
        range_check read_race;
        range_check write_race;

        // To enable detection of future races, we have to make sure we update
        // the reader range And make sure nothing is missed :) Unfortunately, we
        // can't upgrade our lock. We'll have to try again under an exclusive
        // lock. If someone else has already expanded to cover us, we can stop
        // early, since reader-writer checks are atomic.
        seqlock.begin_write();
        // First, grab an updated view
        if (last_writer.is_empty() || reader.is_identical(last_writer)) {
            write_race = synced;
        } else {
            write_race = reader.range_relation(last_writer, false);
        }
        if (!is_range && reader.is_identical(last_reader_range)) {
            read_race = identical;
        } else {
            read_race = last_reader_range.is_empty()
                            ? synced
                            : reader.range_relation(last_reader_range, is_range);
        }
        // Determine if we need to update any information
        switch (read_race) {
        case within:
        case identical:
            seqlock.end_write();
            return write_race == parallel || write_race == within;
        case synced:
            last_reader_range.copy_from(reader);
            is_range = false;
            break;
        case parallel:
            is_range = true;
            reader.expand_parallel_range(last_reader_range);
            break;
        }
        // Our reader is in series with last_writer. Thus, we can prune last_writer.
        if (write_race == synced) {
            last_writer.clear();
        }
        seqlock.end_write();

        return write_race == parallel || write_race == within;
    }

    __attribute__((always_inline))
    bool does_read_race(const os_label &reader) {
        range_check read_race;
        range_check write_race;
        uint32_t seq;

        // To detect read-write races, we compare against the last writer and reader range.
        // We start with a fastpath readonly check
        do {
            seq = seqlock.begin_read();
            if (last_writer.is_empty() || reader.is_identical(last_writer)) {
                write_race = synced;
            } else {
                write_race = reader.range_relation(last_writer, false);
            }
            if (!is_range && reader.is_identical(last_reader_range)) {
                read_race = identical;
            } else {
                read_race = last_reader_range.is_empty()
                                ? synced
                                : reader.range_relation(last_reader_range, is_range);
            }
        } while (!seqlock.read_was_safe(seq));

        // We may need to update if we're not within or identical
        if (__builtin_expect(read_race != synced && read_race != parallel, 1)) {
            return write_race == parallel || write_race == within;
        }

        return does_read_race_slow(reader);
    }

    // Slow path: We have to update something and therefore check races.
    __attribute__((noinline))
    bool does_write_race_slow(const os_label &writer) {
        range_check read_race;
        range_check write_race;

        seqlock.begin_write();
        // To detect read-write races, we compare against the range of possible readers.
        read_race = last_reader_range.is_empty()
                        ? synced
                        : writer.range_relation(last_reader_range, is_range);
        // To detect write-write races, we compare against the last writer
        // (and set ourselves as last writer)
        write_race = last_writer.is_empty()
                         ? synced
                         : writer.range_relation(last_writer, false);
        // Do not modify unless we have to :)
        if (write_race != identical) {
            last_writer.copy_from(writer);
        }
        // Our reader is in series with our writer. Thus, we can prune it, as races are impossible.
        if (read_race == synced) {
            last_reader_range.clear();
            is_range = false;
        }
        seqlock.end_write();

        return (read_race == parallel || read_race == within) ||
               (write_race == parallel || write_race == within);
    }

    __attribute__((always_inline))
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
