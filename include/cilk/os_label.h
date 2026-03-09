#ifndef _OS_LABEL_H
#define _OS_LABEL_H

//#include "codes.h"
#include <cstddef>
#include <iostream>

constexpr size_t __code_max_length = 8 * 1024;
constexpr size_t __code_nbytes = __code_max_length/8;

using bitset = uint8_t[__code_nbytes];

class os_label
{
  bitset labels = {0};
  size_t offset = 0;
  // Store a count of continuations to remove on sync
  size_t conts = 0;

public:
  //Encoding: offset-span labeling DOI:10.1145/125826.125861

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

  // Returns true if in parallel
  bool operator||(const os_label&& rhs) const
  {
    // 4 cases:
    // 1. No LCA-- series
    // 2. LCA is one of them-- parent child, series
    // 3. LCA, both have longer labels. series or parallel depends on parity
    // 4. Identical labels-- series
    size_t num_matches = 0;
    for (size_t i = 0; i <= offset && i <= rhs.offset; i++)
    {
      if (labels[i] == rhs.labels[i])
        num_matches++;
      else
        break;
    }
    // Cases 1, 2, and 4. Offsets are indices of the last elements
    if (num_matches == 0 || num_matches == offset+1 || num_matches == rhs.offset+1)
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
  
  inline friend std::ostream& operator<<(std::ostream& os, const os_label& l);

};

inline std::ostream& operator<<(std::ostream& os, const os_label& l) {
    os << "Length: " << l.offset+1 << ", Label:";
    for (size_t i = 0; i <= l.offset; i++) 
      os << " " << (uint64_t) l.labels[i]; // (uint8s are unfortunately chars)
    return os;
}
#endif /* _OS_LABEL_H */