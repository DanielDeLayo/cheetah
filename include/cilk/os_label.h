#ifndef _OS_LABEL_H
#define _OS_LABEL_H

//#include "codes.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <shared_mutex>

constexpr size_t __code_max_length = 5 * 64;
constexpr size_t __code_nbytes = __code_max_length/8;

using bitset = uint8_t[__code_nbytes];

// Represents your status reletive to the other label.
// Either you've synced since them, you're within the range, or your're simply in parallel
enum range_check {synced, within, parallel, identical};


#pragma pack(push, 1)
// shared mutex is 168 bytes. So I have to write my own.
class atomic_seqlock
{
  std::atomic<bool> has_writer;
  std::atomic_uint8_t seq{0};
  public:

  void begin_write()
  {
    // Loop until we get false back
    bool locked = true;
    while(has_writer.exchange(locked));

    // Since we're the only writer, we can simply add 1
    seq.fetch_add(1);
  }

  void end_write()
  {
    //Since we're the only writer, we can simply add 1
    seq.fetch_add(1);

    // As such, we can unlock
    has_writer.exchange(false);
  }

  uint32_t begin_read()
  {
    uint32_t ret;
    // Might as well wait until we've got an even number
    while((ret = has_writer.load()) % 2 == 1);
    return ret;
  }

  bool read_was_safe(uint32_t old_seq)
  {
    return seq.load() == old_seq;
  }

};

class os_label
{
  bitset labels = {0};
  uint8_t offset = 0;
  uint8_t conts = 0;
  // Store a count of continuations to remove on sync
 

public:
  // Encoding: offset-span labeling DOI:10.1145/125826.125861
  // TODO: 2 value bits, 1 child-direction bit, and 1 continuation bit
  // TODO: Gray code stuff? Right align, grow left, etc.

  void append_left_child()
  {
    labels[++offset] = 0;
    ++conts;
  }

  void append_right_child()
  {
    labels[++offset] = 1;
  }

  void restore_on_sync()
  {
    //if (conts == 0) return;
    // Clear left child
    for(; conts > 0; conts--)
      labels[offset--] = 0;
    // Increment Parent
    labels[offset] += 2;
  }

  size_t inline calc_matching_prefix_length(const os_label& rhs) const
  {
    size_t num_matches = 0;
    for (size_t i = 0; i <= offset && i <= rhs.offset; i++)
    {
      if (labels[i] == rhs.labels[i])
        num_matches++;
      else
        return num_matches;
    }
  }

  // Returns true if in parallel
  bool is_parallel(const os_label& rhs) const
  {
    // 4 cases:
    // 1. No LCA-- series
    // 2. LCA is one of them-- parent child, series
    // 3. LCA, both have longer labels. series or parallel depends on parity
    // 4. Identical labels-- series
    size_t num_matches = calc_matching_prefix_length(rhs);

    // Cases 1, 2, and 4. Offsets are indices of the last elements
    if (num_matches == 0 || num_matches == offset || num_matches == rhs.offset)
      return false;

    // Case 3: The tricky one.

    // Read the first label that differs. Bounds checking handled above.
    // In the first difference, if the parity is the same, then it's a parent-child relationship. 
    // Otherwise, it's two children (and in parallel).
    uint8_t left = labels[num_matches];
    uint8_t right = rhs.labels[num_matches];
    
    // Parity check
    return left % 2 != right % 2;
  }

  // Should fixup LCA range?
  range_check range_relation(const os_label& rhs, const bool& is_range) const
  {
    // This function handles multiple cases
    // 1. We have synced since the rhs label
    // 2. We are a descendent of the rhs label
    // 3. We are in parallel with the rhs label but outside the range
    size_t num_matches = calc_matching_prefix_length(rhs);
    
    // Case 1: same
    if (num_matches == offset && offset == rhs.offset)
      return identical;
    // Case 2: descendent
    if (num_matches == offset || num_matches == rhs.offset)
      return is_range ? within : synced;
    // Case 3: parallel
    if (is_parallel(rhs))
      return parallel;
    return synced;
  }

  // Fixup parallel LCA range
  void expand_parallel_range(os_label& rhs) const
  {
    // This function handles a single case.
    // 1. We are in parallel, but outside the range.
    // That is, we just shrink the prefix to the LCA
    size_t num_matches = calc_matching_prefix_length(rhs);
    
    for (size_t i = num_matches; i <= rhs.offset; i++)
    {
      rhs.labels[i] = 0;
    }
    rhs.offset = num_matches;
  }
  
  inline friend std::ostream& operator<<(std::ostream& os, const os_label& l);

};

class shadow_label
{
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
 And, to make read-read races (allowed races) fast, we should use a readers-writers (shared-exclusive) style of locking.
 However, we have to be careful-- we don't want a read-write race to miss.

 TODO FIXME: As written, there's a distinguishability problem. 
 Pretend you have a node with 3 children.
 How can you distingiush between a single reader (parent) and the LCA of two readers (same parent).
 Proposal: Maybe store some sort of depth to distinguish?
*/

  bool does_read_race(const os_label& reader)
  {
    range_check read_race;
    range_check write_race;
    uint32_t seq;


    // To detect read-write races, we compare against the last writer and reader range.
    do {
      seq = seqlock.begin_read();
      write_race = reader.range_relation(last_writer, false);
      read_race = reader.range_relation(last_reader_range, is_range);
    } while(!seqlock.read_was_safe(seq));

    // To enable detection of future races, we have to make sure we update the reader range
    // And make sure nothing is missed :)
    // Unfortunately, we can't upgrade our lock. We'll have to try again under an exclusive lock.
    // If someone else has already expanded to cover us, we can stop early, since reader-writer checks are atomic.

    //We may need to update if we're not within or identical
    if (read_race == synced || read_race == parallel){
      seqlock.begin_write();
      // First, grab an updated view
      write_race = reader.range_relation(last_writer, false);
      read_race = reader.range_relation(last_reader_range, is_range);
      // Determine if we need to update any information
      switch (read_race) {
        case within: case identical: seqlock.end_write(); return write_race;
        case synced: last_reader_range = reader; is_range = false; break;
        case parallel: is_range = true; reader.expand_parallel_range(last_reader_range); break;
      }
      seqlock.end_write();
    }

    switch (write_race) {
        case parallel:
        case within:
        return true;
    }
    return false;
  }

  bool does_write_race(const os_label& writer)
  {
      // TODO: test/cilksan/TestCases
      // TODO: Count distinct races?
      range_check read_race;
      range_check write_race;

      // We make this atomic and exclusive under the label lock to make reasoning easier.
      // That is, the stored writer label has checked against the stored reader label
      {
        seqlock.begin_write();
        // To detect read-write races, we compare against the range of possible readers.
        read_race = writer.range_relation(last_reader_range, is_range);
        // To detect write-write races, we compare against the last writer (and set ourselves as last writer)
        write_race = writer.range_relation(last_writer, false);
        last_writer = writer;
        seqlock.end_write();
      }

      std::cout << "READ:  " << read_race << std::endl;
      std::cout << "WRITE: " << write_race << std::endl;
      switch (read_race) {
          case parallel:
          case within:
          return true;
      }
      switch (write_race) {
          case parallel:
          case within:
          return true;
      }
      return false;
  }
  
  inline friend std::ostream& operator<<(std::ostream& os, const shadow_label& l);

};

#pragma pack(pop)

inline std::ostream& operator<<(std::ostream& os, const os_label& l) {
    os << "Length: " << l.offset+1 << ", Label:";
    for (size_t i = 0; i <= l.offset; i++) 
      os << " " << (uint64_t) l.labels[i]; // (uint8s are unfortunately chars)
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const shadow_label& l) {
    os << "Last Writer: " << l.last_writer << std::endl;
    os << (l.is_range ? "Range" : "Point") << " Reader: " << l.last_reader_range << std::endl;
    return os;
}
#endif /* _OS_LABEL_H */