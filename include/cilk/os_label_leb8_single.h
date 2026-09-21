#ifndef _OS_LABEL_LEB8_SINGLE_H
#define _OS_LABEL_LEB8_SINGLE_H

#pragma GCC visibility push(default)

#include <cstdint>
#include <ostream>
#include <vector>

#pragma pack(push, 2)
struct os_label {
  // bit at index i indicates l or r at depth i
  // depths i = 4*k+[0,2] represent S nodes
  // depths i = 4*k+3 represent P nodes
  // ...76543210
  // ...PSSSPSSS
  //
  // At each spawn, we make use of the next unused p bit.
  // Left child is set to 0, right child is set to 1.
  //
  // At each sync, we need to increment S bits like a counter.
  // If this overflows a group of 3 s nodes, the carry should propagate through
  // the P bit, and the P bit should be set to 0 to be left unused
  //
  // Differentiating between an unused P bit vs a l/r child P bit is not
  // possible, so instead of popping a continuation, we use the parent's label
  // as reference
  uint64_t data[7] = {};
  uint16_t end_idx = 0;

  static constexpr int scan_max_low_offset_bytes = sizeof(data) - sizeof(uint64_t);

  void append_left_child();
  void append_right_child();
  uint16_t get_restore_point() const { return end_idx; }
  void restore_on_sync(uint16_t restore_idx);
  bool is_identical(const os_label &other) const;
  unsigned lca(const os_label &other) const;
  std::vector<uint8_t> to_vector() const;
};
#pragma pack(pop)

static_assert(sizeof(os_label) == 58, "os_label must be 58 bytes");

#ifdef ENABLE_LABEL_PRINTING
std::ostream &operator<<(std::ostream &os, const os_label &l);
#endif

#pragma GCC visibility pop

#endif // _OS_LABEL_LEB8_SINGLE_H
