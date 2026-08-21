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
    bool is_range = false;

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

    bool does_read_race(const os_label &reader) {
        range_check read_race;
        range_check write_race;
        uint32_t seq;

        // To detect read-write races, we compare against the last writer and
        // reader range.
        do {
            seq = seqlock.begin_read();
            write_race = reader.range_relation(last_writer, false);
            read_race = reader.range_relation(last_reader_range, is_range);
        } while (!seqlock.read_was_safe(seq));

        // To enable detection of future races, we have to make sure we update
        // the reader range And make sure nothing is missed :) Unfortunately, we
        // can't upgrade our lock. We'll have to try again under an exclusive
        // lock. If someone else has already expanded to cover us, we can stop
        // early, since reader-writer checks are atomic.

        // We may need to update if we're not within or identical
        if (read_race == synced || read_race == parallel) {
            seqlock.begin_write();
            // First, grab an updated view
            write_race = reader.range_relation(last_writer, false);
            read_race = reader.range_relation(last_reader_range, is_range);
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
            seqlock.end_write();
        }

        switch (write_race) {
        case parallel:
        case within:
            return true;
        case synced:
        case identical:
            break;
        }
        return false;
    }

    bool does_write_race(const os_label &writer) {
        // TODO: test/cilksan/TestCases
        // TODO: Count distinct races?
        range_check read_race;
        range_check write_race;

        // We make this atomic and exclusive under the label lock to make
        // reasoning easier. That is, the stored writer label has checked
        // against the stored reader label
        {
            seqlock.begin_write();
            // To detect read-write races, we compare against the range of
            // possible readers.
            read_race = writer.range_relation(last_reader_range, is_range);
            // To detect write-write races, we compare against the last writer
            // (and set ourselves as last writer)
            write_race = writer.range_relation(last_writer, false);
            // Do not modify unless we have to :)
            if (write_race != identical) {
                last_writer.copy_from(writer);
            }
            seqlock.end_write();
        }

        switch (read_race) {
        case parallel:
        case within:
            return true;
        case synced:
        case identical:
            break;
        }
        switch (write_race) {
        case parallel:
        case within:
            return true;
        case synced:
        case identical:
            break;
        }
        return false;
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
#endif /* _OS_LABEL_H */
